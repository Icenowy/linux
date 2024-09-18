/*!
 *****************************************************************************
 *
 * @File       vha.h
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

#ifndef _VHA_UAPI_H
#define _VHA_UAPI_H

#if defined(__KERNEL__)
#include <linux/ioctl.h>
#include <linux/types.h>
#elif defined(__linux__)
#include <sys/ioctl.h>
#include <inttypes.h>
#else
#error unsupported build
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define VHA_OCM_MAX_NUM_PAGES 128
#define VHA_CORE_MAX_ALT_ADDRS 16
#define VHA_MAX_CORES 8

// represents OCM types,
#define VHA_LOCAL_OCM  0  /* Local OCM */
#define VHA_SHARED_OCM 1  /* Shared OCM */
#define VHA_OCM_TYPE_MAX 2

/* device hw properties */
struct vha_hw_props {
	uint64_t product_id;
	uint64_t core_id;
	uint64_t soc_axi;
	uint8_t  mmu_width;	   /* MMU address width: 40, or 0 if no MMU */
	uint8_t  mmu_ver;      /* MMU version */
	uint32_t mmu_pagesize; /* MMU page size */

	union {
		struct {
			unsigned rtm: 1;
			unsigned parity: 1;
		} supported;
		uint8_t features;
	};
	bool     dummy_dev;
	bool     skip_bvnc_check;
	bool     use_pdump;
	uint8_t  num_cnn_core_devs;
	uint32_t locm_size_bytes; /* per core */
	uint32_t socm_size_bytes; /* total size for all cores */
	uint32_t socm_core_size_bytes; /* per core */
	uint32_t clock_freq;		/* hardware clock rate, kHz */

} __attribute__((aligned(8)));

struct vha_cnn_props {
	/* TOBEDONE */
};

/* command sent to device */
enum vha_cmd_type {
	VHA_CMD_INVALID          = 0x000,
	VHA_CMD_CNN_SUBMIT       = 0x101,
	VHA_CMD_CNN_SUBMIT_MULTI,
	VHA_CMD_CNN_PDUMP_MSG
};

/* optional flags for commands */
#define VHA_CMDFLAG_NOTIFY       0x0001 /* send response when cmd complete */
#define VHA_CHECK_CRC            0x0002 /* check the combined CRCs */
#define VHA_EXEC_TIME_SET        0x0004 /* execution time is valid */

/*
 * message from user to be sent to VHA (write).
 * A command will contain a number of input and ouput buffers
 * and some command specific parameters.
 * Buffers must be identified by their buffer id.
 * All buffer ids must *precede* any other parameters:
 *    input buf ids,
 *    output buf ids,
 *    followed by other parameters.
 */
struct vha_user_cmd {
	uint32_t cmd_id;     /* arbitrary id for cmd */
	uint16_t cmd_type;   /* enum vha_cmd_type */
	uint16_t flags;      /* VHA_CMDFLAG_xxx */
	uint8_t  priority;   /* WL priority */
	uint8_t  padding;    /* padding to keep data 32bit aligned */
	uint8_t  num_bufs;   /* total number of buffers */
	uint8_t  num_inbufs; /* number of input buffers */
	uint32_t data[0];    /* 0-N words: input bufids
	                      * followed by other bufids
	                      * followed by other parameters */
};

/* Structure defining hardware issues */
struct vha_hw_brns {
	union {
		struct {
			unsigned bRN71649: 1;
			unsigned bRN71556: 1;
			unsigned bRN71338: 1;
		} bit;
		uint64_t map;
	};
};

/*
 * CNN_SUBMIT message written from user to VHA.
 * 3 input buffers: cmdstream, input image, coefficients
 * 1 output buffer
 * 1 internal buffer (optional)
 * offsets into the input and output buffers
 * and a register map: this tells the driver which alt-register-N
 * will contain the address of which buffer.
 */
