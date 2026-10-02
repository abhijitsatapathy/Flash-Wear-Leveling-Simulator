// SPDX-License-Identifier: GPL-2.0
/*
 * virtual_nand_driver.c - Stage 4, Part 3
 * Character device driver with a simulated NAND flash held in kernel memory.
 * Supports open, release, read, write, and ioctl (erase / block info / device info).
 */

/* Prefix every pr_*() message with the module name */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>        /* alloc_chrdev_region, file_operations */
#include <linux/cdev.h>      /* cdev_init, cdev_add, cdev_del */
#include <linux/device.h>    /* class_create, device_create */
#include <linux/version.h>   /* LINUX_VERSION_CODE */
#include <linux/uaccess.h>   /* copy_to_user, copy_from_user */
#include <linux/vmalloc.h>   /* vzalloc, vfree */
#include <linux/mutex.h>     /* DEFINE_MUTEX */
#include <linux/string.h>    /* memset */

#include "virtual_nand_ioctl.h"   /* shared with user space */

#define DEVICE_NAME  VNAND_DEVICE_NAME   /* node name: /dev/virtualnand */
#define CLASS_NAME   VNAND_DEVICE_NAME   /* shows up in /sys/class/virtualnand */

/* ------------------------------------------------------------------ */
/* NAND geometry (fixed constants)                                    */
/* ------------------------------------------------------------------ */
#define NAND_BLOCK_COUNT      8      /* number of erase blocks          */
#define NAND_PAGES_PER_BLOCK  16     /* pages inside one block          */
#define NAND_PAGE_SIZE        512    /* bytes per page                  */

#define NAND_BLOCK_SIZE   (NAND_PAGES_PER_BLOCK * NAND_PAGE_SIZE)               /* 8192  */
#define NAND_TOTAL_PAGES  (NAND_BLOCK_COUNT * NAND_PAGES_PER_BLOCK)             /* 128   */
#define NAND_TOTAL_SIZE   (NAND_BLOCK_COUNT * NAND_PAGES_PER_BLOCK * NAND_PAGE_SIZE) /* 65536 */

#define NAND_ERASED_BYTE  0xFF       /* erased NAND cells read as all 1s */

/* ------------------------------------------------------------------ */
/* Metadata: small bookkeeping structures, kept separate from the data */
/* ------------------------------------------------------------------ */
enum nand_page_state {
	PAGE_FREE = 0,      /* never written since the last erase */
	PAGE_WRITTEN = 1    /* has been programmed with data      */
};

struct nand_page_meta {
	enum nand_page_state state;
	unsigned int program_count;   /* writes to this page since the last erase */
};

struct nand_block_meta {
	unsigned int block_id;        /* index of this block                  */
	unsigned int erase_count;     /* how many times the block was erased  */
	unsigned int used_pages;      /* pages in PAGE_WRITTEN state          */
	struct nand_page_meta pages[NAND_PAGES_PER_BLOCK];
};

/* ------------------------------------------------------------------ */
/* Driver state                                                       */
/* ------------------------------------------------------------------ */
static dev_t vnand_devno;              /* major + minor number */
static struct cdev vnand_cdev;         /* the character device object */
static struct class *vnand_class;      /* device class */
static struct device *vnand_device;    /* the device inside that class */

static u8 *nand_buffer;                                    /* simulated NAND data */
static struct nand_block_meta nand_meta[NAND_BLOCK_COUNT]; /* metadata per block/page */
static DEFINE_MUTEX(vnand_lock);                           /* protects buffer + metadata */

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Fill the buffer with the erased value and reset all metadata (module load) */
static void nand_reset_storage(void)
{
	unsigned int b, p;

	memset(nand_buffer, NAND_ERASED_BYTE, NAND_TOTAL_SIZE);

	for (b = 0; b < NAND_BLOCK_COUNT; b++) {
		nand_meta[b].block_id = b;
		nand_meta[b].erase_count = 0;
		nand_meta[b].used_pages = 0;
		for (p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
			nand_meta[b].pages[p].state = PAGE_FREE;
			nand_meta[b].pages[p].program_count = 0;
		}
	}
}

