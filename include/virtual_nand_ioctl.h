/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * virtual_nand_ioctl.h
 * Shared between the kernel driver and user-space programs.
 * Only fixed-width types are used, so struct layout is identical on both sides.
 */
#ifndef VIRTUAL_NAND_IOCTL_H
#define VIRTUAL_NAND_IOCTL_H

#include <linux/types.h>   /* __u32 (works in kernel and user space) */
#include <linux/ioctl.h>   /* _IO, _IOR, _IOW, _IOWR */

/* Device name (kernel creates /dev/<name>) and path (user space opens it) */
#define VNAND_DEVICE_NAME  "virtualnand"
#define VNAND_DEVICE_PATH  "/dev/" VNAND_DEVICE_NAME

/* Static information about the whole simulated device */
struct vnand_device_info {
	__u32 block_count;       /* number of erase blocks   */
	__u32 pages_per_block;   /* pages in each block      */
	__u32 page_size;         /* bytes per page           */
	__u32 total_size;        /* total capacity in bytes  */
};

/* Information about one block. User fills block_id, driver fills the rest. */
struct vnand_block_info {
	__u32 block_id;          /* which block (0 .. block_count-1)  */
	__u32 erase_count;       /* how many times it has been erased */
	__u32 used_pages;        /* pages written since the last erase */
};

/* ioctl "magic" number: identifies commands that belong to this driver */
#define VNAND_IOC_MAGIC  'N'

/* Command numbers (the direction is from the user program's point of view) */
#define VNAND_IOC_GET_DEVICE_INFO  _IOR (VNAND_IOC_MAGIC, 1, struct vnand_device_info)
#define VNAND_IOC_GET_BLOCK_INFO   _IOWR(VNAND_IOC_MAGIC, 2, struct vnand_block_info)
#define VNAND_IOC_ERASE_BLOCK      _IOW (VNAND_IOC_MAGIC, 3, __u32)

#endif /* VIRTUAL_NAND_IOCTL_H */
