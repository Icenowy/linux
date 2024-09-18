/*!
 *****************************************************************************
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

#ifndef IMG_MEM_MAN_H
#define IMG_MEM_MAN_H

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#define KERNEL_DMA_FENCE_SUPPORT
#endif

#include <linux/mm.h>
#include <linux/types.h>
#include <linux/device.h>
#ifdef KERNEL_DMA_FENCE_SUPPORT
#include <linux/dma-fence.h>
#endif

#include "uapi/img_mem_man.h"

/* Defines allocation order range for platforms
 * that do not explicitly defined it.
 * NOTE: applicable for unified heap type only */
#define IMG_MIN_ALLOC_ORDER_DEFAULT 0
#define IMG_MAX_ALLOC_ORDER_DEFAULT 0

/* Page catalogue address shift */
#define IMG_MMU_PC_ADDR_SHIFT 12

/* MMUv3 PTE entry flags */
enum {
	IMG_MMU_PTE_FLAG_NONE = 0x0,
	IMG_MMU_PTE_FLAG_VALID = 0x1,
	IMG_MMU_PTE_FLAG_READ_ONLY = 0x2,
	IMG_MMU_PTE_FLAG_CACHE_COHERENCY = 0x4,
};

/* All level entries flags are stored under 4lsb bits */
#define IMG_MMU_ENTRY_FLAGS_MASK 0xf

/* Each entry can store 40bit physical address */
#define IMG_MMU_PHY_ADDR_MASK ((1ULL<<40)-1)

struct mmu_config {
	uint32_t addr_width; /* physical */
	bool bypass_hw; /* MMU bypass mode */
	size_t bypass_offset; /* Optional offset in physical space for MMU bypass mode */
	bool use_pte_parity; /* enables parity calculation for PTEs */
	/* memory attributes to be used when allocating mmu pages */
	enum img_mem_attr alloc_attr;
	int page_size;
};

union heap_options {
	struct {
		gfp_t gfp_type; /* pool and flags for buffer allocations */
		int min_order;  /* minimum page allocation order */
		int max_order;  /* maximum page allocation order */
	} unified;
#ifdef CONFIG_ION
	struct {
		struct ion_client *client; /* must be provided by platform */
	} ion;
#endif
#ifdef CONFIG_GENERIC_ALLOCATOR
	struct {
		void *kptr; /* static pointer to kernel mapping of memory */
		/* Optional hooks to obtain kernel mapping dynamically */
		void* (*get_kptr)(
				phys_addr_t addr,
				size_t size,
				enum img_mem_attr mattr);
		int (*put_kptr)(void *);
		phys_addr_t phys; /* physical address start of memory */
		size_t size; /* size of memory */
		unsigned long offs; /* optional offset of the start
							of memory as seen from device,
							zero by default */
		int pool_order;  /* allocation order */
	} carveout;
#endif
	struct {
		bool use_sg_dma;  /* Forces sg_dma physical address instead of CPU physical address*/
	} dmabuf;
	struct {
		gfp_t gfp_flags; /* for buffer allocations */
	} coherent;
	struct {
		phys_addr_t phys; /* physical address start of memory */
		size_t size; /* size of memory */
		enum img_mem_heap_attrs hattr; /* User attributes */
	} ocm;
};

struct heap_config {
	enum img_mem_heap_type type;
	union heap_options options;
	/* (optional) functions to convert a physical address as seen from
		 the CPU to the physical address as seen from the vha device and
		 vice versa. When not implemented,
		 it is assumed that physical addresses are the
		 same regardless of viewpoint */
	phys_addr_t (*to_dev_addr)(union heap_options *opts, phys_addr_t addr);
	phys_addr_t (*to_host_addr)(union heap_options *opts, phys_addr_t addr);
	/* Cache attribute,
	 * could be platform specific if provided - overwrites the global cache policy */
	enum img_mem_attr cache_attr;
};

enum img_mmu_callback_type {
	IMG_MMU_CALLBACK_MAP = 1,
	IMG_MMU_CALLBACK_UNMAP,
};

struct mem_ctx;
struct mmu_ctx;

int img_mem_add_heap(const struct heap_config *heap_cfg, int *heap_id);
void img_mem_del_heap(int heap_id);
int img_mem_get_heap_info(int heap_id, uint8_t *type, uint32_t *attrs);

/*
*  related to process context (contains SYSMEM heap's functionality in general)
*/

int img_mem_create_proc_ctx(struct mem_ctx **ctx);
void img_mem_destroy_proc_ctx(struct mem_ctx *ctx);

int img_mem_alloc(struct device *device, struct mem_ctx *ctx, int heap_id,
			size_t size, enum img_mem_attr attributes, int *buf_id);
int img_mem_import(struct device *device, struct mem_ctx *ctx, int heap_id,
			 size_t size, enum img_mem_attr attributes, uint64_t buf_fd,
			 uint64_t cpu_ptr, int *buf_id);
int img_mem_export(struct device *device, struct mem_ctx *ctx, int buf_id,
			 size_t size, enum img_mem_attr attributes, uint64_t *buf_hnd);
void img_mem_free(struct mem_ctx *ctx, int buf_id);

