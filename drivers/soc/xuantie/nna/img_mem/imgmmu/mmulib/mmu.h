/*!
 *****************************************************************************
 *
 * @File           mmu.h
 * @Description    MMU Library
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

#ifndef IMGMMU_MMU_H
#define IMGMMU_MMU_H

#include <linux/scatterlist.h>

/**
 * @defgroup IMGMMU_lib The MMU page table manager
 * @brief The Memory Mapping Unit page table manager library handles the memory
 * hierarchy for a multi-directory MMU.
 *
 * The library is composed of several elements:
 * @li the Page Table management, responsible for managing the device memory
 * used for the mapping. This requires some functions from the Allocator to
 * access the memory and a virtual address from a Heap.
 * @li the Heap interface, that is the SW heap API one can re-implement (or use
 * the provided TAL one). This is responsible for choosing a virtual address
 * for an allocation.
 * @li the Allocator, that is not implemented in this library, is responsible
 * for providing physical pages list (when mapping) and a few memory operation
 * functions (imgmmu_info).
 * An example TAL allocator is provided in this code and can be used when
 * running in full user-space with pdumps.
 *
 * Some pre-processor values can be defined to customise the MMU:
 * @li IMGMMU_PHYS_SIZE physical address size of the MMU in bits (default: 40)
 * - only used for the default memory write function
 * @li IMGMMU_PAGE_SIZE page size in bytes (default: 4096) - not used directly
 * in the MMU code, but the allocator should take it into account
 * If IMGMMU_PAGE_SIZE is defined the following HAVE TO be defined as well:
 * @li IMGMMU_PAGE_SHIFT as log2(IMGMMU_PAGE_SIZE) (default: 12) - used in
 * virtual address to determine the position of the page offset
 * @li IMGMMU_DIR_SHIFT as log2(IMGMMU_PAGE_SIZE)*2-2 (default: 21) - used in
 * virtual address to determine the position of the directory offset
 * @li IMGMMU_CAT_SHIFT as log2(IMGMMU_PAGE_SIZE)*3-6 (default: 30) - used in
 * virtual address to determine the position of the catalogue offset
 *
 * @{
 */
/*-----------------------------------------------------------------------------
 * Following elements are in the IMGMMU_lib documentation module
 *---------------------------------------------------------------------------*/

/**
 * @name MMU page table management
 * @brief The public functions to use to control the MMU.
 * @image html MMU_class.png "MMU structure organisation"
 * @{
 */
/*-----------------------------------------------------------------------------
 * Following elements are in the public functions
 *---------------------------------------------------------------------------*/

/** @brief Opaque type representing an MMU Catalogue page */
struct imgmmu_cat;
/** @brief Opaque type representing an MMU Mapping */
struct imgmmu_map;

struct imgmmu_page;
struct imgmmu_halloc;

/** @brief Define indicating the mmu page type */
#define IMGMMU_PTYPE_PC 0x1
#define IMGMMU_PTYPE_PD 0x2
#define IMGMMU_PTYPE_PT 0x3

/** @brief Bypass phys address translation when using page_write */
#define IMGMMU_BYPASS_ADDR_TRANS 0x80000000

/**
 * @brief Pointer to a function implemented by the used allocator to create 1
 * page table (used for the MMU mapping - catalogue page and mapping page)
 *
 * @param type of the mmu page (PC, PD, PT)
 *
 * This is not done internally to allow the usage of different allocators
 *
 * @return A populated imgmmu_page structure with the result of the page allocation
 * @return NULL if the allocation failed
 *
 * @see imgmmu_page_free to liberate the page
 */
typedef struct imgmmu_page *(*imgmmu_page_alloc) (void *, unsigned char type);
/**
 * @brief Pointer to a function to free the allocated page table used for MMU
 * mapping
 *
 * This is not done internally to allow the usage of different allocators
 *
 * @see imgmmu_page_alloc to allocate the page
 */
typedef void (*imgmmu_page_free) (struct imgmmu_page *);

/**
 * @brief Pointer to a function to update Device memory on non Unified Memory
 */
typedef void (*imgmmu_page_update) (struct imgmmu_page *);

/**
 * @brief Write to a device address.
 *
 * This is not done internally to allow debug operations such a pdumping to
 * occur
 *
 * This function should do all the shifting and masking needed for the used MMU
 *
 * @param page page to update - asserts it is not NULL
 * @param offset in entries (32b word)
 * @param write physical address to write
 * @param flags bottom part of the entry used as flags for the MMU (including
 * valid flag) or IMGMMU_BYPASS_ADDR_TRANS
 * @param priv private data passed with map call
 */
