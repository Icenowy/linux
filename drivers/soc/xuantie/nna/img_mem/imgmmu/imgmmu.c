/*!
 *****************************************************************************
 *
 * @File         imgmmu.c
 * @Description  Implementation of the MMU functions
 * ---------------------------------------------------------------------------
 *
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "mmulib/mmu.h"
#include "mmulib/heap.h"	/* for struct imgmmu_halloc */

/*-----------------------------------------------------------------------------
 * Following elements are in the IMGMMU_lib_int module
 *---------------------------------------------------------------------------*/

/* access to MMU info and error printing function */
#include "mmu_defs.h"

#include <asm/page.h>

static int pte_cache_mode;
module_param(pte_cache_mode, int, 0444);
MODULE_PARM_DESC(pte_cache_mode,
    "PTE ax_cache signals. Acceptable values:<0-15>, refer to MMUv3 spec.");

static bool pte_rb_check = true;
module_param(pte_rb_check, bool, 0444);
MODULE_PARM_DESC(pte_rb_check,
    "Enables PTE read-back checks");

/** variable page shift */
static size_t g_mmupageshift = IMGMMU_PAGE_SHIFT;

/* Page table index mask in virtual address - low bits */
static uint64_t VIRT_PAGE_TBL_MASK(void) {
	return ((((1ULL<<IMGMMU_CAT_SHIFT)-1) & ((1ULL<<IMGMMU_DIR_SHIFT)-1)) &
			~(((1ULL<<g_mmupageshift)-1)));
}

/* Directory index mask in virtual address - middle bits */
static const uint64_t VIRT_DIR_IDX_MASK
	= (((1ULL<<IMGMMU_CAT_SHIFT)-1) & ~((1ULL<<IMGMMU_DIR_SHIFT)-1));

/* Catalogue index mask in virtual address - high bits */
static const uint64_t VIRT_CAT_IDX_MASK = (~((1ULL<<IMGMMU_CAT_SHIFT)-1));


/*
 * Catalogue entry in the MMU - contains up to 1024 directory mappings
 */
struct imgmmu_cat {
	/* Physical page used for the catalogue entries */
	struct imgmmu_page *page;
	/* All the page directory structures in
	 * a static array of pointers
	 */
	struct imgmmu_dir **dir_map;

	/*
	 * Functions to use to manage pages allocation,
	 * liberation and writing
	 */
	struct imgmmu_info config;

	/* number of mapping using this catalogue (PCEs) */
	uint32_t nmap;
};

/* Directory entry in the MMU - contains several page mapping */
struct imgmmu_dir {
	/* associated catalogue */
	struct imgmmu_cat *cat;
	/* Physical page used for the directory entries */
	struct imgmmu_page *page;
	/* All the page table structures
	 * in a static array of pointers */
	struct imgmmu_pagetab **page_map;

	/*
	 * Functions to use to manage pages allocation,
	 * liberation and writing
	 */
	struct imgmmu_info config;

	/* number of mapping using this directory (PDEs)*/
	uint32_t nmap;
};

/* Mapping a virtual address range and some entries in a directory */
struct imgmmu_dirmap {
	struct list_head entry; /* Entry in <imgmmu_map:dir_maps> */
	/* associated directory */
	struct imgmmu_dir *dir;
	/*
	 * device virtual address range associated with this mapping - not
	 * owned by the mapping
	 */
	struct imgmmu_halloc virt_mem;

	/* flag used when allocating */
	unsigned int flags;
	/* number of entries mapped (PTEs) */
	uint32_t entries;
};

/* Mapping a virtual address and catalogue entries */
struct imgmmu_map {
	struct list_head dir_maps; /* contains <struct imgmmu_dirmap> */
	/*
	 * device virtual address associated with this mapping - not
	 * owned by the mapping
	 */
	struct imgmmu_halloc virt_mem;

	/* number of entries mapped (PCEs) */
	uint32_t entries;
};

/* One page Table of the directory */
struct imgmmu_pagetab {
	/* associated directory */
	struct imgmmu_dir *dir;
	/* page used to store this mapping in the MMU */
	struct imgmmu_page *page;

	/* number of valid entries in this page */
	uint32_t valid_entries;
};

/*
 * local functions
 */

#define MMU_LOG_TMP 256

/*
 *  Write to stderr (or KRN_ERR if in kernel module)
 */
void _mmu_log(int err, const char *function, uint32_t line,
	      const char *format, ...)
{
	char _message_[MMU_LOG_TMP];
	va_list args;

	va_start(args, format);

	vsprintf(_message_, format, args);

	va_end(args);

	if (err)
		pr_err("ERROR: %s:%u %s", function, line, _message_);
	else
		/* info, debug, ... */
		pr_debug("%s:%u %s", function, line, _message_);
}

/*
 * Destruction of a PageTable
 *
 * warning: Does not verify if pages are still valid or not
 */
static void mmu_pagetab_destroy(struct imgmmu_pagetab *pagetab)
{
	WARN_ON(pagetab->dir == NULL);
	/* the function should be configured */
	WARN_ON(pagetab->dir->config.page_free == NULL);
	/* the physical page should still be here */
	WARN_ON(pagetab->page == NULL);

	mmu_log_dbg("Destroy page table (phys addr 0x%x)\n",
		     pagetab->page->phys_addr);
	pagetab->dir->config.page_free(pagetab->page);
	pagetab->page = NULL;

	kfree(pagetab);
}

/*
 * Extact the catalogue index from a virtual address
 */
static uint16_t mmu_cat_entry(uint64_t vaddr)
{
	return (vaddr & VIRT_CAT_IDX_MASK) >>
			  IMGMMU_CAT_SHIFT;
}

