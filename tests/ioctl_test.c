/*
 * ioctl_test.c - tiny test tool for /dev/virtualnand ioctls.
 * This is only a test utility, not the final C++ application.
 *
 * Build (from project root):
 *   gcc -Wall -Wextra -Iinclude tests/ioctl_test.c -o tests/ioctl_test
 *
 * Usage:
 *   sudo ./tests/ioctl_test info          device information
 *   sudo ./tests/ioctl_test block <id>    block information
 *   sudo ./tests/ioctl_test erase <id>    erase one block
 *   sudo ./tests/ioctl_test demo  <id>    info -> block -> erase -> block
 *   sudo ./tests/ioctl_test bad           send an unknown command (expects ENOTTY)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "virtual_nand_ioctl.h"

static int do_info(int fd)
{
	struct vnand_device_info info;

	if (ioctl(fd, VNAND_IOC_GET_DEVICE_INFO, &info) < 0) {
		printf("GET_DEVICE_INFO failed: %s\n", strerror(errno));
		return -1;
	}
	printf("Device info:\n");
	printf("  blocks          : %u\n", info.block_count);
	printf("  pages per block : %u\n", info.pages_per_block);
	printf("  page size       : %u bytes\n", info.page_size);
	printf("  total capacity  : %u bytes\n", info.total_size);
	return 0;
}

static int do_block(int fd, unsigned int id)
{
	struct vnand_block_info info;

	memset(&info, 0, sizeof(info));
	info.block_id = id;                      /* input: which block */

	if (ioctl(fd, VNAND_IOC_GET_BLOCK_INFO, &info) < 0) {
		printf("GET_BLOCK_INFO(%u) failed: %s\n", id, strerror(errno));
		return -1;
	}
	printf("Block %u: erase_count=%u used_pages=%u\n",
	       info.block_id, info.erase_count, info.used_pages);
	return 0;
}

static int do_erase(int fd, unsigned int id)
{
	__u32 block_id = id;

	if (ioctl(fd, VNAND_IOC_ERASE_BLOCK, &block_id) < 0) {
		printf("ERASE_BLOCK(%u) failed: %s\n", id, strerror(errno));
		return -1;
	}
	printf("Block %u erased.\n", id);
	return 0;
}

static int do_bad(int fd)
{
	/* A command number the driver does not know: should fail with ENOTTY */
	if (ioctl(fd, _IO(VNAND_IOC_MAGIC, 99), 0) < 0) {
		printf("Unknown ioctl failed as expected: %s\n", strerror(errno));
		return 0;
	}
	printf("Unexpected: unknown ioctl succeeded\n");
	return -1;
}

static int parse_id(const char *s, unsigned int *out)
{
	char *end;
	unsigned long v = strtoul(s, &end, 10);

	if (*s == '\0' || *end != '\0')
		return -1;
	*out = (unsigned int)v;
	return 0;
}

static void usage(const char *prog)
{
	printf("Usage: %s info | bad | block <id> | erase <id> | demo <id>\n", prog);
}

int main(int argc, char *argv[])
{
	int fd, ret = 0;
	unsigned int id = 0;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	fd = open(VNAND_DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " VNAND_DEVICE_PATH);
		return 1;
	}

	if (strcmp(argv[1], "info") == 0) {
		ret = do_info(fd);
	} else if (strcmp(argv[1], "bad") == 0) {
		ret = do_bad(fd);
	} else if (argc >= 3 && parse_id(argv[2], &id) == 0) {
		if (strcmp(argv[1], "block") == 0) {
			ret = do_block(fd, id);
		} else if (strcmp(argv[1], "erase") == 0) {
			ret = do_erase(fd, id);
		} else if (strcmp(argv[1], "demo") == 0) {
			do_info(fd);
			do_block(fd, id);
			do_erase(fd, id);
			ret = do_block(fd, id);
		} else {
			usage(argv[0]);
			ret = 1;
		}
	} else {
		usage(argv[0]);
		ret = 1;
	}

	close(fd);
	return ret ? 1 : 0;
}