/* Mark every page touched by a write of 'count' bytes starting at 'offset' */
static void nand_mark_pages_written(unsigned int offset, size_t count)
{
	unsigned int first_page = offset / NAND_PAGE_SIZE;
	unsigned int last_page  = (offset + count - 1) / NAND_PAGE_SIZE;
	unsigned int page;

	for (page = first_page; page <= last_page; page++) {
		struct nand_block_meta *bm = &nand_meta[page / NAND_PAGES_PER_BLOCK];
		struct nand_page_meta *pm = &bm->pages[page % NAND_PAGES_PER_BLOCK];

		if (pm->state == PAGE_FREE) {
			pm->state = PAGE_WRITTEN;
			bm->used_pages++;          /* first write to this page */
		}
		pm->program_count++;
	}
}

/*
 * Erase one complete block: data back to 0xFF, pages back to FREE,
 * erase counter + 1. Returns 0 on success, negative errno on failure.
 */
static int nand_erase_block(u32 block_id)
{
	struct nand_block_meta *bm;
	unsigned int p;
	unsigned int new_count;

	if (block_id >= NAND_BLOCK_COUNT) {
		pr_err("erase rejected: invalid block %u (valid range 0..%d)\n",
		       block_id, NAND_BLOCK_COUNT - 1);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&vnand_lock))
		return -ERESTARTSYS;

	bm = &nand_meta[block_id];

	/* Data: the whole block returns to the erased value */
	memset(nand_buffer + (size_t)block_id * NAND_BLOCK_SIZE,
	       NAND_ERASED_BYTE, NAND_BLOCK_SIZE);

	/* Metadata: pages are free again, wear counter goes up */
	for (p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
		bm->pages[p].state = PAGE_FREE;
		bm->pages[p].program_count = 0;
	}
	bm->used_pages = 0;
	bm->erase_count++;
	new_count = bm->erase_count;

	mutex_unlock(&vnand_lock);

	pr_info("erased block %u, new erase count=%u\n", block_id, new_count);
	return 0;
}

/* ------------------------------------------------------------------ */
/* ioctl handlers                                                     */
/* ------------------------------------------------------------------ */

static long vnand_ioctl_get_device_info(void __user *uarg)
{
	struct vnand_device_info info;

	info.block_count     = NAND_BLOCK_COUNT;
	info.pages_per_block = NAND_PAGES_PER_BLOCK;
	info.page_size       = NAND_PAGE_SIZE;
	info.total_size      = NAND_TOTAL_SIZE;

	if (copy_to_user(uarg, &info, sizeof(info)))
		return -EFAULT;

	pr_info("ioctl GET_DEVICE_INFO\n");
	return 0;
}

static long vnand_ioctl_get_block_info(void __user *uarg)
{
	struct vnand_block_info info;

	/* The user tells us which block (input), we fill in the rest (output) */
	if (copy_from_user(&info, uarg, sizeof(info)))
		return -EFAULT;

	if (info.block_id >= NAND_BLOCK_COUNT) {
		pr_err("GET_BLOCK_INFO rejected: invalid block %u\n", info.block_id);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&vnand_lock))
		return -ERESTARTSYS;
	info.erase_count = nand_meta[info.block_id].erase_count;
	info.used_pages  = nand_meta[info.block_id].used_pages;
	mutex_unlock(&vnand_lock);

	if (copy_to_user(uarg, &info, sizeof(info)))
		return -EFAULT;

	pr_info("ioctl GET_BLOCK_INFO block=%u erase_count=%u used_pages=%u\n",
		info.block_id, info.erase_count, info.used_pages);
	return 0;
}