/*
 * Extact the directory index from a virtual address
 */
static uint16_t mmu_dir_entry(uint64_t vaddr)
{
	return (vaddr & VIRT_DIR_IDX_MASK) >>
			  IMGMMU_DIR_SHIFT;
}

/*
 * Extract the page table index from a virtual address
 */
static uint16_t mmu_page_entry(uint64_t vaddr)
{
	return (vaddr & VIRT_PAGE_TBL_MASK())
			  >> g_mmupageshift;
}

/*
 * Create a page table
 *
 * A pointer to the new page table structure and 0 in res
 * return: NULL in case of error and a value in res
 *  -ENOMEM if internal structure allocation failed
 *  -EFAULT if physical page allocation failed
 */
static struct imgmmu_pagetab *mmu_pagetab_create(struct imgmmu_dir *dir,
		int *res)
{
	struct imgmmu_pagetab *tab = NULL;
	uint32_t i;

	WARN_ON(res == NULL);
	WARN_ON(dir == NULL);
	WARN_ON(dir->config.page_alloc == NULL);
	WARN_ON(dir->config.page_write == NULL);

	tab = kzalloc(sizeof(struct imgmmu_pagetab), GFP_KERNEL);
	if (tab == NULL) {
		mmu_log_err("failed to allocate %zu bytes for page table\n",
			     sizeof(struct imgmmu_pagetab));
		*res = -ENOMEM;
		return NULL;
	}

	tab->dir = dir;

	tab->page = dir->config.page_alloc(dir->config.ctx, IMGMMU_PTYPE_PT);
	if (tab->page == NULL) {
		mmu_log_err("failed to allocate Page Table physical page\n");
		kfree(tab);
		*res = -EFAULT;
		return NULL;
	}
	mmu_log_dbg("Create page table (phys addr 0x%x 0x%x)\n",
		     tab->page->phys_addr, tab->page->cpu_addr);

	/* invalidate all pages */
	for (i = 0; i < IMGMMU_N_PAGE; i++)
		dir->config.page_write(tab->page, i, 0, MMU_FLAG_INVALID, NULL);

	/*
	 * when non-UMA need to update the device
	 * memory after setting it to 0
	 */
	if (dir->config.page_update != NULL)
		dir->config.page_update(tab->page);

	*res = 0;
	return tab;
}

/* Sets mapped pages as invalid with given pagetab entry and range*/
static void mmu_pagetab_rollback(struct imgmmu_dir *dir,
		unsigned int page_offs, unsigned int dir_offs,
		uint32_t entry, uint32_t from, uint32_t to)
{
	while (entry > 1) {
		if (from == 0) {
			entry--;
			from = to;
		}
		from--;

		if (page_offs == 0) {
			/* -1 is done just after */
			page_offs = IMGMMU_N_PAGE;
			WARN_ON(dir_offs == 0);
			dir_offs--;
		}

		page_offs--;

		/* it should have been used before */
		WARN_ON(dir->page_map[dir_offs] == NULL);
		dir->config.page_write(
			dir->page_map[dir_offs]->page,
			page_offs, 0, MMU_FLAG_INVALID, NULL);
		dir->page_map[dir_offs]->valid_entries--;
	}
}

/*-----------------------------------------------------------------------------
 * End of the IMGMMU_lib_int module
 *---------------------------------------------------------------------------*/

/*
 * public functions already have a group in mmu.h
 */

static size_t g_mmupagesize = IMGMMU_PAGE_SIZE;

size_t imgmmu_get_page_size(void)
{
	return g_mmupagesize;
}

size_t imgmmu_get_entry_shift(unsigned char type)
{
	if (type == IMGMMU_PTYPE_PT)
		return g_mmupageshift;
	else if (type == IMGMMU_PTYPE_PD)
		return IMGMMU_DIR_SHIFT;
	else if (type == IMGMMU_PTYPE_PC)
		return IMGMMU_CAT_SHIFT;
	else
		return 0;
}

int imgmmu_set_page_size(size_t pagesize)
{
	if (pagesize > imgmmu_get_cpu_page_size()) {
		mmu_log_dbg("MMU page size: %zu is bigger than CPU page size (%zu)\
				and will only work with physically contiguous memory!\n",
			     pagesize, imgmmu_get_cpu_page_size());
	}
	// get_order uses CPU page size as a base
	g_mmupageshift = IMGMMU_PAGE_SHIFT + get_order(pagesize);

	g_mmupagesize = pagesize;

	return 0;
}

size_t imgmmu_get_phys_size(void)
{
	return IMGMMU_PHYS_SIZE;
}

size_t imgmmu_get_virt_size(void)
{
	return IMGMMU_VIRT_SIZE;
}

static size_t g_cpupagesize = PAGE_SIZE;

size_t imgmmu_get_cpu_page_size(void)
{
	return g_cpupagesize;
}

int imgmmu_set_cpu_page_size(size_t pagesize)
{
	if (pagesize != PAGE_SIZE) {
		mmu_log_err("trying to change CPU page size from %zu to %zu\n",
			     PAGE_SIZE, pagesize);
		return -EFAULT;
	}
	return 0;
}