struct vha_subseg_info {
	uint32_t cmdbuf_offset;
	uint32_t cmdbuf_size;
};
struct vha_user_cnn_submit_cmd {
	struct vha_user_cmd msg;
	uint32_t cmdbuf;                             /* bufid of cmdstream buffer */
	uint32_t bufs[VHA_CORE_MAX_ALT_ADDRS];       /* bufid of IN, COEFF, OUT, INTERNAL,CRC,DBG,WB buffers */
	uint32_t bufoffsets[VHA_CORE_MAX_ALT_ADDRS]; /* offsets into inbufs and outbufs buffers */
	uint32_t bufsizes[VHA_CORE_MAX_ALT_ADDRS];   /* sizes of the inbufs and outbufs buffers */
	uint8_t  regidx[VHA_CORE_MAX_ALT_ADDRS];     /* register to be used for inbufs and outbufs */
	uint32_t onchipram_map_id;                   /* OCM mapping id - hot pages */
	uint32_t onchipram_bufs[VHA_OCM_TYPE_MAX];   /* OCM linear mapping buffers */
	uint32_t estimated_cycles;                   /* estimated number of cycles for this command */
	uint64_t expected_ip_capab;                  /* expected BVNC */
	uint64_t hw_brns;                            /* BRNSs bit map */
	uint32_t subseg_num;                         /* number of subsegments in subseg_info array */
	struct vha_subseg_info subseg_info[1];       /* there's always at least one subsegment */
} __attribute__((aligned(8)));

/*
 * CNN_SUBMIT_MULTI message written from user to VHA.
 * 3 input buffers: cmdstream(s), input image, coefficients
 * 1 output buffer
 * 1 internal buffer (optional)
 * offsets into the input and output buffers
 * and a register map: this tells the driver which alt-register-N
 * will contain the address of which buffer.
 */
struct vha_user_cnn_submit_multi_cmd {
	struct vha_user_cmd msg;
	uint32_t cmdbuf[VHA_MAX_CORES];              /* bufid of cmdstream buffer */
	uint32_t bufs[VHA_CORE_MAX_ALT_ADDRS];       /* bufid of IN, COEFF, OUT, INTERNAL,CRC,DBG,WB buffers */
	uint32_t bufoffsets[VHA_CORE_MAX_ALT_ADDRS]; /* offsets into inbufs and outbufs buffers */
	uint32_t bufsizes[VHA_CORE_MAX_ALT_ADDRS];   /* sizes of the inbufs and outbufs buffers */
	uint8_t  regidx[VHA_CORE_MAX_ALT_ADDRS];     /* register to be used for inbufs and outbufs */
	uint8_t  num_cores;                          /* number of cores required for this workload */
	uint32_t onchipram_bufs[VHA_OCM_TYPE_MAX];   /* OCM linear mapping buffers */
	uint32_t crcs[VHA_MAX_CORES];                /* golden CRCs */
	uint64_t exec_time;                          /* expected execution time */
	uint32_t shared_circ_buf_offs;               /* circular buffer offset in the shared memory */
	uint32_t estimated_cycles;                   /* estimated number of cycles for this command */
	uint64_t expected_ip_capab;                  /* expected BVNC */
	uint64_t hw_brns;                            /* BRNSs bit map */
} __attribute__((aligned(8)));

/*
 * response from from kernel module to user.
 */
struct vha_user_rsp {
	uint32_t cmd_id;	/* arbitrary id to identify cmd */
	uint32_t err_no;	/* 0 if successful, else -ve */
	uint64_t rsp_err_flags;
	uint32_t data[0];	/* 0-N words of additional info */
};

/*
 * response returned after CNN_SUBMIT.
 */
struct vha_user_cnn_submit_rsp {
	struct vha_user_rsp msg;
	uint64_t last_proc_us;	/* processing time in us,
				measured with system clock */
	uint32_t mem_usage;	/* device memory used */
	uint32_t hw_cycles;	/* hardware cycles used */
} __attribute__((aligned(8)));

#define MAX_VHA_USER_RSP_SIZE (sizeof(struct vha_user_cnn_submit_rsp))

/* response returned when querying for heaps */
struct vha_heap_data {
	uint32_t id;				/* Heap ID   */
	uint32_t type;				/* Heap type */
	uint32_t attributes;		/* Heap attributes
		defining capabilities that user may treat as hint
		when selecting the heap id during allocation/importing */
};

#define VHA_MAX_HEAPS 16

struct vha_heaps_data {
	struct vha_heap_data heaps[VHA_MAX_HEAPS];		/* [OUT] Heap data */
} __attribute__((aligned(8)));

