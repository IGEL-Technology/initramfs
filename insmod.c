/*
 * initramfs init program for kernel 3.13.x.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <asm/unistd.h>
#include <sys/syscall.h>
#include "init.h"

/* We use error numbers in a loose translation... */
static const char *moderror(int err)
{
	switch (err) {
		case ENOEXEC:
			return "Invalid module format";
		case ENOENT:
			return "Unknown symbol in module";
		case ESRCH:
			return "Module has wrong symbol version";
		case EINVAL:
			return "Invalid parameters";
		default:
			return strerror(err);
	}
}

int insmod_cmd(char *filename, struct mod_opt_t *opts)
{
	int fd;
	long int ret;
	struct stat st;
	unsigned long len;
	void *map;
	char *options = strdup("");

	if (!filename) {
		msg(NULL,LOG_ERR,"insmod: can not find %s\n",filename);
                free(options);
		return -1;
	}

	/* evaluate options */
	while( opts ) {
		options = realloc(options, strlen(options) + 2 + strlen(opts->m_opt_val) + 2);
		/* Spaces handled by "" pairs, but no way of escaping quotes */
		if (strchr(opts->m_opt_val, ' ')) {
			strcat(options, "\"");
			strcat(options, opts->m_opt_val);
			strcat(options, "\"");
		} else {
			strcat(options, opts->m_opt_val);
		}
		strcat(options, " ");
		
		opts = opts->m_next;
	}

	if ((fd = open(filename, O_RDONLY, 0)) < 0) {
		msg(NULL,LOG_ERR,"insmod: can not open module '%s'\n", filename);
	}

	fstat(fd, &st);
	len = st.st_size;
	map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		msg(NULL,LOG_ERR,"insmod: can not mmap '%s'\n", filename);
                free(options);
		return 1;
	}

	ret = syscall(__NR_init_module, map, len, options);
	if (ret != 0) {
		msg(NULL,LOG_ERR,"insmod: can not insmod '%s' (errno %d): %s\n",
				filename, errno, moderror(errno));
                free(options);
		if (errno == 0)
			return 1;
		return -errno;
	}

	return 0;
}