/* Proper directory will be populated on the first mapping request */
static struct imgmmu_dir *mmu_dir_create(const struct imgmmu_info *info,
			int *res)
{
	struct imgmmu_dir *dir = NULL;
	uint32_t i;

	WARN_ON(res == NULL);

	/* invalid information in the directory config:
	   - invalid page allocator and dealloc (page write can be NULL)
	   - invalid virtual address representation
	   - invalid page size
	   - invalid MMU size */
	if (info == NULL || info->page_alloc == NULL ||
	    info->page_free == NULL) {
		mmu_log_err("invalid MMU configuration\n");
		*res = -EINVAL;
		return NULL;
	}

	dir = kzalloc(sizeof(struct imgmmu_dir), GFP_KERNEL);
	if (dir == NULL) {
		mmu_log_err("failed to allocate %zu bytes for directory\n",
			     sizeof(struct imgmmu_dir));
		*res = -ENOMEM;
		return NULL;
	}

	dir->page_map = kzalloc(
			IMGMMU_N_TABLE * sizeof(struct imgmmu_pagetab *),
			GFP_KERNEL);
	if (dir->page_map == NULL) {
		kfree(dir);
		mmu_log_err("failed to allocate %zu bytes for directory\n",
			     IMGMMU_N_TABLE * sizeof(struct imgmmu_pagetab *));
		*res = -ENOMEM;
		return NULL;
	}

	memcpy(&dir->config, info, sizeof(struct imgmmu_info));
	if (info->page_write == NULL ||
			info->page_read == NULL) {
		mmu_log_err("wrong configuration!\n");
		kfree(dir->page_map);
		kfree(dir);
		*res = -EFAULT;
		return NULL;
	}

	dir->page = info->page_alloc(info->ctx, IMGMMU_PTYPE_PD);
	if (dir->page == NULL) {
		mmu_log_err("failed to allocate directory physical page\n");
		kfree(dir->page_map);
		kfree(dir);
		*res = -EFAULT;
		return NULL;
	}

	mmu_log_dbg("create MMU directory (phys page 0x%x 0x%x)\n",
		     dir->page->phys_addr, dir->page->cpu_addr);
	/* now we have a valid imgmmu_dir structure */

	/* invalidate all entries */
	for (i = 0; i < IMGMMU_N_TABLE; i++)
		dir->config.page_write(dir->page, i, 0, MMU_FLAG_INVALID, NULL);

	/* when non-UMA need to update the device memory */
	if (dir->config.page_update != NULL)
		dir->config.page_update(dir->page);

	*res = 0;
	return dir;
}

struct imgmmu_cat *imgmmu_cat_create(const struct imgmmu_info *info,
			int *res)
{
	struct imgmmu_cat *cat = NULL;
	uint32_t i;

	WARN_ON(res == NULL);

	/* invalid information in the directory config:
	   - invalid page allocator and dealloc (page write can be NULL)
	 */
	if (info == NULL || info->page_alloc == NULL ||
	    info->page_free == NULL) {
		mmu_log_err("invalid MMU configuration\n");
		*res = -EINVAL;
		return NULL;
	}

	cat = kzalloc(sizeof(struct imgmmu_cat), GFP_KERNEL);
	if (cat == NULL) {
		mmu_log_err("failed to allocate %zu bytes for catalogue\n",
			     sizeof(struct imgmmu_cat));
		*res = -ENOMEM;
		return NULL;
	}

	cat->dir_map = kzalloc(
			IMGMMU_N_DIR * sizeof(struct imgmmu_dir *),
			GFP_KERNEL);
	if (cat->dir_map == NULL) {
		kfree(cat);
		mmu_log_err("failed to allocate %zu bytes for catalogue\n",
			     IMGMMU_N_DIR * sizeof(struct imgmmu_dir *));
		*res = -ENOMEM;
		return NULL;
	}

	memcpy(&cat->config, info, sizeof(struct imgmmu_info));
	if (info->page_write == NULL ||
			info->page_read == NULL) {
		mmu_log_err("wrong configuration!\n");
		kfree(cat->dir_map);
		kfree(cat);
		*res = -EFAULT;
		return NULL;
	}

	cat->page = info->page_alloc(info->ctx, IMGMMU_PTYPE_PC);
	if (cat->page == NULL) {
		mmu_log_err("failed to allocate catalogue physical page\n");
		kfree(cat->dir_map);
		kfree(cat);
		*res = -EFAULT;
		return NULL;
	}

	mmu_log_dbg("create MMU catalogue (phys page 0x%x 0x%x)\n",
		     cat->page->phys_addr, cat->page->cpu_addr);
	/* now we have a valid imgmmu_cat structure */

	/* invalidate all entries */
	for (i = 0; i < IMGMMU_N_DIR; i++)
		cat->config.page_write(cat->page, i, 0, MMU_FLAG_INVALID, NULL);

	/* when non-UMA need to update the device memory */
	if (cat->config.page_update != NULL)
		cat->config.page_update(cat->page);

	*res = 0;
	return cat;
}

static int mmu_dir_destroy(struct imgmmu_dir *dir)
{
	uint32_t i;

	if (dir == NULL) {
		/* could be an assert */
		mmu_log_err("dir is NULL\n");
		return -EINVAL;
	}

	if (dir->nmap > 0)
		/* mappings should have been destroyed! */
		mmu_log_err("directory still has %u mapping attached to it\n",
			     dir->nmap);

	WARN_ON(dir->config.page_free == NULL);
	WARN_ON(dir->page_map == NULL);

	mmu_log_dbg("destroy MMU dir (phys page 0x%x)\n",
		     dir->page->phys_addr);

	/* first we destroy the directory entry */
	dir->config.page_free(dir->page);
	dir->page = NULL;

	/* destroy every mapping that still exists */
	for (i = 0; i < IMGMMU_N_TABLE; i++)
		if (dir->page_map[i] != NULL) {
			mmu_pagetab_destroy(dir->page_map[i]);
			dir->page_map[i] = NULL;
		}

	kfree(dir->page_map);
	kfree(dir);
	return 0;
}