static long vnand_ioctl_erase_block(void __user *uarg)
{
	u32 block_id;

	if (copy_from_user(&block_id, uarg, sizeof(block_id)))
		return -EFAULT;

	pr_info("ioctl ERASE_BLOCK block=%u\n", block_id);
	return nand_erase_block(block_id);
}

/* Single entry point for all ioctl() calls on /dev/virtualnand */
static long vnand_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case VNAND_IOC_GET_DEVICE_INFO:
		return vnand_ioctl_get_device_info(uarg);
	case VNAND_IOC_GET_BLOCK_INFO:
		return vnand_ioctl_get_block_info(uarg);
	case VNAND_IOC_ERASE_BLOCK:
		return vnand_ioctl_erase_block(uarg);
	default:
		pr_err("unknown ioctl command 0x%x\n", cmd);
		return -ENOTTY;      /* "not a typewriter": standard code for unknown ioctl */
	}
}

/* ------------------------------------------------------------------ */
/* File operations                                                    */
/* ------------------------------------------------------------------ */

static int vnand_open(struct inode *inode, struct file *filp)
{
	pr_info("device opened (major=%d, minor=%d)\n",
		imajor(inode), iminor(inode));
	return 0;
}

static int vnand_release(struct inode *inode, struct file *filp)
{
	pr_info("device closed\n");
	return 0;
}

/*
 * read(fd, buf, count): copy up to 'count' bytes from the simulated NAND,
 * starting at *ppos, into the user's buffer. Returns bytes read, 0 at end.
 */
static ssize_t vnand_read(struct file *filp, char __user *buf,
			  size_t count, loff_t *ppos)
{
	unsigned int offset;
	size_t remaining;

	/* At or past the end of the NAND: report end-of-file */
	if (*ppos < 0)
    		return -EINVAL;
	
	if (*ppos >= NAND_TOTAL_SIZE)
		return 0;

	if (count == 0)
		return 0;

	/* Never read more than what is left (short read is normal) */
	remaining = NAND_TOTAL_SIZE - *ppos;
	if (count > remaining)
		count = remaining;

	if (mutex_lock_interruptible(&vnand_lock))
		return -ERESTARTSYS;

	offset = (unsigned int)*ppos;

	/* Kernel buffer -> user buffer. Non-zero return = bad user pointer */
	if (copy_to_user(buf, nand_buffer + offset, count)) {
		mutex_unlock(&vnand_lock);
		return -EFAULT;
	}

	*ppos += count;          /* next read continues where this one ended */
	mutex_unlock(&vnand_lock);

	pr_info("read %zu bytes at offset %u (block %u, page %u), new pos=%lld\n",
		count, offset,
		offset / NAND_BLOCK_SIZE,
		(offset % NAND_BLOCK_SIZE) / NAND_PAGE_SIZE,
		*ppos);

	return count;
}

/*
 * write(fd, buf, count): copy up to 'count' bytes from the user's buffer
 * into the simulated NAND at *ppos. Returns bytes written.
 */
static ssize_t vnand_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	unsigned int offset;
	size_t remaining;

	/* Already at the end: there is no space left */
	if (*ppos < 0)
    		return -EINVAL;

	if (*ppos >= NAND_TOTAL_SIZE)
		return -ENOSPC;

	if (count == 0)
		return 0;

	/* Never write past the end (short write is normal) */
	remaining = NAND_TOTAL_SIZE - *ppos;
	if (count > remaining)
		count = remaining;

	if (mutex_lock_interruptible(&vnand_lock))
		return -ERESTARTSYS;

	offset = (unsigned int)*ppos;

	/* User buffer -> kernel buffer. Non-zero return = bad user pointer */
	if (copy_from_user(nand_buffer + offset, buf, count)) {
		mutex_unlock(&vnand_lock);
		return -EFAULT;
	}

	nand_mark_pages_written(offset, count);
	*ppos += count;
	mutex_unlock(&vnand_lock);

	pr_info("wrote %zu bytes at offset %u (block %u, page %u), new pos=%lld\n",
		count, offset,
		offset / NAND_BLOCK_SIZE,
		(offset % NAND_BLOCK_SIZE) / NAND_PAGE_SIZE,
		*ppos);

	return count;
}
static loff_t vnand_llseek(struct file *filp, loff_t offset, int whence)
{
    return fixed_size_llseek(filp, offset, whence, NAND_TOTAL_SIZE);
}