int img_mem_map_um(struct mem_ctx *ctx, int buf_id, struct vm_area_struct *vma);
int img_mem_unmap_um(struct mem_ctx *ctx, int buf_id);
int img_mem_map_km(struct mem_ctx *ctx, int buf_id);
int img_mem_unmap_km(struct mem_ctx *ctx, int buf_id);
void *img_mem_get_kptr(struct mem_ctx *ctx, int buf_id);
uint64_t *img_mem_get_page_array(struct mem_ctx *mem_ctx, int buf_id);
uint64_t img_mem_get_single_page(struct mem_ctx *mem_ctx, int buf_id,
		unsigned int offset);
phys_addr_t img_mem_get_dev_addr(struct mem_ctx *mem_ctx, int buf_id,
		phys_addr_t addr);


int img_mem_sync_cpu_to_device(struct mem_ctx *ctx, int buf_id);
int img_mem_sync_device_to_cpu(struct mem_ctx *ctx, int buf_id);

int img_mem_get_usage(const struct mem_ctx *ctx, size_t *max, size_t *curr);
int img_mmu_get_usage(const struct mem_ctx *ctx, size_t *max, size_t *curr);

#ifdef KERNEL_DMA_FENCE_SUPPORT
struct dma_fence * img_mem_add_fence(struct mem_ctx *ctx, int buf_id);
void img_mem_remove_fence(struct mem_ctx *ctx, int buf_id);
int img_mem_signal_fence(struct mem_ctx *ctx, int buf_id);
#endif

/*
* related to stream MMU context (constains IMGMMU functionality in general)
*/
int img_mmu_ctx_create(struct device *device, const struct mmu_config *config,
					 struct mem_ctx *mem_ctx, int heap_id,
					 int (*callback_fn)(enum img_mmu_callback_type type,
					 int buf_id, void *data),
					 void *callback_data,
					 struct mmu_ctx **mmu_ctx);
void img_mmu_ctx_destroy(struct mmu_ctx *mmu);

int img_mmu_map(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx, int buf_id,
		uint64_t virt_addr, unsigned int map_flags);
int img_mmu_unmap(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx, int buf_id);

int img_mmu_get_pc(const struct mmu_ctx *ctx,
		unsigned int *pc_reg, int *buf_id);
int img_mmu_get_conf(size_t *page_size, size_t *virt_size);
phys_addr_t img_mmu_get_paddr(const struct mmu_ctx *ctx,
		uint64_t vaddr, uint8_t *flags);

int img_mmu_init_cache(struct mmu_ctx *mmu_ctx,	unsigned long cache_phys_start,
		uint32_t cache_size);
int img_mmu_clear_cache(struct mmu_ctx *mmu_ctx);
int img_mmu_move_pg_to_cache(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx,
		int buf_id, uint64_t virt_addr, uint32_t page_size, uint32_t page_idx);
/*
 * virtual address allocation
 */
struct mmu_vaa;

int img_mmu_vaa_create(struct device *device,
		uint32_t base, size_t size, struct mmu_vaa **vaa);
int img_mmu_vaa_destroy(struct mmu_vaa *vaa);
int img_mmu_vaa_alloc(struct mmu_vaa *vaa, size_t size, uint32_t *addr);
int img_mmu_vaa_free(struct mmu_vaa *vaa, uint32_t addr, size_t size);

bool img_mem_calc_parity(unsigned long long input);

/*
 * PDUMP generation:
 * img_pdump_txt_create creates a TXT buffer in RAM,
 *   which is used by img_pdump_txt_printf
 * img_pdump_bin_create creates a PRM or RES buffer in RAM,
 *   which is used by img_pdump_bin_write
 *
 */
struct pdump_buf {
	char   *ptr;
	size_t  size;      /* allocated size of buffer */
	size_t  len;       /* how full is the buffer */
	bool    drop_data; /* do not store data in file */
};
#define PDUMP_TXT      0  /* eg pdump.txt     */
#define PDUMP_PRM      1  /* eg pdump.prm     */
#define PDUMP_RES      2  /* eg pdump.res     */
#define PDUMP_DBG      3  /* eg pdump.dbg     */
#define PDUMP_CRC      4  /* eg pdump.crc     */
#define PDUMP_CRC_CMB  5  /* eg pdump.crc_cmb */
#define PDUMP_MAX      6  

/*
  * VHA PDUMPs.
  * Uses img_pdump buffers to collect pdump information.
  * there are 3 different PDUMP files: TXT, PRM and RES.
  * they are simply buffers in ram.
  * They are mapped into debugfs: /sys/kernel/debug/vhaN/pdump.*
  */
struct pdump_descr {
	struct pdump_buf pbufs[PDUMP_MAX];
	struct mutex     lock;
};

#ifndef OSID
#define _PMEM_ ":MEM"
#else
#define _PMEM_ ":MEM_OS"__stringify(OSID)
#endif
struct pdump_buf *img_pdump_create(struct pdump_descr* pdump, uint32_t pdump_num, size_t size);
int img_pdump_write(struct pdump_descr* pdump, uint32_t pdump_num, const void *ptr, size_t size);
int __img_pdump_printf(struct device* dev, const char *fmt, ...) __printf(2, 3);
#define img_pdump_printf(fmt, ...) __img_pdump_printf(vha->dev, fmt, ##__VA_ARGS__)

void img_pdump_destroy(struct pdump_descr* pdump);
bool img_pdump_enabled(struct pdump_descr* pdump);

#endif /* IMG_MEM_MAN_H */

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */
