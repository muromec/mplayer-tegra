#ifndef VO_TEGRA_H
#define VO_TEGRA_H
#include <linux/types.h>
#include <linux/ioctl.h>

struct nvmap_create_handle {
	union {
		__u32 key;	/* ClaimPreservedHandle */
		__u32 id;	/* FromId */
		__u32 size;	/* CreateHandle */
	};
	__u32 handle;
};
struct nvmap_alloc_handle {
	__u32 handle;
	__u32 heap_mask;
	__u32 flags;
	__u32 align;
};

struct nvmap_pin_handle {
	unsigned long handles;	/* array of handles to pin/unpin */
	unsigned long addr;	/* array of addresses to return */
	__u32 count;		/* number of entries in handles */
};

struct nvmap_map_caller {
	__u32 handle;		/* hmem */
	__u32 offset;		/* offset into hmem; should be page-aligned */
	__u32 length;		/* number of bytes to map */
	__u32 flags;
	unsigned long addr;	/* user pointer */
};

#define NVMAP_IOC_MAGIC 'N'
#define NVMAP_IOC_CREATE  _IOWR(NVMAP_IOC_MAGIC, 0, struct nvmap_create_handle)
#define NVMAP_IOC_ALLOC    _IOW(NVMAP_IOC_MAGIC, 3, struct nvmap_alloc_handle)
#define NVMAP_IOC_PIN_MULT   _IOWR(NVMAP_IOC_MAGIC, 10, struct nvmap_pin_handle)
#define NVMAP_IOC_GET_ID  _IOWR(NVMAP_IOC_MAGIC, 13, struct nvmap_create_handle)
#define NVMAP_IOC_MMAP       _IOWR(NVMAP_IOC_MAGIC, 5, struct nvmap_map_caller)


#endif
