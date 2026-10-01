// SPDX-License-Identifier: GPL-2.0
/*
 * virtual_nand_driver.c - Milestone 1
 * Minimal character device driver for the Flash Wear-Leveling Simulator.
 * Creates /dev/virtualnand and logs open/close events.
 */

/* Prefix every pr_*() message with the module name: "virtual_nand_driver: ..." */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>        /* alloc_chrdev_region, file_operations */
#include <linux/cdev.h>      /* cdev_init, cdev_add, cdev_del */
#include <linux/device.h>    /* class_create, device_create */
#include <linux/version.h>   /* LINUX_VERSION_CODE */

#define DEVICE_NAME "virtualnand"   /* node name: /dev/virtualnand */
#define CLASS_NAME  "virtualnand"   /* shows up in /sys/class/virtualnand */

static dev_t vnand_devno;              /* major + minor number */
static struct cdev vnand_cdev;         /* the character device object */
static struct class *vnand_class;      /* device class (lets udev/devtmpfs create the node) */
static struct device *vnand_device;    /* the device inside that class */

/* Called when a user program does open("/dev/virtualnand", ...) */
static int vnand_open(struct inode *inode, struct file *filp)
{
	pr_info("device opened (major=%d, minor=%d)\n",
		imajor(inode), iminor(inode));
	return 0;
}

/* Called when the last reference to the open file is closed */
static int vnand_release(struct inode *inode, struct file *filp)
{
	pr_info("device closed\n");
	return 0;
}

/* Table that tells the kernel which functions handle which system calls */
static const struct file_operations vnand_fops = {
	.owner   = THIS_MODULE,
	.open    = vnand_open,
	.release = vnand_release,
};

static int __init vnand_init(void)
{
	int ret;

	pr_info("module loading\n");

	/* 1. Ask the kernel for one free major number (minor starts at 0) */
	ret = alloc_chrdev_region(&vnand_devno, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", ret);
		return ret;
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
	return ret;
}

static void __exit vnand_exit(void)
{
	/* Same order as the error path: reverse of initialisation */
	device_destroy(vnand_class, vnand_devno);
	class_destroy(vnand_class);
	cdev_del(&vnand_cdev);
	unregister_chrdev_region(vnand_devno, 1);

	pr_info("module unloaded, /dev/%s removed\n", DEVICE_NAME);
}

module_init(vnand_init);
module_exit(vnand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Virtual NAND flash simulator - Milestone 1: minimal char device");
MODULE_VERSION("0.1");