typedef void (*imgmmu_page_write) (struct imgmmu_page *page,
				     unsigned int offset, uint64_t write,
				     unsigned int flags, void *priv);

/**
 * @brief Reads a 32 word on a physical page
 *
 * This is used only when debug operations occures (e.g. access page table
 * entry that caused a page fault)
 *
 * @param page physical page - asserts it is not NULL
 * @param offset in entries (32b word)
 * @param priv private data passed with map call

 * @return physical address at given offset and flags
 */
typedef uint64_t(*imgmmu_page_read) (struct imgmmu_page *page,
					 unsigned int offset, void *priv,
					 unsigned int *flags);

/**
 * @brief Callbacks definition structure
 */
struct imgmmu_info {
	void *ctx;
    /** @brief allocate a physical page used in MMU mapping */
	imgmmu_page_alloc page_alloc;
    /** @brief liberate a physical page used in MMU mapping */
	imgmmu_page_free page_free;
    /**
     * @brief write a physical address onto a page - optional, if NULL internal
     * function is used
     *
     * The internal function assumes that IMGMMU_PHYS_SIZE is the MMU size.
     *
     * @note if NULL page_read is also set
     *
     * @warning The function assumes that the physical page memory is
     * accessible
     */
	imgmmu_page_write page_write;
    /**
     * @brief read a physical page offset - optional, if NULL access to page
     * table and catalogue entries is not supported
     *
     * @note If page_write and page_read are NULL then the internal
     * function is used.
     *
     * @warning The function assumes that the physical page memory is
     * accessible
     */
	imgmmu_page_read page_read;
    /**
     * @brief update a physical page on device if non UMA - optional, can be
     * NULL if update are not needed
     */
	imgmmu_page_update page_update;

};

/** @brief Page table entry - used when allocating the MMU pages */
struct imgmmu_page {
    /**
     * @note Use ui64 instead of uintptr_t to support extended physical address
     * on 32b OS
     */
	uint64_t phys_addr;
	uint64_t virt_base;
	uintptr_t cpu_addr;
};

/**
 * @brief Access the default specified page size of the MMU (in Bytes)
 */
size_t imgmmu_get_page_size(void);

/**
 * @brief Returns entry shift for given type
 */
size_t imgmmu_get_entry_shift(unsigned char type);

/**
 * @brief Change the MMU page size in runtime.
 */
int imgmmu_set_page_size(size_t pagesize);

/**
 * @brief Access the compilation specified physical size of the MMU (in bits)
 */
size_t imgmmu_get_phys_size(void);

/**
 * @brief Access the compilation specified virtual address size of the MMU
 * (in bits)
 */
size_t imgmmu_get_virt_size(void);

/**
 * @brief Access the CPU page size - similar to PAGE_SIZE macro in Linux
 *
 * Not directly using PAGE_SIZE because we need a run-time configuration of the
 * PAGE_SIZE when running against simulators and different projects define
 * PAGE_SIZE in different ways...
 *
 * The default size is using the PAGE_SIZE macro if defined (or 4kB if not
 * defined when running against simulators)
 */
size_t imgmmu_get_cpu_page_size(void);

/**
 * @brief Change run-time CPU page size
 *
 * @warning to use against simulators only! default of imgmmu_get_cpu_page_size()
 * is PAGE_SIZE otherwise!
 */
int imgmmu_set_cpu_page_size(size_t pagesize);

/**
 * @brief Create a catalogue entry based on a given catalogue configuration
 *
 * @warning Obviously creation of the catalogue allocates memory - do not call
 * while interrupts are disabled
 *
 * @param info is copied and not modified - contains the functions to
 * use to manage page table memory
 * @param res where to store the error code, should not be NULL (trapped by
 * assert)
 *
 * @return The opaque handle to the imgmmu_cat object and res to
 * 0
 * @return NULL in case of an error and res has the value:
 * @li -EINVAL if catConfig is NULL or does not
 * contain function pointers
 * @li -ENOMEM if an internal allocation failed
 * @li -EFAULT if the given imgmmu_page_alloc returned NULL
 */
struct imgmmu_cat *imgmmu_cat_create(
	const struct imgmmu_info *info,
	int *res);

/**
 * @brief Destroy the imgmmu_cat - assumes that the HW is not going to access
 * the memory any-more
 *
 * Does not invalidate any memory because it assumes that everything is not
 * used any-more
 */
int imgmmu_cat_destroy(struct imgmmu_cat *cat);

/**
 * @brief Get access to the page table structure used in the catalogue (to be
 * able to write it to registers)
 *
 * @param cat asserts if cat is NULL
 *
 * @return the page table structure used
 */