int imgmmu_cat_destroy(struct imgmmu_cat *cat)
{
	uint32_t i;

	if (cat == NULL) {
		/* could be an assert */
		mmu_log_err("cat is NULL\n");
		return -EINVAL;
	}

	if (cat->nmap > 0)
		/* mappings should have been destroyed! */
		mmu_log_err("catalogue still has %u mapping attached to it\n",
			     cat->nmap);

	WARN_ON(cat->config.page_free == NULL);
	WARN_ON(cat->dir_map == NULL);

	mmu_log_dbg("destroy MMU cat (phys page 0x%x)\n",
		     cat->page->phys_addr);

	/* first we destroy the catalogue entry */
	cat->config.page_free(cat->page);
	cat->page = NULL;

	/* destroy every mapping that still exists */
	for (i = 0; i < IMGMMU_N_DIR; i++)
		if (cat->dir_map[i] != NULL) {
			mmu_dir_destroy(cat->dir_map[i]);
			cat->dir_map[i] = NULL;
		}

	kfree(cat->dir_map);
	kfree(cat);
	return 0;
}

struct imgmmu_page *imgmmu_cat_get_page(struct imgmmu_cat *cat)
{
	WARN_ON(cat == NULL);

	return cat->page;
}

uint64_t imgmmu_cat_get_pte(struct imgmmu_cat *cat,
					     uint64_t vaddr)
{
	uint16_t cat_entry = 0;
	uint16_t dir_entry = 0;
	uint16_t tab_entry = 0;
	struct imgmmu_dir *dir;
	struct imgmmu_pagetab *tab;
	uint64_t addr;
	unsigned flags;

	if (vaddr & (imgmmu_get_page_size()-1))
		return (uint64_t)-1;

	WARN_ONCE(cat == NULL, "No MMU entries");
	if (cat == NULL || cat->config.page_read == NULL)
		return (uint64_t)-1;

	cat_entry = mmu_cat_entry(vaddr);
	dir_entry = mmu_dir_entry(vaddr);
	tab_entry = mmu_page_entry(vaddr);

	dir = cat->dir_map[cat_entry];
	if (dir == NULL || dir->page_map[dir_entry] == NULL)
		return (uint64_t)-1;

	addr = cat->config.page_read(
			cat->page, cat_entry, NULL, &flags);
	/* Check consistency of PCE */
	if (addr != dir->page->phys_addr) {
		mmu_log_err("PCE entry inconsistent!\n");
		return (uint64_t)-1;
	}

	tab = dir->page_map[dir_entry];
	if (tab == NULL || dir->page == NULL)
		return (uint64_t)-1;

	addr = dir->config.page_read(
			dir->page, dir_entry, NULL, &flags);
	/* Check consistency of PDE */
	if (addr != tab->page->phys_addr) {
		mmu_log_err("PDE entry inconsistent!\n");
		return (uint64_t)-1;
	}

	addr = dir->config.page_read(
			tab->page, tab_entry, NULL, &flags);

	return addr|flags;
}

uint64_t imgmmu_cat_override_phys_addr(struct imgmmu_cat *cat,
				  uint64_t vaddr, uint64_t new_phys_addr)
{
	uint32_t cat_entry = 0;
	uint32_t dir_entry = 0;
	uint32_t tab_entry = 0;
	struct imgmmu_dir *dir;
	unsigned flags = 0;

	WARN_ON(cat == NULL);
	if (cat->config.page_read == NULL)
		return (uint64_t)-1;

	if (cat->config.page_write == NULL)
		return (uint64_t)-1;

	cat_entry = mmu_cat_entry(vaddr);
	dir_entry = mmu_dir_entry(vaddr);
	tab_entry = mmu_page_entry(vaddr);

	dir = cat->dir_map[cat_entry];
	WARN_ON(dir == NULL);
	if (dir->page_map[dir_entry] == NULL)
		return (uint64_t)-1;

	(void)dir->config.page_read(
		dir->page_map[dir_entry]->page, tab_entry, NULL, &flags);

	if (!(flags & MMU_FLAG_VALID))
		return (uint64_t)-1;

	dir->config.page_write(
			dir->page_map[dir_entry]->page,
			tab_entry,
			new_phys_addr | (uint64_t)pte_cache_mode << MMU_PTE_AXCACHE_SHIFT,
			flags | IMGMMU_BYPASS_ADDR_TRANS, NULL);

	return 0;
}

