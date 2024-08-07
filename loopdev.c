#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include "init.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <linux/loop.h>
#include <linux/major.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

/*
 * create loop device with given file offset and size (if size is 0 -> unlimited)
 *
 * returns 0 : if everything was done successfully
 *         1 : if open of given file failed
 *         2 : if open of given loop device failed
 *         3 : if loop binding to file IOCTL failed
 *         4 : if setting loop settings (offset, sizelimit) failed
 */

int loopdev_setup_device(const char *file, const char *loopdev, mode_t mode, uint64_t offset, uint64_t size)
{
	int fd = -1, loop_fd = -1;
	struct loop_info64 info;

	memset(&info, 0, sizeof(struct loop_info64));

	fd = open(file, mode);
	if (fd < 0) {
		return 1;
	}

	loop_fd = open(loopdev, mode);
	if (loop_fd < 0) {
		close(fd);
		return 2;
	}

	if (ioctl(loop_fd, LOOP_SET_FD, fd) < 0) {
		close(fd);
		close(loop_fd);
		return 3;
	}

	close(fd);

	strncpy((char *)info.lo_file_name, file, LO_NAME_SIZE);
	info.lo_offset = offset;
	info.lo_sizelimit = size;

	if (ioctl(loop_fd, LOOP_SET_STATUS64, &info) != 0) {
		ioctl(loop_fd, LOOP_CLR_FD, 0);
		close(loop_fd);
		return 4;
	}
	close(loop_fd);

	return 0;
}

/*
 * deletes a loop device
 *
 * returns 0 : if everything was done successfully
 *         1 : if open of given loop device failed
 *         2 : if loop clearing IOCTL failed
 */

int loopdev_delete_device(const char *loopdev)
{
	int loop_fd = -1;

	loop_fd = open(loopdev, O_RDWR);
        if (loop_fd < 0) {
                return 1;
        }

	if (ioctl(loop_fd, LOOP_CLR_FD, 0) < 0) {
		close(loop_fd);
		return 2;
	}

	close(loop_fd);
	return 0;
}

/*
 * get free loop device
 *
 * returns <number of first free loopdev> and -1 in case of error
 */

int get_free_loopdev_num(void)
{
	int i, fd, err;
	char loop_device[26];
	struct stat st;
	struct loop_info64 info;

	for (i = 0; i <= 7; i++) {
		snprintf(loop_device, 26, "/dev/loop%d", i);
		if (stat(loop_device, &st) != 0) {
			mknod(loop_device, S_IFBLK | S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, makedev(LOOP_MAJOR, i));
		}
		err = stat(loop_device, &st);
		if (err || !S_ISBLK(st.st_mode)) {
			continue;
		}
		fd = open(loop_device, O_RDONLY);
		if (fd >= 0) {
			if (ioctl(fd, LOOP_GET_STATUS64, &info) != 0) {
				if (errno == ENXIO) {
					close(fd);
					return i;
				}
			}
			close(fd);
		}
	}

	return (-1);
}