struct imgmmu_page *imgmmu_cat_get_page(struct imgmmu_cat *cat);

/**
 * @brief Returns the page table entry value associated with the virtual
 * address
 *
 * @return -1 if the Catalogue's page_read is NULL or if the associate page
 * table is invalid in the catalogue map
 *
 */
uint64_t imgmmu_cat_get_pte(
	struct imgmmu_cat *cat,
	uint64_t vaddr);
	
/**
 * @brief Overrides the physical address associated with the virtual address
 *
 * @return -1 if the Catalogue's page_read is NULL or if the associate page
 * table is invalid in the catalogue map
 *
 */
uint64_t imgmmu_cat_override_phys_addr(struct imgmmu_cat *cat,
                 uint64_t vaddr, uint64_t new_phys_addr);
/**
 * @brief Create a PageTable mapping for a list of physical pages and device
 * virtual address
 *
 * @warning Mapping can cause memory allocation (missing pages) - do not call
 * while interrupts are disabled
 *
 * @param cat catalogue to use for the mapping
 * @param phys_page_list sorted array of physical addresses (ascending order).
 * The number of elements is virt_mem->size/MMU_PAGE_SIZE
 * @note This array can potentially be big, the caller may need to use vmalloc
 * if running the linux kernel (e.g. mapping a 1080p NV12 is 760 entries, 6080
 * Bytes - 2 CPU pages needed, fine with kmalloc; 4k NV12 is 3038 entries,
 * 24304 Bytes - 6 CPU pages needed, kmalloc would try to find 8 contiguous
 * pages which may be problematic if memory is fragmented)
 * @param virt_mem associated device virtual address. Given structure is
 * copied
 * @param map_flags flags to apply on the page (typically 0x2 for Write Only,
 * 0x4 for Read Only) - the flag should not set bit 1 as 0x1 is the valid flag.
 * @param priv private data to be passed in callback interface
 * @param res where to store the error code, should not be NULL
 *
 * @return The opaque handle to the imgmmu_map object and res to
 * 0
 * @return NULL in case of an error an res has the value:
 * @li -EINVAL if the allocation size is not a multiple of
 * IMGMMU_PAGE_SIZE,
 *     if the given list of page table is too long or not long enough for the
 * mapping or
 *     if the give flags set the invalid bit
 * @li -EBUSY if the virtual memory is already mapped
 * @li -ENOMEM if an internal allocation failed
 * @li -EFAULT if a page creation failed
 */
struct imgmmu_map *imgmmu_cat_map_arr(
	struct imgmmu_cat *cat,
	uint64_t  *phys_page_list,
	const struct imgmmu_halloc *virt_mem,
	unsigned int map_flags,
	void *priv,
	int  *res);

struct imgmmu_map *imgmmu_cat_map_sg(
	struct imgmmu_cat *cat,
	struct scatterlist *phys_page_sg,
	bool use_sg_dma,
	const struct imgmmu_halloc *virt_mem,
	unsigned int map_flags,
	void *priv,
	int *res);

/**
 * @brief Un-map the mapped pages (invalidate their entries) and destroy the
 * mapping object
 *
 * This does not destroy the created Page Table (even if they are becoming
 * un-used) and does not change the Catalogue valid bits.
 *
 * @return 0
 */
int imgmmu_cat_unmap(struct imgmmu_map *map);

/**
 * @brief Remove the internal Page Table structures and physical pages that
 * have no mapping attached to them
 *
 * @note This function does not have to be used, but can be used to clean some
 * un-used memory out and clean the Catalogue valid bits.
 *
 * @return The number of clean catalogue entries
 */
uint32_t imgmmu_cat_clean(struct imgmmu_cat *cat);

/**
 * @brief Get cache bits for PTE entry
 *
 * @return Cache bits
 */
uint64_t imgmmu_get_pte_cache_bits(uint64_t pte_entry);

/**
 * @brief Get parity bit shift of PTE entry
 *
 * @return Parity bit shift
 */
u8 imgmmu_get_pte_parity_shift(void);

/**
 * @brief Set parity for PTE entry
 *
 * @return Entry with applied parity
 */
void imgmmu_set_pte_parity(uint64_t *pte_entry);

#define IMGMMU_GET_MAX_PAGE_SIZE() (max(imgmmu_get_page_size(), imgmmu_get_cpu_page_size()))

/**
 * @}
 */
/*-----------------------------------------------------------------------------
 * End of the public functions
 *---------------------------------------------------------------------------*/

/**
 * @}
 */
/*-----------------------------------------------------------------------------
 * End of the IMGMMU_lib documentation module
 *---------------------------------------------------------------------------*/

#endif /* IMGMMU_MMU_H */