/* parameters to allocate a device buffer */
struct vha_alloc_data {
	uint64_t size;				/* [IN] Size of device memory (in bytes)    */
	uint32_t heap_id;			/* [IN] Heap ID of allocator
														or VHA_USE_DEFAULT_MEM_HEAP */
	uint32_t attributes;	/* [IN] Attributes of buffer: img_mem_attr  */
	char     name[8];			/* [IN] short name for buffer               */
	uint32_t buf_id;			/* [OUT] Generated buffer ID                */
} __attribute__((aligned(8)));

/* parameters to import a device buffer */
struct vha_import_data {
	uint64_t size;				/* [IN] Size of device memory (in bytes)    */
	uint64_t buf_fd;			/* [IN] File descriptor */
	uint64_t cpu_ptr;			/* [IN] Cpu pointer of buffer to import */
	uint32_t heap_id;			/* [IN] Heap ID of allocator                */
	uint32_t attributes;	/* [IN] Attributes of buffer                */
	char     name[8];			/* [IN] short name for buffer               */
	uint32_t buf_id;			/* [OUT] Generated buffer ID                */
} __attribute__((aligned(8)));

/* parameters to export a device buffer */
struct vha_export_data {
	uint32_t buf_id;       /* [IN] Buffer ID to be exported */
	uint64_t size;         /* [IN] Size to be exported */
	uint32_t attributes;   /* [IN] Attributes of buffer */
	uint64_t buf_hnd;      /* [OUT] Buffer handle (file descriptor) */
} __attribute__((aligned(8)));

struct vha_free_data {
	uint32_t buf_id;	/* [IN] ID of device buffer to free */
};

enum vha_map_flags {
	VHA_MAP_FLAG_NONE       = 0x0,
	VHA_MAP_FLAG_READ_ONLY  = 0x1,
	VHA_MAP_FLAG_WRITE_ONLY = 0x2,
	VHA_MAP_FLAG_IO         = 0x4,
	VHA_MAP_FLAG_MODEL      = 0x8,
};

/* parameters to map a buffer into device */
struct vha_map_to_onchip_data {
	uint64_t virt_addr;		/* [IN] Device virtual address of a mapping */
	uint32_t buf_id;		/* [IN] ID of device buffer to map to VHA */
	uint32_t page_size;		/* [IN] Page size */
	uint32_t num_pages;		/* [IN] The number of pages to be mapped */
	uint32_t page_idxs[VHA_OCM_MAX_NUM_PAGES];
							/* [IN] Indexes of pages to be mapped */
	uint32_t map_id;		/* [IN/OUT] if map_id == 0, creates new mapping
								and generates new map_id,
								otherwise using existing map_id*/
} __attribute__((aligned(8)));

/* parameters to map a buffer into device */
struct vha_map_data {
	uint64_t virt_addr;	/* [IN] Device virtual address to map     */
	uint32_t buf_id;	/* [IN] ID of device buffer to map to VHA */
	uint32_t flags;		/* [IN] Mapping flags, see vha_map_flags  */
} __attribute__((aligned(8)));

struct vha_unmap_data {
	uint32_t buf_id;	/* [IN] ID of device buffer to unmap from VHA */
} __attribute__((aligned(8)));

enum vha_buf_status {
	VHA_BUF_UNFILLED,
	VHA_BUF_FILLED_BY_SW,
	VHA_BUF_FILLED_BY_HW
};
#define VHA_SYNC_NONE (-1)
/* parameters to set buffer status ("filled" or "unfilled") */
struct vha_buf_status_data {
	uint32_t buf_id;
	uint32_t status;	/* enum vha_buf_status */
	int      in_sync_fd;   /* input sync to attach */
	bool     out_sync_sig; /* output sync signal */
} __attribute__((aligned(8)));

enum vha_sync_op {
	VHA_SYNC_OP_CREATE_OUT, /* create output sync_fd */
	VHA_SYNC_OP_MERGE_IN,   /* merge input sync_fds */
	VHA_SYNC_OP_RELEASE     /* release syncs */
};