static struct imgmmu_dirmap *mmu_dir_map(struct imgmmu_dir *dir,
				struct imgmmu_halloc *virt_mem,
				unsigned int map_flag,
				int(*phys_iter_next) (void *arg, uint64_t *next),
				void *phys_iter_arg,
				void *priv,
				int *res)
{
	unsigned int first_dir = 0, first_page = 0;
	unsigned int dir_offs = 0, page_offs = 0;
	uint32_t entries = 0;
	uint32_t i, d;
	const uint32_t duplicate = imgmmu_get_cpu_page_size() < imgmmu_get_page_size() ?
		1 : imgmmu_get_cpu_page_size() / imgmmu_get_page_size();
	int ret = 0;
	struct imgmmu_dirmap *map = NULL;

	/* in non UMA updates on pages needs to be done,
	 * store index of directory entry pages to update */
	uint32_t *pages_to_update;
	/* number of pages in pages_to_update
	 * (will be at least 1 for the first_page to update) */
	uint32_t num_pages_to_update = 0;
	/* to know if we also need to update the directory page
	 * (creation of new page) */
	bool dir_update = false;

	WARN_ON(res == NULL);
	WARN_ON(dir == NULL);
	WARN_ON(virt_mem == NULL);
	/* otherwise PAGE_SIZE and MMU page size are not set properly! */
	WARN_ON(duplicate == 0);

	entries = virt_mem->size / IMGMMU_GET_MAX_PAGE_SIZE();
	if (virt_mem->size % imgmmu_get_page_size() != 0 || entries == 0) {
		mmu_log_err("invalid allocation size\n");
		*res = -EINVAL;
		return NULL;
	}

	if ((map_flag & MMU_FLAG_VALID) != 0) {
		mmu_log_err("valid flag (0x%x) is set in the flags 0x%x\n",
			     MMU_FLAG_VALID, map_flag);
		*res = -EINVAL;
		return NULL;
	}

	/* has to be dynamically allocated because it is bigger than 1k
	 * (max stack in the kernel)
	 * IMGMMU_N_TABLE is 1024 for 4096B pages,
	 * that's a 4k allocation (1 page) */
	pages_to_update = kzalloc(IMGMMU_N_TABLE * sizeof(uint32_t), GFP_KERNEL);
	if (pages_to_update == NULL) {
		mmu_log_err("Failed to allocate the update index table (%zu Bytes)\n",
			     IMGMMU_N_TABLE * sizeof(uint32_t));
		*res = -ENOMEM;
		return NULL;
	}

	/* manage multiple page table mapping */

	first_dir = mmu_dir_entry(virt_mem->vaddr);
	first_page = mmu_page_entry(virt_mem->vaddr);

	WARN_ON(first_dir > IMGMMU_N_TABLE);
	WARN_ON(first_page > IMGMMU_N_PAGE);

	/* verify that the pages that should be used are available */
	dir_offs = first_dir;
	page_offs = first_page;

	/*
	 * loop over the number of entries given by CPU allocator
	 * but CPU page size can be > than MMU page size therefore
	 * it may need to "duplicate" entries  by creating a fake
	 * physical address
	 */
	for (i = 0; i < entries * duplicate; i++) {
		if (page_offs >= IMGMMU_N_PAGE) {
			WARN_ON(dir_offs > IMGMMU_N_TABLE);
			dir_offs++;	/* move to next directory */
			WARN_ON(dir_offs > IMGMMU_N_TABLE);
			page_offs = 0;	/* using its first page */
		}

		/* if dir->page_map[dir_offs] == NULL not yet allocated it
		   means all entries are available */
		if (pte_rb_check &&
				dir->page_map[dir_offs] != NULL) {
			/*
			 * inside a pagetable
			 * verify that the required offset is invalid
			 */
			unsigned flags = 0;
			(void)dir->config.page_read(
					dir->page_map[dir_offs]->page, page_offs, priv, &flags);

			if (flags & MMU_FLAG_VALID) {
				mmu_log_err("PTE is currently in use\n");
				ret = -EBUSY;
				break;
			}
		}
		/* PageTable struct exists */
		page_offs++;
	} /* for all needed entries */

	/* it means one entry was not invalid or not enough page were given */
	if (ret != 0) {
		/* message already printed */
		*res = ret;
		kfree(pages_to_update);
		return NULL;
	}

	map = kzalloc(sizeof(struct imgmmu_dirmap), GFP_KERNEL);
	if (map == NULL) {
		mmu_log_err("failed to allocate %zu bytes for mapping structure\n",
				sizeof(struct imgmmu_dirmap));
		*res = -ENOMEM;
		kfree(pages_to_update);
		return NULL;
	}
	map->dir = dir;
	map->virt_mem = *virt_mem;
	memcpy(&(map->virt_mem), virt_mem, sizeof(struct imgmmu_halloc));
	map->flags = map_flag;

	/* we now know that all pages are available */
	dir_offs = first_dir;
	page_offs = first_page;

	pages_to_update[num_pages_to_update] = first_dir;
	num_pages_to_update++;

	for (i = 0; i < entries; i++) {
		uint64_t curr_phy_addr;

		if (phys_iter_next(phys_iter_arg, &curr_phy_addr) != 0) {
			mmu_log_err("not enough entries in physical address array/sg list!\n");
			kfree(map);
			kfree(pages_to_update);
			*res = -EFAULT;
			return NULL;
		}
		if ((curr_phy_addr & (imgmmu_get_page_size()-1)) != 0) {
			mmu_log_err("current physical address: %llx "
					"is not aligned to MMU page size: %zu!\n",
					curr_phy_addr, imgmmu_get_page_size());
			kfree(map);
			kfree(pages_to_update);
			*res = -EFAULT;
			return NULL;
		}
		for (d = 0; d < duplicate; d++) {
			if (page_offs >= IMGMMU_N_PAGE) {
				dir_offs++;	/* move to next directory */
				page_offs = 0;	/* using its first page */

				pages_to_update[num_pages_to_update] = dir_offs;
				num_pages_to_update++;
			}

			/* this page table object does not exists, create it */
			if (dir->page_map[dir_offs] == NULL) {
				struct imgmmu_pagetab *pagetab;

				pagetab = mmu_pagetab_create(dir, res);
				dir->page_map[dir_offs] = pagetab;

				if (dir->page_map[dir_offs] == NULL) {
					mmu_log_err("failed to create a page table\n");

					/* invalidate all already mapped pages
					 * do not destroy the created pages */
					mmu_pagetab_rollback(dir,
							page_offs,
							dir_offs,
							i,
							d,
							duplicate);

					kfree(map);
					kfree(pages_to_update);
					*res = -EFAULT;
					return NULL;
				}
				pagetab->page->virt_base = (dir->page->virt_base  &
						~(VIRT_PAGE_TBL_MASK())) +
						((1<<IMGMMU_DIR_SHIFT) * dir_offs);

				/*
				 * make this page table valid
				 * should be dir_offs
				 */
				dir->config.page_write(
					dir->page,
					dir_offs,
					pagetab->page->phys_addr,
					MMU_FLAG_VALID, NULL);
				dir_update = true;
			}

			if (pte_rb_check) {
				unsigned flags = 0;
				(void)dir->config.page_read(
						dir->page_map[dir_offs]->page, page_offs, priv, &flags);

				if (flags & MMU_FLAG_VALID) {
					mmu_log_err("PTE is currently in use (2)\n");
					kfree(map);
					kfree(pages_to_update);
					*res = -EFAULT;
					return NULL;
				}
			}
			/*
			 * map this particular page in the page table
			 * use d*(MMU page size) to add additional entries
			 * from the given  physical address with the correct
			 * offset for the MMU
			 */
			dir->config.page_write(
				dir->page_map[dir_offs]->page,
				page_offs,
				(curr_phy_addr + d * imgmmu_get_page_size()) |
					(uint64_t)pte_cache_mode << MMU_PTE_AXCACHE_SHIFT,
				map->flags | MMU_FLAG_VALID, priv);
			dir->page_map[dir_offs]->valid_entries++;

			if (pte_rb_check) {
				unsigned flags = 0;

				uint64_t phys = dir->config.page_read(
						dir->page_map[dir_offs]->page, page_offs, priv, &flags);

				if (flags != (map->flags | MMU_FLAG_VALID) ||
						(phys != (curr_phy_addr + d * imgmmu_get_page_size())) ) {
					mmu_log_err("PTE read back failed\n");
					kfree(map);
					kfree(pages_to_update);
					*res = -EFAULT;
					return NULL;
				}
			}

			page_offs++;
		} /* for duplicate */
	} /* for entries */

	map->entries = entries * duplicate;
	/* one more mapping is related to this directory */
	dir->nmap++;

	/* if non UMA we need to update device memory */
	if (dir->config.page_update != NULL) {
		while (num_pages_to_update > 0) {
			uint32_t idx = pages_to_update[num_pages_to_update - 1];
			dir->config.page_update(
				dir->page_map[idx]->page);
			num_pages_to_update--;
		}
		if (dir_update == true)
			dir->config.page_update(
				dir->page);
	}

	*res = 0;
	kfree(pages_to_update);
	return map;
}