static const struct file_operations vnand_fops = {
	.owner          = THIS_MODULE,
	.open           = vnand_open,
	.release        = vnand_release,
	.read           = vnand_read,
	.write          = vnand_write,
	.llseek         = vnand_llseek,
	.unlocked_ioctl = vnand_ioctl,
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                 */
/* ------------------------------------------------------------------ */

static int __init vnand_init(void)
{
	int ret;

	pr_info("module loading\n");
	pr_info("geometry: %d blocks x %d pages x %d bytes = %d bytes\n",
		NAND_BLOCK_COUNT, NAND_PAGES_PER_BLOCK, NAND_PAGE_SIZE,
		NAND_TOTAL_SIZE);

	/* 0. Allocate the simulated NAND memory (zeroed), then set it to "erased" */
	nand_buffer = vzalloc(NAND_TOTAL_SIZE);
	if (!nand_buffer) {
		pr_err("failed to allocate %d bytes for NAND buffer\n",
		       NAND_TOTAL_SIZE);
		return -ENOMEM;
	}
	nand_reset_storage();

	/* 1. Ask the kernel for one free major number (minor starts at 0) */
	ret = alloc_chrdev_region(&vnand_devno, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", ret);
		goto err_free_buffer;
	}
	pr_info("allocated major=%d minor=%d\n",
		MAJOR(vnand_devno), MINOR(vnand_devno));

	/* 2. Initialise the cdev and connect it to our file operations */
	cdev_init(&vnand_cdev, &vnand_fops);
	vnand_cdev.owner = THIS_MODULE;

	/* 3. Make the cdev live. After this call, open() can reach our driver */
	ret = cdev_add(&vnand_cdev, vnand_devno, 1);
	if (ret < 0) {
		pr_err("cdev_add failed: %d\n", ret);
		goto err_unregister_region;
	}

	/* 4. Create the device class (class_create changed signature in kernel 6.4) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	vnand_class = class_create(CLASS_NAME);
#else
	vnand_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(vnand_class)) {
		ret = PTR_ERR(vnand_class);
		pr_err("class_create failed: %d\n", ret);
		goto err_cdev_del;
	}

	/* 5. Create the device node /dev/virtualnand */
	vnand_device = device_create(vnand_class, NULL, vnand_devno,
				     NULL, DEVICE_NAME);
	if (IS_ERR(vnand_device)) {
		ret = PTR_ERR(vnand_device);
		pr_err("device_create failed: %d\n", ret);
		goto err_class_destroy;
	}

	pr_info("module loaded, /dev/%s created\n", DEVICE_NAME);
	return 0;

	/* Error unwinding: undo only what succeeded, in reverse order */
err_class_destroy:
	class_destroy(vnand_class);
err_cdev_del:
	cdev_del(&vnand_cdev);
err_unregister_region:
	unregister_chrdev_region(vnand_devno, 1);
err_free_buffer:
	vfree(nand_buffer);
	nand_buffer = NULL;
	return ret;
}

static void __exit vnand_exit(void)
{
	/* Reverse order of initialisation */
	device_destroy(vnand_class, vnand_devno);
	class_destroy(vnand_class);
	cdev_del(&vnand_cdev);
	unregister_chrdev_region(vnand_devno, 1);
	vfree(nand_buffer);

	pr_info("module unloaded, /dev/%s removed\n", DEVICE_NAME);
}

module_init(vnand_init);
module_exit(vnand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhijit Satapathy");
MODULE_DESCRIPTION("Virtual NAND flash simulator - char device with read/write/erase/ioctl");
MODULE_VERSION("0.3");
