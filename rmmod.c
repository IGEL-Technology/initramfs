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
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include "init.h"

static inline void filename2modname(char *modname, const char *afterslash)
{
	unsigned int i;
	
	/* Convert to underscores, stop at first . */
	for (i = 0; afterslash[i] && afterslash[i] != '.'; i++) {
		if (afterslash[i] == '-')
			modname[i] = '_';
		else
			modname[i] = afterslash[i];
	}
	modname[i] = '\0';
}

static const char *moderror(int err)
{
	switch (err) {
		case EINVAL:
			return "Invalid parameters";
		case ENOENT:
			return "No such module loaded";
		case EAGAIN:
			return "Module is in use";
		default:
			return strerror(err);
	}
}

int rmmod_cmd(const char *name)
{
	int /*n,*/ ret = EXIT_SUCCESS;
	unsigned int flags = O_NONBLOCK|O_EXCL;
	const char *afterslash;
	char *module_name;
		
#if 0
	/* Parse command line. */
	n = getopt_flags(argc, argv, "wfa");
	if((n & 1))	// --wait
		flags &= ~O_NONBLOCK;
	if((n & 2))	// --force
		flags |= O_TRUNC;
	if((n & 4)) {
		/* Unload _all_ unused modules via NULL delete_module() call */
		/* until the number of modules does not change */
		size_t nmod = 0; /* number of modules */
		size_t pnmod = -1; /* previous number of modules */

		while (nmod != pnmod) {
			if (syscall(__NR_delete_module, NULL, flags) != 0) {
				if (errno==EFAULT)
					return(ret);
				msg(NULL,LOG_ERR,"rmmod: rmmod %s failed\n", name)
				return EXIT_FAILURE;
			}
			pnmod = nmod;
		}
		return EXIT_SUCCESS;
	}
#endif
		

	afterslash = strrchr(name, '/');
	if (!afterslash)
		afterslash = name;
	else
		afterslash++;
	module_name = alloca(strlen(afterslash) + 1);
	filename2modname(module_name, afterslash);
		
	ret = syscall(__NR_delete_module, module_name, flags);
	if (ret != 0) {
		msg(NULL,LOG_ERR,"rmmod: can not rmmod '%s' (errno %d): %s\n",
				name, errno, moderror(errno));
		return(EXIT_FAILURE);
	}

	return(EXIT_SUCCESS);
}