/*
 * with physical address array
 */

struct linear_phys_iter {
	uint64_t *array;
	int idx;
};

static int linear_phys_iter_next(void *arg, uint64_t *next)
{
	struct linear_phys_iter *iter = arg;

	int advance = imgmmu_get_cpu_page_size() < imgmmu_get_page_size() ?
		imgmmu_get_page_size() / imgmmu_get_cpu_page_size() : 1;

	*next = iter->array[iter->idx];	/* boundary check? */
	iter->idx += advance;
	return 0;
}

struct imgmmu_map *imgmmu_cat_map_arr(struct imgmmu_cat *cat,
					uint64_t *phys_page_list,
					const struct imgmmu_halloc *virt_mem,
					unsigned int map_flag,
					void *priv,
					int *res)
{
	uint16_t idx;
	struct linear_phys_iter arg = { phys_page_list, 0 };
	struct imgmmu_map *map = NULL;
	struct imgmmu_dirmap *dir_map = NULL;
	struct imgmmu_halloc virt_mem_range;

	if (virt_mem->vaddr >> IMGMMU_VIRT_SIZE) {
		mmu_log_err("Virtual address beyond %u bits!\n",
				IMGMMU_VIRT_SIZE);
		*res = -EFAULT;
		return NULL;
	}

	if (virt_mem->vaddr & (imgmmu_get_page_size()-1)) {
		mmu_log_err("Virtual address not aligned to %zu!\n",
				imgmmu_get_page_size());
		*res = -EFAULT;
		return NULL;
	}

	map = kzalloc(sizeof(struct imgmmu_map), GFP_KERNEL);
	if (map == NULL) {
		mmu_log_err("failed to allocate %zu bytes for mapping structure\n",
				sizeof(struct imgmmu_map));
		*res = -ENOMEM;
		return NULL;
	}
	INIT_LIST_HEAD(&map->dir_maps);
	/* Store the whole virtual address space for this mapping */
	map->virt_mem = *virt_mem;
	/* Set starting address & total size */
	virt_mem_range.vaddr = virt_mem->vaddr;
	virt_mem_range.size = virt_mem->size;

	do {
		struct imgmmu_dir *dir;

		/* Determine catalogue entry (PCE-> PD) */
		idx = mmu_cat_entry(virt_mem_range.vaddr);
		dir = cat->dir_map[idx];
		if (dir == NULL) {
			dir = mmu_dir_create(
					&cat->config, res);
			if (*res != 0)
				goto error;

			dir->page->virt_base = virt_mem_range.vaddr &
						~(VIRT_DIR_IDX_MASK | VIRT_PAGE_TBL_MASK());

			dir->cat = cat;
			WARN_ON(cat->dir_map[idx] != NULL);
			cat->dir_map[idx] = dir;
			/* Mark PCE valid and store PD address */
			cat->config.page_write(
				cat->page,
				idx, dir->page->phys_addr,
				MMU_FLAG_VALID, NULL);
			if (cat->config.page_update != NULL)
				cat->config.page_update(cat->page);
			cat->nmap++;
		}

		/* Need to handle buffer spanning across the GB boundaries */
		if (((virt_mem_range.vaddr % (1ULL<<IMGMMU_CAT_SHIFT)) +
		  virt_mem_range.size) >= (1ULL<<IMGMMU_CAT_SHIFT))
			virt_mem_range.size = (1ULL<<IMGMMU_CAT_SHIFT) -
				(virt_mem_range.vaddr % (1ULL<<IMGMMU_CAT_SHIFT));

		dir_map = mmu_dir_map(dir, &virt_mem_range, map_flag,
			   linear_phys_iter_next, &arg, priv, res);

		if (dir_map) {
			/* Update starting address */
			virt_mem_range.vaddr += virt_mem_range.size;
			/* and bytes left ... */
			virt_mem_range.size =  (virt_mem->vaddr + virt_mem->size) -
					virt_mem_range.vaddr;

			list_add(&dir_map->entry, &map->dir_maps);
		}

	} while(dir_map && *res == 0 && virt_mem_range.size);

	if (dir_map)
		/* If last dir mapping succeeded,
		 * return overlay container mapping structure */
		return map;
	else
error:
		imgmmu_cat_unmap(map);
		return NULL;
}