/* parameters to manage sync_fds */
#define VHA_SYNC_MAX_BUF_IDS     (VHA_CORE_MAX_ALT_ADDRS)
#define VHA_SYNC_MAX_IN_SYNC_FDS (VHA_CORE_MAX_ALT_ADDRS)
struct vha_sync_create_data {
	uint32_t buf_id_count;                  /* [IN] number of output buffers */
	uint32_t buf_ids[VHA_SYNC_MAX_BUF_IDS]; /* [IN] list of output buffer ids */
};
struct vha_sync_merge_data {
	uint32_t in_sync_fd_count;                 /* [IN] number of input sync_fds */
	int in_sync_fds[VHA_SYNC_MAX_IN_SYNC_FDS]; /* [IN] list of input sync_fds */
};
struct vha_sync_release_data {
	uint32_t buf_id_count;                  /* [IN] number of buffers */
	uint32_t buf_ids[VHA_SYNC_MAX_BUF_IDS]; /* [IN] list of buffer ids */
};
struct vha_sync_data {
	enum vha_sync_op op;
	union {
		struct vha_sync_create_data create_data;   /* create output sync_fd data */
		struct vha_sync_merge_data merge_data;     /* merge input sync_fds data */
		struct vha_sync_release_data release_data; /* release syncs data */
	};
	int sync_fd; /* [OUT] output sync_fd/sync_fd for merged input sync_fds */
} __attribute__((aligned(8)));

struct vha_cancel_data {
	uint32_t cmd_id;      /* [IN] masked ID of commands to be cancelled */
	uint32_t cmd_id_mask; /* [IN] mask for command IDs to be cancelled */
	bool     respond;     /* [IN] if true, respond to this cancel request */
} __attribute__((aligned(8)));

struct vha_version_data {
	char  digest[33];     /* [OUT] digest of this interface file */
} __attribute__((aligned(8)));

#define VHA_IOC_MAGIC  'q'

#define VHA_IOC_HW_PROPS          _IOR(VHA_IOC_MAGIC,  0, struct vha_hw_props)
#define VHA_IOC_QUERY_HEAPS       _IOR(VHA_IOC_MAGIC,  1, struct vha_heaps_data)
#define VHA_IOC_ALLOC             _IOWR(VHA_IOC_MAGIC, 2, struct vha_alloc_data)
#define VHA_IOC_IMPORT            _IOWR(VHA_IOC_MAGIC, 3, struct vha_import_data)
#define VHA_IOC_EXPORT            _IOWR(VHA_IOC_MAGIC, 4, struct vha_export_data)
#define VHA_IOC_FREE              _IOW(VHA_IOC_MAGIC,  5, struct vha_free_data)
#define VHA_IOC_VHA_MAP_TO_ONCHIP _IOW(VHA_IOC_MAGIC,  6, struct vha_map_to_onchip_data)
#define VHA_IOC_VHA_MAP           _IOW(VHA_IOC_MAGIC,  7, struct vha_map_data)
#define VHA_IOC_VHA_UNMAP         _IOW(VHA_IOC_MAGIC,  8, struct vha_unmap_data)
#define VHA_IOC_BUF_STATUS        _IOW(VHA_IOC_MAGIC,  9, struct vha_buf_status_data)
#define VHA_IOC_SYNC              _IOWR(VHA_IOC_MAGIC, 10, struct vha_sync_data)
#define VHA_IOC_CANCEL            _IOW(VHA_IOC_MAGIC,  11, struct vha_cancel_data)

#define VHA_IOC_VERSION           _IOW(VHA_IOC_MAGIC,  16, struct vha_version_data)

#define VHA_SCOPE_DEV_NAME "vha_scope"

/* vha scope context
 * */
struct vha_trace_ctx {
	unsigned model_id;	/* model id */
	unsigned frame_id;	/* inference id */
	unsigned dev_id;		/* device id */
	unsigned osid;			/* device id */
	unsigned pid;				/* process id */
	unsigned tid;				/* thread id */
};

/* Event information, available from vha_info */
struct vha_timing_data {
	unsigned evt_type;			/* event type */
	unsigned seqno;					/* continually increments */
	unsigned dev_id;				/* device id */
	unsigned timestamp_lo;	/* in microseconds */
	unsigned timestamp_hi;
	unsigned type;					/* either SUBMIT or COMPLETE or ERROR */
	unsigned cycles;				/* HW cycle count */
	unsigned pid;						/* process id */
};

enum vha_scope_evt_type {
	VHA_EVENT_TIMING,
	VHA_EVENT_NUM
};

enum vha_timing_data_type {
	VHA_EVENT_TYPE_ENQUEUE,
	VHA_EVENT_TYPE_SUBMIT,
	VHA_EVENT_TYPE_COMPLETE,
	VHA_EVENT_TYPE_ERROR,
	VHA_EVENT_TYPE_NUM
};

#if defined(__cplusplus)
}
#endif

#endif /* _VHA_UAPI_H */
