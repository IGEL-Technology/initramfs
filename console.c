/*
 * initramfs init program for kernel 3.13.x.
 * message/console functions.
 * Copyright (C) by IGEL Technology GmbH 2014
 * @author: Klaus Lang

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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/vt.h>
#include "init.h"

static const char *current_console = NULL;

void
msg(init_t *init, int level, const char *fmt, ...)
{
	FILE * file = NULL, *debug = NULL;

	if (current_console) {
		/* LOG_INFO messages are printed only in verbose boot */
		if (init && (level == LOG_INFO) && (init->verbose == 0)) {
			file = NULL;
		} else {
			file = fopen(current_console, "r+");
		}
	}
	
	debug = fopen("/dev/.initramfs.debug", "a");

	va_list ap;
	
	va_start(ap, fmt);
	if (file)
		vfprintf(file, fmt, ap);
	if (debug)
		vfprintf(debug, fmt, ap);
	va_end(ap);
	
	if (file)
		fclose(file);
	if (debug)
		fclose(debug);
}

int
cursor_off(const char *tty)
{
	FILE * file;
	
	if ((file = fopen(tty, "r+")) == NULL) {
		return (1);
	}
	fprintf(file,"\e[?25l\e[?1c");
	fclose(file);
	return (0);
}

int
cursor_on(const char *tty)
{
	FILE * file;
	
	if ((file = fopen(tty, "r+")) == NULL) {
		return (1);
	}
	fprintf(file,"\e[?25h\e[?0c");
	fclose(file);
	return (0);
}

int
change_vt(init_t *init, int console)
{
	int fdc;
	
	if (init->splash && console == SPLASH_CONS) {
		/* do not switch to splash console, because it would
		   destroy the boot splash */
		init->current_console = SPLASH_TTY;
		current_console = SPLASH_TTY;
		return (0);
	}
	
	if ((fdc = open("/dev/tty", O_RDONLY)) < 0) {
		if ((fdc = open("/dev/tty0", O_RDONLY)) < 0) {
			if ((fdc = open("/dev/console", O_RDONLY)) < 0) {
				msg(init,LOG_ERR,"change_vt: unable to open "
				    "/dev/tty, /dev/tty0 and /dev/console\n");
				return (1);
			}
		}
	}
	
	if (ioctl(fdc,VT_ACTIVATE,console)) {
		msg(init,LOG_ERR,"change_vt: unable to VT_ACTIVATE\n");
		close(fdc);
		return (1);
	}
	if (ioctl(fdc,VT_WAITACTIVE,console)) {
		msg(init,LOG_ERR,"change_vt: unable to VT_WAITACTIVE\n");
		close(fdc);
		return (1);
	}
	close(fdc);
  
	switch (console) {
		case SPLASH_CONS:
			init->current_console = SPLASH_TTY;
			current_console = SPLASH_TTY;
			break;
		case CONSOLE_CONS:
			init->current_console = CONSOLE_TTY;
			current_console = CONSOLE_TTY;
			break;
		default:
			init->current_console = BOOT_TTY;
			current_console = BOOT_TTY;
	}
	
	return (0);
}

int
setlogcons(init_t *init, int console)
{
	int fdc;
	struct {
		char fn;
		char subarg;
	} arg;
	
	arg.fn = 11;    /* redirect kernel messages */
	arg.subarg = console;
	if ((fdc = open("/dev/tty1", O_RDONLY)) < 0) {
		msg(init,LOG_ERR,"setlogcons: unable to open /dev/tty1\n");
		return (1);
	}
	if (ioctl(fdc, TIOCLINUX, &arg) < 0) {
		msg(init,LOG_ERR,"setlogcons: unable to set kernel log console\n");
		close(fdc);
		return (1);
	}
	close(fdc);
	return (0);
}

int
setconsole(init_t *init, const char *tty)
{
	int fdcnew;
	int fdcold;
	
	if ((fdcnew = open(tty, O_WRONLY|O_NONBLOCK)) < 0) {
		msg(init,LOG_ERR,"setconsole: unable to open %s\n",tty);
		return (1);
	}
	if (!isatty(fdcnew)) {
		msg(init,LOG_ERR,"setconsole: %s must be a tty.\n", tty);
		close(fdcnew);
		return (1);
	}
	
	if ((fdcold = open("/dev/console", O_WRONLY|O_NONBLOCK)) < 0) {
		msg(init,LOG_ERR,"setconsole: unable to open /dev/console\n");
		close(fdcnew);
		return (1);
	}
	
	/* undo an old console redirection and */
	/* set the new one */
	(void)ioctl(fdcold, TIOCCONS, NULL);
	close(fdcold);
	
	if (ioctl(fdcnew, TIOCCONS, NULL) < 0) {
		msg(init,LOG_ERR,"setconsole: unable to set console redirection\n");
		close(fdcnew);
		return (1);
	}
	close(fdcnew);
	return (0);
}