/*
 * with sg
 */

struct sg_phys_iter {
	struct scatterlist *sgl;
	unsigned int offset;
	bool use_sg_dma;
};

static int sg_phys_iter_next(void *arg, uint64_t *next)
{
	struct sg_phys_iter *iter = arg;
	phys_addr_t phys;
	unsigned int len;

	if (!iter->sgl)
		return -EFAULT;

	if (iter->use_sg_dma) {
		if (sg_dma_address(iter->sgl) == ~(dma_addr_t)0 ||
				!sg_dma_len(iter->sgl))
			return -EFAULT;

		phys = sg_dma_address(iter->sgl);
		len = sg_dma_len(iter->sgl);
	} else {
		phys = sg_phys(iter->sgl);
		len = iter->sgl->length;
	}

	*next = phys + iter->offset;
	iter->offset += IMGMMU_GET_MAX_PAGE_SIZE();

	if (iter->offset >= len) {
		int advance = iter->offset/len;
		while (iter->sgl) {
			iter->sgl = sg_next(iter->sgl);
			advance--;
			if (!advance)
				break;
		}
		iter->offset = 0;
	}

	return 0;
}

struct imgmmu_map *imgmmu_cat_map_sg(
	struct imgmmu_cat *cat,
	struct scatterlist *phys_page_sg,
	bool use_sg_dma,
	const struct imgmmu_halloc *virt_mem,
	unsigned int map_flag,
	void *priv,
	int *res)
{
	uint16_t idx;
	struct sg_phys_iter arg = { phys_page_sg, 0, use_sg_dma};
	struct imgmmu_map *map = NULL;
	struct imgmmu_dirmap *dir_map = NULL;
	struct imgmmu_halloc virt_mem_range;

	if (virt_mem->vaddr >> IMGMMU_VIRT_SIZE) {
		mmu_log_err("Virtual address beyond %u bits!\n",
				IMGMMU_VIRT_SIZE);
		*res = -EFAULT;
		return NULL;
	}
	if (virt_mem->vaddr & (imgmmu_get_page_size()-1)) {
		mmu_log_err("Virtual address not aligned to %zu!\n",
				imgmmu_get_page_size());
		*res = -EFAULT;
		return NULL;
	}

	map = kzalloc(sizeof(struct imgmmu_map), GFP_KERNEL);
	if (map == NULL) {
		mmu_log_err("failed to allocate %zu bytes for mapping structure\n",
				sizeof(struct imgmmu_map));
		*res = -ENOMEM;
		return NULL;
	}
	INIT_LIST_HEAD(&map->dir_maps);
	/* Store the whole virtual address space for this mapping */
	map->virt_mem = *virt_mem;
	/* Set starting address & total size */
	virt_mem_range.vaddr = virt_mem->vaddr;
	virt_mem_range.size = virt_mem->size;

	do {
		struct imgmmu_dir *dir;

		/* Determine catalogue entry (PCE-> PD) */
		idx = mmu_cat_entry(virt_mem_range.vaddr);
		dir = cat->dir_map[idx];
		if (dir == NULL) {
			dir = mmu_dir_create(
					&cat->config, res);
			if (*res != 0)
				goto error;

			dir->page->virt_base = virt_mem_range.vaddr &
						~(VIRT_DIR_IDX_MASK | VIRT_PAGE_TBL_MASK());

			dir->cat = cat;
			WARN_ON(cat->dir_map[idx] != NULL);
			cat->dir_map[idx] = dir;
			/* Mark PCE valid and store PD address */
			cat->config.page_write(
				cat->page,
				idx, dir->page->phys_addr,
				MMU_FLAG_VALID, NULL);
			if (cat->config.page_update != NULL)
				cat->config.page_update(cat->page);
			cat->nmap++;
		}

		/* Need to handle buffer spanning across the GB boundaries */
		if (((virt_mem_range.vaddr % (1ULL<<IMGMMU_CAT_SHIFT)) +
		  virt_mem_range.size) >= (1ULL<<IMGMMU_CAT_SHIFT))
			virt_mem_range.size = (1ULL<<IMGMMU_CAT_SHIFT) -
				(virt_mem_range.vaddr % (1ULL<<IMGMMU_CAT_SHIFT));

		dir_map = mmu_dir_map(dir, &virt_mem_range, map_flag,
				   sg_phys_iter_next, &arg, priv, res);

		if (dir_map) {
			/* Update starting address */
			virt_mem_range.vaddr += virt_mem_range.size;
			/* and bytes left ... */
			virt_mem_range.size = (virt_mem->vaddr + virt_mem->size) -
				virt_mem_range.vaddr;

			list_add(&dir_map->entry, &map->dir_maps);
		}

	} while(dir_map && *res == 0 && virt_mem_range.size);

	if (dir_map)
		/* If last dir mapping succeeded,
		 * return overlay container mapping structure */
		return map;
	else
error:
		imgmmu_cat_unmap(map);
		return NULL;
}

static int mmu_dir_unmap(struct imgmmu_dirmap *map)
{
	unsigned int first_dir = 0, first_page = 0;
	unsigned int dir_offs = 0, page_offs = 0;
	uint32_t i;
	struct imgmmu_dir *dir = NULL;

	/* in non UMA updates on pages needs to be done
	 * store index of directory entry pages to update */
	uint32_t *pages_to_update;
	uint32_t num_pages_to_update = 0;

	WARN_ON(map == NULL);
	WARN_ON(map->entries == 0);
	WARN_ON(map->dir == NULL);

	dir = map->dir;

	/* has to be dynamically allocated because
	 * it is bigger than 1k (max stack in the kernel) */
	pages_to_update = kzalloc(IMGMMU_N_TABLE * sizeof(uint32_t), GFP_KERNEL);
	if (pages_to_update == NULL) {
		mmu_log_err("Failed to allocate the update index table (%zu Bytes)\n",
			     IMGMMU_N_TABLE * sizeof(uint32_t));
		kfree(map);
		return -ENOMEM;
	}

	first_dir = mmu_dir_entry(map->virt_mem.vaddr);
	first_page = mmu_page_entry(map->virt_mem.vaddr);

	/* verify that the pages that should be used are available */
	dir_offs = first_dir;
	page_offs = first_page;

	pages_to_update[num_pages_to_update] = first_dir;
	num_pages_to_update++;

	for (i = 0; i < map->entries; i++) {
		if (page_offs >= IMGMMU_N_PAGE) {
			dir_offs++;	/* move to next directory */
			page_offs = 0;	/* using its first page */

			pages_to_update[num_pages_to_update] = dir_offs;
			num_pages_to_update++;
		}

		/* this page table object does not exists something destroyed it
		 * while the mapping was supposed to use it */
		WARN_ON(dir->page_map[dir_offs] == NULL);

		dir->config.page_write(
			dir->page_map[dir_offs]->page,
			page_offs, 0,
			MMU_FLAG_INVALID, NULL);
		dir->page_map[dir_offs]->valid_entries--;

		page_offs++;
	}

	dir->nmap--;

	if (dir->config.page_update != NULL)
		while (num_pages_to_update > 0) {
			uint32_t idx = pages_to_update[num_pages_to_update - 1];
			dir->config.page_update(
				dir->page_map[idx]->page);
			num_pages_to_update--;
		}

	/* mapping does not own the given virtual address */
	kfree(map);
	kfree(pages_to_update);
	return 0;
}

int imgmmu_cat_unmap(struct imgmmu_map *map)
{
	WARN_ON(map == NULL);

	while (!list_empty(&map->dir_maps)) {
		struct imgmmu_dirmap *dir_map;
		struct imgmmu_cat *cat;
		struct imgmmu_dir *dir;
		uint16_t idx;

		dir_map = list_first_entry(&map->dir_maps,
				       struct imgmmu_dirmap, entry);
		list_del(&dir_map->entry);

		idx = mmu_cat_entry(dir_map->virt_mem.vaddr);
		dir = dir_map->dir;
		cat = dir->cat;
		WARN_ON(cat == NULL);
		/* This destroys the mapping */
		mmu_dir_unmap(dir_map);

		/* Check integrity */
		WARN_ON(dir != cat->dir_map[idx]);

		if (!dir->nmap) {
			mmu_dir_destroy(dir);
			WARN_ON(cat->dir_map[idx] == NULL);
			cat->dir_map[idx] = NULL;
			/* Mark PCE invalid */
			cat->config.page_write(
				cat->page,
				idx, 0,
				MMU_FLAG_INVALID, NULL);
			if (cat->config.page_update != NULL)
				cat->config.page_update(cat->page);

			cat->nmap--;
		}
	}

	kfree(map);
	return 0;
}

static uint32_t mmu_dir_clean(struct imgmmu_dir *dir)
{
	uint32_t i, removed = 0;

	WARN_ON(dir == NULL);
	WARN_ON(dir->config.page_write == NULL);

	for (i = 0; i < IMGMMU_N_TABLE; i++) {
		if (dir->page_map[i] != NULL &&
		    dir->page_map[i]->valid_entries == 0) {
			dir->config.page_write(
				dir->page,
				i, 0,
				MMU_FLAG_INVALID, NULL);

			mmu_pagetab_destroy(dir->page_map[i]);
			dir->page_map[i] = NULL;
			removed++;
		}
	}

	if (dir->config.page_update != NULL)
		dir->config.page_update(dir->page);

	return removed;
}

/* Not used */
uint32_t imgmmu_cat_clean(struct imgmmu_cat *cat)
{
	uint32_t i, removed = 0;

	WARN_ON(cat == NULL);
	WARN_ON(cat->config.page_write == NULL);

	for (i = 0; i < IMGMMU_N_DIR; i++) {
		if (cat->dir_map[i] != NULL) {
			mmu_dir_clean(cat->dir_map[i]);
			cat->dir_map[i] = NULL;
			removed++;
		}
	}

	if (cat->config.page_update != NULL)
		cat->config.page_update(cat->page);

	return removed;
}

uint64_t imgmmu_get_pte_cache_bits(uint64_t pte_entry)
{
	return pte_entry & MMU_PTE_AXCACHE_MASK;
}

u8 imgmmu_get_pte_parity_shift(void)
{
	return MMU_PTE_PARITY_SHIFT;
}

void imgmmu_set_pte_parity(uint64_t *pte_entry)
{
	*pte_entry &= ~(1ULL << imgmmu_get_pte_parity_shift());

	*pte_entry |= (1ULL << imgmmu_get_pte_parity_shift());
}
