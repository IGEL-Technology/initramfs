/*
 * initramfs init program for kernel 4.4.x
 * main functions, with unionfs support.
 * Copyright (C) by IGEL Technology GmbH 2017

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
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#ifndef BLKFLSBUF
#include <linux/fs.h>
#endif
#include <libgen.h>
#include <sys/ioctl.h>
#include <linux/loop.h>
#include "init.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdint.h>
#include <igel64/igel.h>
#include <limits.h>
#include <igel64/os11/bootregfs.h>
#include <linux/major.h>

#define PATH_SIZE 4096
#define WAIT_TIME  250000 /* in usec */
#define ERROR_TIME 15	  /* in seconds */

#define IGF_BOOT_NAME	"/dev/igfboot"
#define IGF_DISK_NAME	"/dev/igfdisk"
#define IGF_SYS_NAME	"/dev/igfsys"
#define IGM_SYS_NAME	"/dev/igmsys"
#define FW_DISK_NAME	"/dev/fwdisk"
#define ISO_SRC_NAME    "/dev/isosrc"
#define ISO_SRC_PATH    "/mntisosrc"
#define ISO_TRGT_PATH   "/igfimage/images"
#define IGF_BSPL_CHROOT "/chroot"
#define IGF_BSPL_CHROOT_SYS "/chroot/sys"
#define IGF_BSPL_CHROOT_DEV "/chroot/dev"
#define IGF_BSPL_CHROOT_PROC "/chroot/proc"
#define IGF_IMAGE_NAME  "/igfimage/igfimage.bin"
#define ISO_IMAGE_NAME  "/igfimage/installer.iso"
#define IGF_TOKEN_DD_IMAGE "/token/boot/ddimage.bin"
#define IGF_TOKEN_FIRMWARE_DIR "/token/images"
#define IGF_TOKEN_BOOTSPLASH_OSC "/token/boot/osc-bootsplash.squashfs"
#define IGF_PXE_DD_IMAGE   "/pxeboot/ddimage.bin"
#define INITRD_IMG "/initrd.image"

#define IGF_MNT_SYSTEM "/dev/.mnt-system"

#define IGF_IMAGE_MOUNTPOINT "/igfimage"
#define ISO_IMAGE_MOUNTPOINT "/igfimage/iso-mnt"
#define IGEL_PREPARE_MIGRATION "/etc/igel/major_update/prepare_migration.sh"

static char buffer[4096];
static devlist_t *device_blacklist = NULL;

static void bootsplash_start(init_t *init);
static void bootsplash_stop(init_t *init);
static void delete_contents(init_t *init, const char *directory);
static int move_file(const char *src_filename, const char *dest_filename) __attribute__((unused));
static int copy_device(const char *src_filename, const char *dest_filename, uint64_t size) __attribute__((unused));
static void cleanup_zram(void) __attribute__((unused));
static int copy_files(const char * src_dir, const char * trg_dir) __attribute__((unused));
static void get_present_igfs(int *igf_array, int *count_igf) __attribute__((unused));

static void
print_init(init_t *init)
{
	if (init->found && init->devname){
		if(strncmp(init->devname,"sr",2)== 0) { // CD/DVD
			msg(init,LOG_NOTICE," * found low-level device /dev/%s (%d:%d)\n"
				,init->devname, init->dev.major, init->dev.minor);
		} else { // USB Token, Harddisk, ...
			msg(init,LOG_NOTICE," * found low-level device /dev/%s%s%d (%d:%d)\n"
				,init->devname, init->part_prefix, init->part, init->dev.major
				,init->dev.minor);
		}
	}

	msg(init,LOG_NOTICE,"   bootversion = %d\n",init->bootversion);
	msg(init,LOG_NOTICE,"   splash = %d\n",init->splash);
	msg(init,LOG_NOTICE,"   verbose = %d\n",init->verbose);
	msg(init,LOG_NOTICE,"   failsafe = %d\n", init->failsafe);
	if (init->initcmd) {
		msg(init,LOG_NOTICE,"   initcmd = %s %d\n",init->initcmd,init->runlevel);
	}
	msg(init,LOG_NOTICE,"   try = %d\n",init->try);
}

static int igel_delete_dev(void)
{
	int fd, ret;

	if ((fd = open("/dev/igel-control", O_RDWR)) < 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "No /dev/igel-control. Using old driver?\n");
			return 0;
		}

		fprintf(stderr, "Failed to open /dev/igel-control:%s\n",
		        strerror(errno));

		return -1;
	}

	ret = ioctl(fd, IGFLASH_REMOVE_DEVICE, "igf");
	fprintf(stderr, "Delete ioctl result: %d\n", ret);

	close(fd);
	return ret;
}

static void
add_device_to_blacklist(struct mmdev  *dev)
{
	devlist_t *bl;
	
	if (! dev) return;
	
	if (! device_blacklist) {
		/* create a new device blacklist */
		device_blacklist = malloc(sizeof(devlist_t));
		if (device_blacklist) {
    			device_blacklist->dev.major = dev->major;
			device_blacklist->dev.minor = dev->minor;
			device_blacklist->next = NULL;
		}
	}
	else {
		/* add the device to the end of the existing device_blacklist */
		bl = device_blacklist;
		while (bl) {
			if (bl->next == NULL)
				break;
			bl = bl->next;
		}
		bl->next = malloc(sizeof(devlist_t));
		if (bl->next) {
    			bl = bl->next;
			bl->dev.major = dev->major;
			bl->dev.minor = dev->minor;
			bl->next = NULL;
		}
	}
}

/* 0 = no, 1 = yes */
static int
device_in_blacklist(struct mmdev  *dev)
{
	devlist_t *bl = device_blacklist;
	
	if (! bl) return 0;
	while (bl) {
		if ((bl->dev.major == dev->major) &&
		    (bl->dev.minor == dev->minor))
			return 1;
		bl = bl->next;
	}
	
	return 0;
}

static int
create_blk_device(const char *newname, int major, int minor)
{
	int err;

	unlink(newname);

	if (mknod(newname, S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
	      makedev(major, minor)) != 0) {
		return 1;
	}

	err = chown(newname, 0, 6);

	return 0;
}

static void
prepare_filesystem(void)
{
	int err, i;
	char path[PATH_SIZE];
        char str_buffer[1024];
	
	err = mount("none","/sys","sysfs",0,NULL);
	err = mount("none","/proc","proc",0,NULL);
	err = mount("udev","/dev","devtmpfs",0,"mode=0755,size=90%");
	
	/* prepare /dev/pts */
	mkdir("/dev/pts", 0755);
	err = mount("devpts","/dev/pts","devpts",0,NULL);
	
	/* preparation for unionfs mounts */
	strcpy(str_buffer, IGF_MNT_SYSTEM);
	mkdir(str_buffer, 0755);
	strcat(str_buffer, "/ro");
	mkdir(str_buffer, 0755);
	strcat(str_buffer, "/sys");
	mkdir(str_buffer, 0755);
	strcpy(str_buffer, IGF_MNT_SYSTEM);
	strcat(str_buffer, "/rw");
	mkdir(str_buffer, 0755);
	err = mount("system-rw",str_buffer,"tmpfs",MS_NOATIME,NULL);
	strcat(str_buffer, "/sys");
	mkdir(str_buffer, 0755);
	
	/* create default character device nodes */
	mknod("/dev/null",    S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
		makedev(1, 3));
	mknod("/dev/zero",    S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
		makedev(1, 5));
	mknod("/dev/random",  S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
		makedev(1, 8));
	mknod("/dev/urandom", S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
		makedev(1, 9));
	mknod("/dev/tty",     S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH, 
		makedev(5, 0));
	mknod("/dev/console", S_IFCHR | S_IRUSR|S_IWUSR, 
		makedev(5, 1));
	mknod("/dev/ptmx",    S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH, 
		makedev(5, 2));
	for (i=0; i<13; i++) {
		snprintf(path, sizeof(path), "/dev/tty%d", i);
		path[sizeof(path)-1] = '\0';
		
		mknod(path,   S_IFCHR | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, 
			makedev(4, i));
		err = chown(path,0,100);
	}
	
	/* create default block device nodes */
	mknod("/dev/flashdisk",  S_IFBLK | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, 
		makedev(61, 0));
	err = chown("/dev/flashdisk",0,6);
	mknod("/dev/igelraw",    S_IFBLK | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, 
		makedev(61, 0));
	err = chown("/dev/igelraw",0,6);
	for (i=0; i<3; i++) {
		snprintf(path, sizeof(path), "/dev/igf%d", i);
		path[sizeof(path)-1] = '\0';
		
		mknod(path,      S_IFBLK | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, 
			makedev(61, i));
		err = chown(path,0,6);
	}
}

/* check if a kernel module is already loaded */
/* return 1 = loaded, 0 = not loaded, -1 = can't tell */
int 
kmodule_already_loaded (init_t *init, const char *name)
{
	int fd, len;
	char *p, *s;

	/* check if this is a kernel built in (/sys/module/name directory exists) */

	if (file_exists("/sys/module/%s", name) == 1) {
		return 1;
	}

	if (asprintf(&s,"/%s.ko", name) >= 0 && s != NULL) {
		if (!init) {
			struct utsname un;
			if (uname(&un) == 0) {
				if (grep_kernel_module_in_file(s, "/lib/modules/%s/modules.builtin",un.release) == 1) {
					return 1;
				}
			}
		} else {
			if (grep_kernel_module_in_file(s, "%s/modules.builtin", init->moddir) == 1) {
				return 1;
			}
		}
		free(s);
	}

	fd = open ("/proc/modules", O_RDONLY);
	if (fd < 0)
		return -1;
	
	while ((len = iread (fd, (unsigned char *)buffer, sizeof(buffer)))) {
		char *start=&buffer[0];
		buffer[len] = '\0';
		
		/* next space is the end of module name */
		p = strchr (start, ' ');
		while (p) {
			*p = '\0';
			
			/* match_module() is a strcmp, except that
			   '-' and '_' are equivalent */
			if (match_module(name, start) == 0) {
				close (fd);
				return 1;
			}
			
			/* goto next newline */
			if (! (*(p+1))) break;
			p = strchr (p+1, '\n');
			if (! (*(p+1))) break;
			start = p+1;
			
			/* next space is the end of module name */
			p = strchr (start, ' ');
		}
	}

	close (fd);
	return 0;
}

/* check if a system is HyperV */
/* return 1 = hyperv, 0 = no hyperv, -1 = can't tell */
int 
is_hyperv (void)
{
	char *buf = NULL;

	buf = get_dmi_data("sys_vendor", buffer, 255);
	if (buf && match_string_nocase("Microsoft Corporation", buf) == 0) {
		buf = get_dmi_data("product_name", buffer, 255);
		if (buf && match_string_nocase("Virtual Machine", buf) == 0) {
			return 1;
		}
	}

	return 0;
}

/* check if this is a system with unstable USB xhci_hcd implementation */
/* currently only looking for DMI match, TODO find better indicator */
int
needs_xhci_workaround (void)
{
	char *buf = NULL;

	buf = get_dmi_data("sys_vendor", buffer, 255);
	if (buf && match_string_nocase("ONYX Healthcare Inc.", buf) == 0) {
		buf = get_dmi_data("product_name", buffer, 255);
		if (buf && match_string_nocase("Venus-222", buf) == 0) {
			return 1;
		}
	}

	return 0;
}

void 
load_kernel_module(init_t *init, const char *name)
{
	int err;
	
	if (kmodule_already_loaded(init, name)==1) return;
	
	msg(init,LOG_INFO,"Loading %s module\n",name);
	err = modprobe_cmd(name);
	if (err != 0)
		msg(init,LOG_ERR,"Loading %s failed\n",name);
}

static void
load_kernel_modules(init_t *init)
{
	int err;
	struct stat st;
	FILE *f;
	char line[256];
	uint8_t load_squashfs = 1;
	
	if (stat("/sbin/depmod", &st) == 0) {
		msg(init,LOG_NOTICE," * reconfigure kernel module dependencies\n");
		err = system("/sbin/depmod -ae");
	}
	
	load_alias_modules(init, "pci");
	load_alias_modules(init, "acpi");
	load_alias_modules(init, "usb");
	
	load_kernel_module(init, "overlay");

	if (stat("/proc/filesystems", &st) == 0) {
		f = fopen("/proc/filesystems", "r");
		if (f) {
			while (fgets(line, 255, f) != NULL)
			{
				if (strstr(line, "squashfs")) {
					load_squashfs = 0;
					break;
				}
			}
			fclose(f);
		}
	}
	if (load_squashfs)
		load_kernel_module(init,"squashfs");

	/* load usb-storage in any case */

	if (kmodule_already_loaded(init, "usb-storage") == 0 ){
	    load_kernel_module(init,"uas");
	    load_kernel_module(init,"usb-storage");
	}

}

void
start_rescue_shell(init_t *init)
{
	int err;
	struct stat st;
	
	/* on small initramfs systems we do not have /bin/sh,
	   so we can not start a rescue_shell there */
	if (stat("/bin/sh",&st) != 0) return;
	
	/* load usb keyboard driver and start rescue shell */
	msg(init,LOG_INFO,"Loading hid modules\n");
	err = modprobe_cmd("usbhid");
	if (err != 0)
		msg(init,LOG_ERR,"Loading usbhid failed\n");
	err = modprobe_cmd("hid-generic");
	if (err != 0)
		msg(init,LOG_ERR,"Loading hid-generic failed\n");
	
	snprintf(buffer,sizeof(buffer),"rescue_shell %s /bin/sh -i", 
						init->current_console);
	buffer[sizeof(buffer)-1] = '\0';
	
	err = system(buffer);
}

/* parse kernel cmdline */
static int parse_cmdline(init_t *init)
{
	char *p, *buf;

	init->splash = 0;
	init->failsafe = 0;
	init->no_major_update = 0;
	init->isopartnum = 0;
	init->ram_install = 0;
	init->osc_external_firmware = 0;
	init->osc_partnum = 0;
	init->firmware_partnum = 0;
	init->osc_unattended = 0;
	init->sys_minor = 1;

	if (init->isofilename) {
		free (init->isofilename);
		init->isofilename = NULL;
	}
	if (init->osc_path) {
		free (init->osc_path);
		init->osc_path = NULL;
	}
	if (init->firmware_path) {
		free (init->firmware_path);
		init->firmware_path = NULL;
	}
	/* proc cmdline is limited to 2k so the 4k buffer should be more then sufficiant */

	buf = read_file(2048, buffer, sizeof(buffer), "/proc/cmdline");

	if (buf == NULL)
		return -1;

	if (strlen(buf) + 1 < sizeof(buffer)) {
		buf[strlen(buf)+1] = '\0';
		buf[strlen(buf)] = ' ';
	}

	if (strstr (buf,"igel_syslog=verbose")) {
		init->verbose = 1;
	}
	if (strstr (buffer,"igel_syslog=emergency")) {
		init->no_major_update = 1;
	}
	if (strstr (buffer,"igel_syslog=resetdefaults")) {
		init->no_major_update = 1;
	}
	if (strstr (buf,"splash=0 ") == NULL && strstr (buf,"splash=") != NULL) {
		init->splash = 1;
	}
	if (strstr (buf,"osc_unattended=true")) {
		init->osc_unattended = 1;
	}
	if (strstr (buf,"to_ram")) {
		init->ram_install = 1;
	}
	if (strstr (buf,"osc_url=") && (! strstr (buf,"osc_url=file://"))) {
		init->osc_external_firmware = 1;
	}
	if ((p=strstr (buf,"bootversion="))) {
		sscanf(p,"bootversion=%d ",&init->bootversion);
	}
	if ((p=strstr (buf,"isofilename="))) {
		if (init->isofilename) free (init->isofilename);
		init->isofilename = malloc(255);
		memset(init->isofilename, 0, 255);
		sscanf(p,"isofilename=%s ", init->isofilename);
	}
	if ((p=strstr (buf,"osc_path="))) {
		if (init->osc_path) free (init->osc_path);
		init->osc_path = malloc(255);
		memset(init->osc_path, 0, 255);
		sscanf(p,"osc_path=%s ", init->osc_path);
	}
	if ((p=strstr (buf,"firmware_path="))) {
		if (init->firmware_path) free (init->firmware_path);
		init->firmware_path = malloc(255);
		memset(init->firmware_path, 0, 255);
		sscanf(p,"firmware_path=%s ", init->firmware_path);
	}
	if ((p=strstr (buf,"isopartnum="))) {
		sscanf(p,"isopartnum=%d ",&init->isopartnum);
	} else {
		init->isopartnum = 0;
	}
	if ((p=strstr (buf,"osc_partnum="))) {
		sscanf(p,"osc_partnum=%d ",&init->osc_partnum);
	} else {
		init->osc_partnum = 0;
	}
	if ((p=strstr (buf,"firmware_partnum="))) {
		sscanf(p,"firmware_partnum=%d ",&init->firmware_partnum);
	} else {
		init->firmware_partnum = 0;
	}
	if ((p=strstr (buf,"sys_minor="))) {
		sscanf(p,"sys_minor=%d ",&init->sys_minor);
	} else {
		init->sys_minor = 1;
	}
	if ((p=strstr (buf,"boot_id="))) {
		if (init->boot_id) free (init->boot_id);
		init->boot_id = malloc(255);
		init->boot_id[0] = '\0';
		sscanf(p,"boot_id=%s ", init->boot_id);
	}
	else
		init->boot_id = NULL;
	if ((p=strstr (buf,"init="))) {
		if (init->initcmd) free (init->initcmd);
		init->initcmd=malloc(255);
		init->initcmd[0]='\0';
		sscanf(p,"init=%s %d ",init->initcmd,&init->runlevel);
	}
	if (strstr (buf, "failsafe")) {
		init->failsafe = 1;
		init->no_major_update = 1;
	}
	if (strstr (buf, "igelcmd")) {
		init->boot_type = BOOT_WINLINUX;
	}

	return 0;
}

/* Check if there is a IGEL start header on a given offset on the IGF_BOOT_NAME device
 *
 * returns 0 if valid
 */

static int check_if_igel_part(uint64_t offset)
{
	int fd;
	char bootreg_ident[18];
	char dir_ident[5];

	fd = open(IGF_BOOT_NAME, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	bootreg_ident[17] = '\0';
	dir_ident[4] = '\0';

	/* Read bootreg ident */

	if (ipread(fd, (unsigned char *)bootreg_ident, 17, offset + IGEL_BOOTREG_OFFSET) != 17) {
		close(fd);
		return 1;
	}

	/* Read partiton directory magic/ident */

	if (ipread(fd, (unsigned char *)dir_ident, 4, offset + DIR_OFFSET) != 4) {
		close(fd);
		return 1;
	}

	close(fd);

	/* Check if bootreg ident and directory ident are valid */

	if (strncmp(bootreg_ident, BOOTREG_IDENT, 17) != 0 || strncmp(dir_ident, "PDIR", 17) != 0) {
		return 2;
	}

	return 0;
}

/* check for OSC Token/CD boot */
static void 
check_boot_type(init_t *init)
{
	if (! init->boot_id) return;
	
	if (strncmp(init->boot_id,"IGEL_OSC_",9) != 0)
		return;
	
	if (!strncmp(init->boot_id,"IGEL_OSC_TO",11)) {
		msg(init,LOG_NOTICE," * booting from IGEL OSC Token\n");
		init->boot_type = BOOT_OSC_TOKEN;
		/* prepare mountpoints */
		mkdir("/token", 0755);
	}
	else if (!strncmp(init->boot_id,"IGEL_OSC_PXE",12)) {
		msg(init,LOG_NOTICE," * booting IGEL OSC via PXE\n");
		init->boot_type = BOOT_OSC_PXE;
	}
	if (init->boot_type > BOOT_STANDARD) {
		if(init->boot_type == BOOT_WINLINUX){
			/* no isofs needed for winlinux */
			return;
		}
		/* for OSC Token iso image and OSC CD we need isofs */
		load_kernel_module(init, "isofs");
		
		/* prepare mountpoints */
		mkdir("/cdrom", 0755);
		mkdir("/igfimage", 0755);
	}
}

static void
get_present_igfs(int *igf_array, int *count_igf)
{
	struct stat st;
	char buf[255];
	int i = 0, c = 0;

	for (i=1;i<=255;i++) {
		snprintf(buf, 255, "/dev/igf%d", i);
		buf[254] = '\0';
	
		if (stat(buf,&st)==0) {
			igf_array[c] = i;
			c++;
		}
	}

	*count_igf = c;
}

/* get sizes and start positions of all (1 - MAX_PART_NUM) partitions on the igf major device */

static int
get_igfdisk_partition_data(init_t *init)
{
	char *buf;
	int i;
	unsigned long long sector = 512;
	long long value;

	buf = get_block_data(init->devname, "logical_block_size", buffer, 255);
	if (buf != NULL) {
		value = strtoll(buf, NULL, 10);
		if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
			sector = 512;
		} else {
			sector = (unsigned long long) value;
		}
	}
	buf = get_block_data(init->devname, "size", buffer, 255);
	if (buf != NULL) {
		value = strtoll(buf, NULL, 10);
		if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
			init->devsize = 0;
			return 1;
		} else {
			init->devsize = (uint64_t) (value * sector);
		}
	}

	for (i=1;i<=MAX_PART_NUM;i++) {
		buf = get_block_partition_data(init->devname, i, "size", buffer, 255);
		if (buf != NULL) {
			value = strtoll(buf, NULL, 10);
			if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
				init->part_size[i-1] = 0;
				init->part_start[i-1] = 0;
				continue;
			} else {
				init->part_size[i-1] = (uint64_t) (value * sector);
			}
		} else {
			init->part_size[i-1] = 0;
			init->part_start[i-1] = 0;
			continue;
		}
		buf = get_block_partition_data(init->devname, i, "start", buffer, 255);
		if (buf != NULL) {
			value = strtoll(buf, NULL, 10);
			if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
				init->part_size[i-1] = 0;
				init->part_start[i-1] = 0;
				continue;
			} else {
				init->part_start[i-1] = (uint64_t) (value * sector);
			}
		} else {
			init->part_size[i-1] = 0;
			init->part_start[i-1] = 0;
			continue;
		}
	}

	return 0;
}

/* function returns size of igf device in bytes */

static uint64_t
get_igf_size(const char *dev_name, int igf_num)
{
	unsigned long long size = 0, sector = 512;
	char *buf;
	long long value;

	buf = get_block_data_printf("size", buffer, 255, "%s%d", dev_name, igf_num);
	if (buf != NULL) {
		value = strtoll(buf, NULL, 10);
		if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
			return 0;
		} else {
			size = (unsigned long long) value;
		}
	}

	buf = get_block_data_printf("logical_block_size", buffer, 255, "%s%d", dev_name, igf_num);
	if (buf != NULL) {
		value = strtoll(buf, NULL, 10);
		if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
			sector = 512;
		} else {
			sector = (unsigned long long) value;
		}
	}

	if (size > 0) return (uint64_t) (size * sector);
	
	return 0;
}

static int
check_igfsys_size(init_t *init)
{
	uint64_t size;

	size = get_igf_size("igf", init->sys_minor);
	if (size == 0) {
		usleep(WAIT_TIME);
		init->try++;
		size = get_igf_size("igf", init->sys_minor);
	}
	
	if (size > 0) return 1;
	
	return 0;
}

static int
check_igmsys_size(init_t *init)
{
	uint64_t size;

	size = get_igf_size("igm", init->sys_minor);
	if (size == 0) {
		usleep(WAIT_TIME);
		init->try++;
		size = get_igf_size("igm", init->sys_minor);
	}
	
	if (size > 0) return 1;
	
	return 0;
}

int mount_fs(init_t *init, const char *fallback_fstype, unsigned long mountflags, const char *device, const char* format, ...)
{
	va_list list;
	char *mntpoint;
	const char *fstype = NULL;
	int err = 0;

	if (access(device, R_OK) != 0) {
		msg(init,LOG_ERR,"mount_fs: Given device %s does not exists.\n", device);
		return (-1);
	}

	msg(init,LOG_NOTICE,"Detecting the fs type of %s ", device);
	fstype = fstype_of("%s", device);
	if (!fstype) {
		msg(init,LOG_ERR,"failed...\n");
		if (fallback_fstype != NULL) {
			msg(init,LOG_NOTICE,"Using fallback fstype %s instead.\n", fallback_fstype);
			fstype = fallback_fstype;
		} else {
			return (-3);
		}
	}
	msg(init,LOG_NOTICE,"done...\n");
	if (strcasecmp(fstype, "ntfs") == 0 && kmodule_already_loaded(init, "ntfs") != 1 ) {
		msg(init,LOG_NOTICE, " * loading ntfs kernel module ");
		load_kernel_module(init,"ntfs");
		if (kmodule_already_loaded(init, "ntfs") != 1 ) {
			msg(init,LOG_ERR,"failed...\n");
		} else {
			msg(init,LOG_NOTICE,"done...\n");
		}
	}

	va_start(list, format);
	if (vasprintf(&mntpoint, format, list) < 0) {
		va_end(list);
		return (-2);
	}
	va_end(list);

	if (access(mntpoint, R_OK) != 0)
		mkdir(mntpoint, 0777);

	msg(init, LOG_NOTICE, "Mounting device %s with fstype %s to %s ", device, fstype, mntpoint);
	err = mount(device, mntpoint, fstype, mountflags, NULL);
	if (err) {
		free(mntpoint);
		msg(init,LOG_ERR,"failed...\n");
		return (-4);
	}
	free(mntpoint);
	msg(init,LOG_NOTICE,"done...\n");

	return 0;
}

static int
copy_file(const char *src_filename, const char *dest_filename)
{
	int src_fd;
	int dest_fd;
	char *buf;
	int s;
	int n;
	size_t len;
	struct stat st;
	int err = 0;
	
	src_fd = open(src_filename, O_RDONLY);
	if (src_fd < 0) {
		return (-1);
	}

	err = fstat(src_fd, &st);

	dest_fd = open(dest_filename, O_WRONLY|O_CREAT|O_SYNC, 0644);
	if (dest_fd < 0) {
		close(src_fd);
		return (-1);
	}

	n = 0;
	s = 1048576;
	buf = (char *) malloc(s);

	do {
		n = iread(src_fd, (unsigned char *)buf, s);
		if (n > 0) {
			len = iwrite(dest_fd, (unsigned char *)buf, n);
			if (len < n)
				return (-1);
		} else if (n < 0) {
			return (-1);
		}
	} while (n > 0);

	if (err == 0) {
		fchmod(dest_fd, st.st_mode);
		err = fchown(dest_fd, st.st_uid, st.st_gid);
	}

	free(buf);
	close(dest_fd);
	close(src_fd);

	return (0);
}

static int
move_file(const char *src_filename, const char *dest_filename)
{
	int src_fd;
	int dest_fd;
	char *buf;
	int s;
	off_t n = 0;
	size_t len;
	struct stat st;
	int err = 0;
	off_t pos = 0;
	
	src_fd = open(src_filename, O_RDWR|O_SYNC);
	if (src_fd < 0) {
		return (-1);
	}

	err = fstat(src_fd, &st);

	dest_fd = open(dest_filename, O_WRONLY|O_CREAT|O_SYNC, 0644);
	if (dest_fd < 0) {
		close(src_fd);
		return (-1);
	}

	s = 1048576;
	buf = (char *) malloc(s);
	pos = st.st_size;
	
	do {
		if (pos - s < 0)
			s = pos;
		pos -= s;
		lseek(src_fd, pos, SEEK_SET);
		lseek(dest_fd, pos, SEEK_SET);
		n = iread(src_fd, (unsigned char *)buf, s);
		if (n > 0) {
			len = iwrite(dest_fd, (unsigned char *)buf, n);
			if (len < n)
				return (-1);
		} else if (n < 0) {
			return (-1);
		}
		err = ftruncate(src_fd, pos);
	} while (n > 0 && pos > 0);

	if (err == 0) {
		fchmod(dest_fd, st.st_mode);
		err = fchown(dest_fd, st.st_uid, st.st_gid);
	}

	free(buf);
	close(dest_fd);
	close(src_fd);

	return (0);
}

/* copy all files and directory in src_dir to trg_dir */
static int copy_files_and_dirs(const char * src_dir, const char * trg_dir){

	DIR *dp = opendir(src_dir);
	struct dirent *ep;
	char src[PATH_MAX], trg[PATH_MAX], lnk[PATH_MAX];
	struct stat st;
	int count;

	if (NULL == dp) return -1;
	while((ep = readdir (dp))){
		if (ep->d_name[0] == '.' 
		&& ( ep->d_name[1] == '.' || ep->d_name[1] == '\0' ))
			continue;

		snprintf(src,PATH_MAX,"%s/%s",src_dir,ep->d_name);
		src[PATH_MAX-1] = '\0';

		if (lstat(src, &st))
			continue;

		snprintf(trg,PATH_MAX,"%s/%s",trg_dir,ep->d_name);
		trg[PATH_MAX-1] = '\0';

		if (S_ISLNK(st.st_mode)) {
			count = readlink(src, lnk, PATH_MAX);
			if (count > 0) {
				lnk[count] = '\0';
				if (symlink(lnk, trg) != 0)
					copy_file(src,trg);
			} else {
				copy_file(src,trg);
			}
		} else if (S_ISDIR(st.st_mode)) {
			if (lstat(trg, &st) != 0) {
				mkdir(trg, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH );
				copy_files_and_dirs((const char *)src, (const char *)  trg);
			} else if (S_ISDIR(st.st_mode)) {
				copy_files_and_dirs((const char *)src, (const char *)  trg);
			}
		} else {
			copy_file(src,trg);
		}
	}
	closedir(dp);
	return 0;
}

/* copy all files in src_dir to trg_dir */
static int copy_files(const char * src_dir, const char * trg_dir){

	DIR *dp;
	struct dirent *ep;
	char src[258],trg[258];	

	dp = opendir(src_dir);
	if (NULL == dp) return -1;
	while((ep = readdir (dp))){
		snprintf(src,258,"%s/%s",src_dir,ep->d_name);
		snprintf(trg,258,"%s/%s",trg_dir,ep->d_name);
		src[257] = '\0';
		trg[257] = '\0';
		copy_file(src,trg);
	}
	closedir(dp);
	return 0;
}

static int 
loop_device_unset(const char *device)
{
	return loopdev_delete_device(device);
}

static int 
loop_device_set(const char *device, const char *filename, mode_t mode)
{
	return loopdev_setup_device(filename, device, mode, 0, 0);
}


static char *
loop_device_get_free(init_t *init, char *loop_device)
{
	int loop;

	if (!loop_device)
		return NULL;

	loop = get_free_loopdev_num();

	if (loop < 0 || loop >= 10)
		return NULL;

	snprintf(loop_device, 16, "/dev/loop%d", loop);

	return loop_device;
}

static int
create_loop_device(const char *filename, char *loop_device, uint64_t offset, uint64_t size)
{
	int err;
	mode_t mode = O_RDWR;
	char *device;

	device = loop_device_get_free(NULL, loop_device);
        if (NULL == device) {
                return (-1);
        }

	err = loopdev_setup_device(filename, loop_device, mode, offset, size);
	if (err) {
		return (err);
	}

	return 0;
}

static int
mount_loop_device(init_t *init,
	const char *filename,
	char *loop_device,
	const char *mountpoint, 
	const char *fstype, 
	mode_t mode, 
	const char *options)
{
	int err;
	unsigned long flags;
	char *device;
	
	device = loop_device_get_free(init, loop_device);
	if (NULL == device) {
		msg(init,LOG_ERR,"No free loop device\n");
		return (-1);
	}

	err = loop_device_set(loop_device, filename, mode);
	if (err) {
		msg(init,LOG_ERR,"loop_device_set(dev: %s, file: %s, mode %u) failed (%s)\n",
			loop_device, filename, mode, strerror(errno));
		return (-1);
	}

	flags = (O_RDONLY == mode)? MS_RDONLY: 0;
	err = mount(loop_device, mountpoint, fstype, flags, options);
	if (err) {
		err = mount_fs(init, NULL, flags, loop_device, "%s", mountpoint);
		if (err) {
			msg(init,LOG_ERR,"loop mount failed for %s: %s\n",loop_device, strerror(errno));
			loop_device_unset(loop_device);
		} else {
			msg(init,LOG_INFO, "loop mount failed with fstype %s but auto fs detect worked.\n", fstype);
		}
	}
	
	return (err);
}

static int
umount_loop_device(init_t *init, char *loop_device, const char *mountpoint)
{
	int err;
	
	err = umount(mountpoint);
	loop_device_unset(loop_device);
	return(err);
}

static int
create_igel_device(init_t *init, char *sysfs_blockdev, const char *newname)
{
	char *buf;
	int err;

	buf = get_sysfs_entry(buffer, 255, "%s", sysfs_blockdev);
	if (buf == NULL)
		return 0;
	
	if (sscanf(buf,"%d:%d",&init->dev.major,&init->dev.minor)!=2) {
		return 0;
	}
	
	if (device_in_blacklist(&(init->dev))) {
		msg(init,LOG_INFO,"init: do not use %d:%d (failed already)\n",
					init->dev.major,init->dev.minor);
		return 0;
	}
	
	unlink(newname);
	if (mknod(newname, S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 
	    makedev(init->dev.major, init->dev.minor)) != 0) {
		msg(init,LOG_ERR,"init: can not create node %s (%d:%d):\n",
				 newname,init->dev.major,init->dev.minor);
		msg(init,LOG_ERR,"init: %s\n", strerror(errno));
		/* give the user time for reading error messages */
		sleep (ERROR_TIME);
		return 0;
	}

	/* use same group as for igf... devices */

	err = chown(newname, 0, 6);

	return 1;
}

static int
create_igfsys_device(init_t *init)
{
	char name[255];

	snprintf(name, sizeof(name), "/sys/block/igf%u/dev", init->sys_minor);
	name[sizeof(name)-1] = '\0';
	if (access(name, R_OK) != 0) {
		name[0] = '\0';
		return (0);
	}
	if (! create_igel_device(init, name, IGF_SYS_NAME)) {
		return (0);
	}
	return (1);
}

static int
create_igmsys_device(init_t *init) {
	char name[255];

	snprintf(name, sizeof(name), "/sys/block/igm%u/dev", init->sys_minor);
	name[sizeof(name)-1] = '\0';
	if (access(name, R_OK) != 0) {
		name[0] = '\0';
		return (0);
	}
	if (! create_igel_device(init, name, IGM_SYS_NAME)) {
		return (0);
	}
	return (1);
}

static int
load_igel_flash_driver(init_t *init, const char *filename)
{
	struct mod_opt_t opts, opts2, opts3, opts4;
	int err;
	
	/* insmod igel driver */
	snprintf(buffer,sizeof(buffer),"file=%s",filename);
	opts.m_opt_val = strdup(buffer);
	opts.m_next = NULL;
	if (init->failsafe) {
		opts.m_next = &opts2;
		opts2.m_opt_val = (char *) "failsafe=1";
		opts2.m_next = &opts3;
		opts3.m_opt_val = (char *) "crc_check=1";
		if (init->sys_minor == 1) {
			opts3.m_next = NULL;
		} else {
			snprintf(buffer,sizeof(buffer),"sys_minor=%d", init->sys_minor);
			opts3.m_next = &opts4;
			opts4.m_opt_val = strdup(buffer);
			opts4.m_next = NULL;
		}
	} else {
		if (init->sys_minor != 1) {
			snprintf(buffer,sizeof(buffer),"sys_minor=%d", init->sys_minor);
			opts.m_next = &opts4;
			opts4.m_opt_val = strdup(buffer);
			opts4.m_next = NULL;
		}
	}
	
	snprintf(buffer,sizeof(buffer),
		 "/%s/kernel/drivers/block/igel/igel-flash.ko", init->moddir);
	
	err = insmod_cmd(buffer, &opts);
	
	free(opts.m_opt_val);
		
	if (err != 0) {
		beep(err);
		if (err == -EKEYREJECTED) {
			msg(init,LOG_ERR,"ERROR: Invalid signature of SYS partition found abort.\n");
			reboot(LINUX_REBOOT_CMD_HALT);
		}
		msg(init,LOG_ERR,"ERROR: return value is %d\n", err);
		unlink(IGF_BOOT_NAME);
		unlink(IGF_DISK_NAME);
		unlink(IGF_SYS_NAME);
		/* give the user time for reading error messages */
		sleep (10 * ERROR_TIME);
		return (0);
	}
	
	/* check igfsys + igfsys size */
	if (! check_igfsys_size(init)) {
		igel_delete_dev();
		err = rmmod_cmd("igel-flash");
		if (err != 0) {
			/* give the user time for reading error messages */
			sleep (ERROR_TIME);
		}
		unlink(IGF_BOOT_NAME);
		unlink(IGF_DISK_NAME);
		unlink(IGF_SYS_NAME);
		return (0);
	}

	if (! create_igfsys_device(init)) {
		igel_delete_dev();
		err = rmmod_cmd("igel-flash");
		if (err != 0) {
			/* give the user time for reading error messages */
			sleep (ERROR_TIME);
		}
		unlink(IGF_BOOT_NAME);
		unlink(IGF_DISK_NAME);
		unlink(IGF_SYS_NAME);
		return (0);
	}

	if(strncmp(init->devname,"sr",2)== 0){ // CD/DVD
		msg(init,LOG_NOTICE," * found low-level device /dev/%s (%d:%d)\n"
			,init->devname, init->dev.major, init->dev.minor);
	}else{ // USB Token, Harddisk, ...
		msg(init,LOG_NOTICE," * found low-level device /dev/%s%s%d (%d:%d)\n"
			,init->devname, init->part_prefix, init->part, init->dev.major
			,init->dev.minor);
	}
	return (1);
}

static int
load_igel_migrate_flash_driver(init_t *init, const char *filename)
{
	struct mod_opt_t opts;
	int err;
	
	/* insmod igel driver */
	snprintf(buffer,sizeof(buffer),"file=%s",filename);
	opts.m_opt_val = strdup(buffer);
	opts.m_next = NULL;
	
	snprintf(buffer,sizeof(buffer),
		 "/%s/kernel/drivers/block/igel/igel-migrate-flash.ko", init->moddir);
	
	err = insmod_cmd(buffer, &opts);
	
	free(opts.m_opt_val);
		
	if (err != 0) {
		/* give the user time for reading error messages */
		sleep (ERROR_TIME);
		return (0);
	}
	
	/* check igmsys + igmsys size */
	if (! check_igmsys_size(init)) {
		err = rmmod_cmd("igel-migrate-flash");
		if (err != 0) {
			/* give the user time for reading error messages */
			sleep (ERROR_TIME);
		}
		return (0);
	}
	
	if (! create_igmsys_device(init)) {
		err = rmmod_cmd("igel-migrate-flash");
		if (err != 0) {
			/* give the user time for reading error messages */
			sleep (ERROR_TIME);
		}
		return (0);
	}

	return (1);
}
static int
check_igel_standard_device(init_t *init)
{
	char name[PATH_SIZE], used_offset[128];
	int err;
	char *boot_id1, *boot_id2, *p;
	int part = 1, found = 0;
    	bootreg_data* hndl = NULL;

	snprintf(name, sizeof(name), "/sys/block/%s/dev", init->devname);
	name[sizeof(name)-1] = '\0';
	
	if (! create_igel_device(init, name, IGF_BOOT_NAME))
		return 0;

	init->use_backports = 0;
	init->igel_poffset = 0;

	/* check 1st and following partitions up to MAX_PART_NUM */
	for (part=1; part<= MAX_PART_NUM && found == 0; part++) {

		snprintf(name, sizeof(name), "/sys/block/%s/%s%s%d/dev", 
			 init->devname, init->devname, init->part_prefix, part);

		name[sizeof(name)-1] = '\0';

		/* if there is no sys entry there also is no partition so try next */
		if (access(name, R_OK) != 0)
			continue;
		
		if (! create_igel_device(init, name, IGF_DISK_NAME)) {
			unlink(IGF_BOOT_NAME);
			return 0;
		}

		if (hndl) {
	        	bootreg_deinit(&hndl);
			hndl = NULL;
		}
		
		hndl = bootreg_init(IGF_DISK_NAME, BOOTREG_RDONLY, BOOTREG_LOG_NONE);
		if(!hndl)
		{
			continue;
		}

		/*
		 * Check whether there is a boot_id available:
		 *   1. In the kernel commandline
		 *   2. In the boot registry of the 1st partition (IGF_DISK_NAME)
		 * If both exist, accept the device only, if they match,
		 * if only one boot_id exists, don't accept,
		 * and if neither boot_id exists, accept !
		 */
		if ((boot_id1 = init->boot_id) != NULL) /* from kernel cmdline */
		{ 
			msg(init, LOG_INFO, "init: boot id from cmdline: %s\n",
			    boot_id1);
			bootreg_get(hndl, "boot_id", &boot_id2);
			msg(init, LOG_INFO, "init: boot id from %s: %s\n",
			    IGF_DISK_NAME, (boot_id2) ? boot_id2 : "NULL");

			if (boot_id2 == NULL) {
				continue;	/* don't accept */
			}
			if (strcmp(boot_id1, boot_id2) != 0) {
				free(boot_id2);
				continue;	/* no match: don't accept */
			}
			free(boot_id2);
			boot_id2 = NULL;
			found = part;
			break;
		}
		else /* no boot_id from kernel cmdline */
		{
			msg(init, LOG_INFO, "init: boot id from cmdline: NULL\n");
			bootreg_get(hndl, "boot_id", &p);
			if (p != NULL)
			{
				free(p);
				p = NULL;
				msg(init, LOG_INFO, "init: boot id from %s: not NULL\n",
				    IGF_DISK_NAME);
				continue;	/* don't accept */
			}
			found = part;
			break;
		}
	}

	/* get backports usage setting from bootreg if present */

	if (found != 0) {
		bootreg_get(hndl, "use_backports", &p);
		if (p != NULL) {
			if (strcmp(p, "true") == 0) {
				init->use_backports = 1;
			} else if (strcmp(p, "false") == 0) {
				init->use_backports = -1;
			}
		}
		bootreg_get(hndl, "igel_poffset", &p);
		if (p != NULL) {
			unsigned long long value = 0;
			value = strtoull(p, NULL, 10);
			if ((value == ULLONG_MAX) && (errno == ERANGE)) {
				value = 0;
			}
			init->igel_poffset = value;
		}
	}
        /*cleanup handler*/
	if (hndl)
	        bootreg_deinit(&hndl);

	if (found > 0 && init->igel_poffset > 0) {
		hndl = bootreg_init(IGF_DISK_NAME, BOOTREG_RDWR, BOOTREG_LOG_NONE);
		if (hndl) {
			/* only one try so delete the bootreg entry here */
			bootreg_set(hndl, "igel_poffset", "0");
	        	bootreg_deinit(&hndl);
		}

		if (check_if_igel_part(init->igel_poffset) == 0) {
			msg(init, LOG_INFO, "init: igel poffset %llu seems to be valid\n", (unsigned long long)init->igel_poffset);
		} else {
			msg(init, LOG_INFO, "init: igel poffset %llu is not valid\n", (unsigned long long)init->igel_poffset);
			init->igel_poffset = 0;
		}
	} else {
		init->igel_poffset = 0;
	}

	if (init->igel_poffset > 0) {
		char loopdev[17];
		int64_t newsize = init->part_start[found - 1] + init->part_size[found - 1] - init->igel_poffset;

		if (newsize > 0) {
			msg(init, LOG_INFO, "init: new size with igel poffset is %lld\n", (long long)newsize);
		} else {
			newsize = 0;
		}
		err = create_loop_device(IGF_BOOT_NAME, loopdev, init->igel_poffset, (uint64_t) newsize);
		if (err == 0) {
			hndl = bootreg_init(loopdev, BOOTREG_RDONLY, BOOTREG_LOG_NONE);
			if(hndl)
			{
				boot_id1 = init->boot_id;
				bootreg_get(hndl, "boot_id", &boot_id2);
				if (boot_id2 == NULL) {
					msg(init, LOG_INFO, "init: No boot_id found on given igel offset\n");
					init->igel_poffset = 0;
				} else if (strcmp(boot_id1, boot_id2) != 0) {
					msg(init, LOG_INFO, "init: Wrong boot_id (found: %s expected: %s) found on given igel offset\n", boot_id2, boot_id1);
					init->igel_poffset = 0;
				}
	        		bootreg_deinit(&hndl);
			} else {
				init->igel_poffset = 0;
			}
			if (init->igel_poffset > 0) {
				if (load_igel_flash_driver(init, loopdev)) {
					int fd;
					int len_to_w = 0;
					unlink(IGF_DISK_NAME);
					err = symlink(loopdev, IGF_DISK_NAME);
					msg(init, LOG_INFO, "init: Flash driver load with igel offset loop dev was successful\n");
					len_to_w = snprintf(used_offset, 128, "%llu", (unsigned long long) init->igel_poffset);
					fd = open("/dev/igel_used_offset", O_WRONLY|O_CREAT, 0644);
					if (fd >= 0) {
						iwrite(fd,(unsigned char *)used_offset, len_to_w);
						close(fd);
					}
					if (init->verbose && (access("/initramfs_debug_lx", R_OK)==0))
						start_rescue_shell(init);
					return 1;
				}
				msg(init, LOG_INFO, "init: Flash driver load with igel offset loop dev failed\n");
				if (init->verbose && (access("/initramfs_debug_lx", R_OK)==0))
					start_rescue_shell(init);

			}
			loop_device_unset(loopdev);
		} else {
			msg(init, LOG_INFO, "init: Could not create loopdev with offset for migration with err: %d\n", err);
		}
	}

	if (init->verbose && (access("/initramfs_debug_lx", R_OK)==0))
		start_rescue_shell(init);

	if (found == 0)
		return(0);

	init->part = found;

	/* insmod igel driver */
	err = load_igel_flash_driver(init, IGF_DISK_NAME);

	return(err);
}

static int
check_igel_osc_token(init_t *init)
{
	char name[PATH_SIZE];
	char isofile[PATH_SIZE];
	int err = 0; 
	struct stat st;
	char loop_device[16];
	char iso_loop_device[16];
	long ddimage_size, isofile_size = 0;
	char option[128];
	static int bootsplash_running = 0;
	char *bootsplash_token = NULL;
	int loop_iso_in_use = 0;
	int iso_part = 0;
	int to_ram = 0;
	char *firmware_dir = NULL;
	int firmware_present = 0;
	char *osc_path = NULL;
	int part_del_num = 0;
	int parts_to_del[10];
	struct vendor_list *vendors;

	if (init->osc_unattended) {
		parts_to_del[part_del_num] = 29;
		part_del_num++;
	}

	vendors = malloc(sizeof (struct vendor_list));
	if (vendors) {
		memset(vendors, 0, sizeof (struct vendor_list));
		strncpy(vendors->name, "0x10DE", 7);

		if (find_pci_vendors(vendors) == 0) {
			parts_to_del[part_del_num] = 60;
			part_del_num++;
		}
	}

	memset(iso_loop_device, 0, sizeof(iso_loop_device));
	if (init->isofilename != NULL) {
		int p_start = 1, p_end = 16;
		if (init->isopartnum != 0) {
			p_start = init->isopartnum;
			p_end = init->isopartnum;
		}

		for (int p=1;p<=16;p++) {
			osc_path = NULL;
			snprintf(name, sizeof(name), "/sys/block/%s/%s%s%d/dev", 
				 init->devname, init->devname, init->part_prefix, p);
			name[sizeof(name)-1] = '\0';
			if (access(name, R_OK) != 0) {
				name[0] = '\0';
				continue;
			}
			msg(init,LOG_ERR,"create igel device %s from %s ", ISO_SRC_NAME, name);
			if (! create_igel_device(init, name, ISO_SRC_NAME)) {
				msg(init,LOG_ERR,"failed...\n");
				unlink(ISO_SRC_NAME);
				name[0] = '\0';
				continue;
			}
			msg(init,LOG_ERR,"done...\n");
			err = mount_fs(init, "iso9660", MS_RDONLY, ISO_SRC_NAME, ISO_SRC_PATH);
			if (err) {
				unlink(ISO_SRC_NAME);
				name[0] = '\0';
				usleep(100000);
				continue;
			}
			mkdir(ISO_SRC_PATH, 0755);
			snprintf(isofile, sizeof(isofile), "%s/%s", ISO_SRC_PATH, init->isofilename);
			msg(init,LOG_ERR,"search for ISO file %s ", isofile);
			if (access(isofile, R_OK) != 0) {
				msg(init,LOG_ERR,"failed...\n");
				umount(ISO_SRC_PATH);
				unlink(ISO_SRC_NAME);
				usleep(100000);
				name[0] = '\0';
				continue;
			} else {
				msg(init,LOG_ERR,"done...\n");
				iso_part = p;
				break;
			}
		}
		if (name[0] == '\0') {
			msg(init,LOG_ERR,"No suitable ISO file found on /dev/%s device.\n", init->devname);
			iso_part = 0;
			return (0);
		}

		if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
			start_rescue_shell(init);

		msg(init,LOG_ERR,"mount isofile %s with loop to /token ", isofile);
		err = mount_loop_device(init, isofile, iso_loop_device, "/token", "iso9660", O_RDONLY, NULL);
		if (err != 0) {
			msg(init,LOG_ERR,"failed...\n");
			if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
				start_rescue_shell(init);
			iso_loop_device[0] = '\0';
			umount(ISO_SRC_PATH);
			unlink(ISO_SRC_NAME);
			return (0);
		}
		msg(init,LOG_ERR,"done...\n");
		loop_iso_in_use = 1;
		stat(isofile, &st);
		isofile_size = st.st_size + 10*1024*1024;

		to_ram = 1;
	} else if (init->osc_path != NULL) {
		int p_start = 1, p_end = 16;
		if (init->osc_partnum != 0) {
			p_start = init->osc_partnum;
			p_end = init->osc_partnum;
		}

		for (int p=1;p<=16;p++) {
			snprintf(name, sizeof(name), "/sys/block/%s/%s%s%d/dev", 
				 init->devname, init->devname, init->part_prefix, p);
			name[sizeof(name)-1] = '\0';
			if (access(name, R_OK) != 0) {
				name[0] = '\0';
				continue;
			}
			msg(init,LOG_NOTICE,"create igel device %s from %s ", IGF_DISK_NAME, name);
			if (! create_igel_device(init, name, IGF_DISK_NAME)) {
				msg(init,LOG_ERR,"failed...\n");
				unlink(IGF_DISK_NAME);
				name[0] = '\0';
				continue;
			}
			msg(init,LOG_NOTICE,"done...\n");
			err = mount_fs(init, "iso9660", MS_RDONLY, IGF_DISK_NAME, "/token");
			if (err) {
				unlink(IGF_DISK_NAME);
				name[0] = '\0';
				continue;
			}
			msg(init,LOG_NOTICE,"Check if all needed files are present ");
			if (file_exists("/token/%s/ddimage.bin", init->osc_path) != 1) {
				if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
					start_rescue_shell(init);

				msg(init,LOG_ERR,"failed...\n");
				umount("/token");
				unlink(IGF_DISK_NAME);
				name[0] = '\0';
				continue;
			} else {
				msg(init,LOG_ERR,"done...\n");
				osc_path = init->osc_path;
				break;
			}
		}
		if (name[0] == '\0') {
			msg(init,LOG_ERR,"No suitable OSC path found on /dev/%s device.\n", init->devname);
			return (0);
		}
	} else { 
		/* check 1st partition */
		if(strncmp(init->devname,"sr",2)== 0){ // CD/DVD
			snprintf(name, sizeof(name), "/sys/block/%s/dev", init->devname);
		}else{
			snprintf(name, sizeof(name), "/sys/block/%s/%s%s1/dev", 
						init->devname, init->devname, init->part_prefix);
		}

		name[sizeof(name)-1] = '\0';
		
		if (! create_igel_device(init, name, IGF_DISK_NAME)) {
			unlink(IGF_DISK_NAME);
			return (0);
		}
		
		/* check major */
		switch(init->dev.major){
			case 8:
				msg(init,LOG_NOTICE,"%s: dev.major = %i (USB TOKEN, Harddisk,...)\n",__FUNCTION__, init->dev.major);
				break;
			case 11:
				if (strncmp(init->devname,"sr", 2) == 0){
					msg(init,LOG_NOTICE,"%s: dev.major = %i (CD/DVD)\n",__FUNCTION__, init->dev.major);
				} else {
					unlink(IGF_DISK_NAME);
					msg(init,LOG_NOTICE,"%s: dev.major = %i(unknown -> OUT)\n",__FUNCTION__, init->dev.major);
					return (0);
				}
				break;
			default:
				unlink(IGF_DISK_NAME);
				msg(init,LOG_NOTICE,"%s: dev.major = %i(unknown -> OUT)\n",__FUNCTION__, init->dev.major);
				return (0);
		}

		/* try to mount token to /token */
		err = mount_fs(init, "iso9660", MS_RDONLY, IGF_DISK_NAME, "/token");
		if (err && EBUSY != errno) {
			unlink(IGF_DISK_NAME);
			return (0);
		}
	}

	if (access(IGF_TOKEN_FIRMWARE_DIR, R_OK) == 0) {
		firmware_present = 1;
		firmware_dir = strdup(IGF_TOKEN_FIRMWARE_DIR);
	}

	if (init->osc_external_firmware)
		to_ram = 0;
	else if (init->ram_install)
		to_ram = 1;

	if (to_ram == 1 && init->firmware_path != NULL) {
		if (file_exists("/token/%s", init->firmware_path)) {
			if (firmware_dir) {
				free(firmware_dir);
				firmware_dir = NULL;
			}
			if (asprintf(&firmware_dir,"/token/%s", init->firmware_path) < 0) {
				firmware_dir = NULL;
				firmware_present = 0;
			} else {
				firmware_present = 1;
			}
			unlink(FW_DISK_NAME);
		} else if (file_exists("%s/%s", ISO_SRC_PATH, init->firmware_path)) { 
			if (firmware_dir) {
				free(firmware_dir);
				firmware_dir = NULL;
			}
			if (asprintf(&firmware_dir,"%s/%s", ISO_SRC_PATH, init->firmware_path) < 0) {
				firmware_dir = NULL;
				firmware_present = 0;
			} else {
				firmware_present = 1;
			}
			unlink(FW_DISK_NAME);
		} else {
			int p_start = 1, p_end = 16;
			if (init->osc_partnum != 0) {
				p_start = init->firmware_partnum;
				p_end = init->firmware_partnum;
			}

			for (int p=1;p<=16;p++) {
				snprintf(name, sizeof(name), "/sys/block/%s/%s%s%d/dev", 
					 init->devname, init->devname, init->part_prefix, p);
				name[sizeof(name)-1] = '\0';
				if (access(name, R_OK) != 0) {
					name[0] = '\0';
					continue;
				}
				msg(init,LOG_NOTICE,"create igel device %s from %s ", FW_DISK_NAME, name);
				if (! create_igel_device(init, name, FW_DISK_NAME)) {
					msg(init,LOG_ERR,"failed...\n");
					unlink(FW_DISK_NAME);
					name[0] = '\0';
					continue;
				}
				msg(init,LOG_NOTICE,"done...\n");
				err = mount_fs(init, "ntfs", MS_RDONLY, FW_DISK_NAME, "/firmware");
				if (err) {
					unlink(FW_DISK_NAME);
					name[0] = '\0';
					continue;
				}
				msg(init,LOG_NOTICE,"Check if all needed files are present ");
				if (file_exists("/firmware/%s", init->firmware_path) != 1) {
					msg(init,LOG_ERR,"failed...\n");
					umount("/firmware");
					unlink(FW_DISK_NAME);
					name[0] = '\0';
					continue;
				} else {
					msg(init,LOG_ERR,"done...\n");
					break;
				}
			}
			if (name[0] == '\0') {
				msg(init,LOG_ERR,"No suitable firmware path found on /dev/%s device.\n", init->devname);
				firmware_present = 0;
			} else {
				if (firmware_dir) {
					free(firmware_dir);
					firmware_dir = NULL;
				}
				if (asprintf(&firmware_dir,"/firmware/%s", init->firmware_path) < 0) {
					umount("/firmware");
					unlink(FW_DISK_NAME);
					firmware_present = 0;
					firmware_dir = NULL;
				} else {
					firmware_present = 1;
				}
			}
		}
	}

	if (firmware_present != 1) {
		if (to_ram)
			msg(init,LOG_ERR,"ERROR: Should copy firmware to RAM but no firmware present.\n");
		to_ram = 0;
	}

	if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
		start_rescue_shell(init);

	/* do not check boot_id on ISO here up to now */
	if (loop_iso_in_use == 0 && osc_path == NULL) {
		/* check boot_id */
		snprintf(name, sizeof(name), "/token/.%s", init->boot_id);
		name[sizeof(name)-1] = '\0';
		if (osc_path && stat(name,&st) != 0) {
			snprintf(name, sizeof(name), "/token/%s/.%s", osc_path, init->boot_id);
			name[sizeof(name)-1] = '\0';
		}
		if (stat(name,&st) != 0) {
			if (loop_iso_in_use != 0) {
				umount_loop_device(init, iso_loop_device, "/token");
				err = umount(ISO_SRC_PATH);
				unlink(ISO_SRC_NAME);
			} else {
				umount("/token");
				unlink(IGF_DISK_NAME);
			}
			if (access(FW_DISK_NAME, R_OK) == 0) {
				umount("/firmware");
				unlink(FW_DISK_NAME);
			}
			if (firmware_dir) {
				free(firmware_dir);
				firmware_dir = NULL;
			}
			return (0);
		}
	}

	/* check ddimage file */
	if (osc_path) {
		snprintf(name, sizeof(name), "/token/%s/ddimage.bin", osc_path);
		stat(name,&st);
	} else if (stat(IGF_TOKEN_DD_IMAGE,&st) != 0) {
		if (loop_iso_in_use != 0) {
			umount_loop_device(init, iso_loop_device, "/token");
			err = umount(ISO_SRC_PATH);
                        unlink(ISO_SRC_NAME);
		} else {
			umount("/token");
			unlink(IGF_DISK_NAME);
		}
		if (access(FW_DISK_NAME, R_OK) == 0) {
			umount("/firmware");
			unlink(FW_DISK_NAME);
		}
		if (firmware_dir) {
			free(firmware_dir);
			firmware_dir = NULL;
		}
		return (0);
	}

	/* ddimage_size + 10 MB reserve */
	ddimage_size = st.st_size + 10*1024*1024;

	msg(init, LOG_INFO, "init: found boot image on token.\n");
	
	/* we want to move mountpoint to root partition, so
	   mount it tmpfs */
	/* only use size parameter if ddimage is > 200 MB */

	// TODO check available RAM

	if (to_ram == 0) {
		if (ddimage_size < 200*1024*1204) {
			err  = mount("none", IGF_IMAGE_MOUNTPOINT, "tmpfs", 0, NULL);
		} else {
			snprintf(option, sizeof(option), "size=%ld", ddimage_size);
			err  = mount("none", IGF_IMAGE_MOUNTPOINT, "tmpfs", 0, option);
		}
	} else {
		size_t mem_avail = (size_t) sysconf(_SC_PHYS_PAGES) * (size_t) sysconf(_SC_PAGESIZE);
		if (mem_avail > 0 && mem_avail < isofile_size + 100 * 1024 * 1024) {
			msg(init, LOG_ERR, "Error: Not enough RAM (%lu MiB) for ramdisk (%lu MiB) present.\n",
			mem_avail / 1024 / 1024, isofile_size / 1024 / 1024);
			err = 1;
		} else {
			snprintf(option, sizeof(option), "size=%ld", isofile_size);
			err  = mount("none", IGF_IMAGE_MOUNTPOINT, "tmpfs", 0, option);
		}
	}
	if (err) {
		msg(init, LOG_ERR, 
			"Error: cannot create and mount tmpfs: %s\n", 
			IGF_IMAGE_MOUNTPOINT);
		umount_loop_device(init, loop_device, "/cdrom");
		if (loop_iso_in_use != 0) {
			umount_loop_device(init, iso_loop_device, "/token");
			err = umount(ISO_SRC_PATH);
			unlink(ISO_SRC_NAME);
		} else {
			umount("/token");
			unlink(IGF_DISK_NAME);
		}
		if (access(FW_DISK_NAME, R_OK) == 0) {
			umount("/firmware");
			unlink(FW_DISK_NAME);
		}
		if (firmware_dir) {
			free(firmware_dir);
			firmware_dir = NULL;
		}
		return (0);
	}

	if (stat(IGF_TOKEN_BOOTSPLASH_OSC,&st) == 0) {
		bootsplash_token = strdup(IGF_TOKEN_BOOTSPLASH_OSC);
	} else {
		bootsplash_token = NULL;
	}
	if (bootsplash_token != NULL && init->splash && bootsplash_running == 0) {
		msg(init, LOG_INFO, " * found bootsplash image on token.\n");
		mkdir("/mnt-bootsplash", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH );
		mkdir(IGF_BSPL_CHROOT, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH );
		if (mount_loop_device(init, bootsplash_token, loop_device,
				"/mnt-bootsplash", "squashfs", O_RDONLY, NULL) == 0) {
			msg(init, LOG_INFO, " * copy files from bootsplash to /.\n");
			if (mount("none", IGF_BSPL_CHROOT, "tmpfs", 0, NULL) == 0) {
				copy_files_and_dirs("/mnt-bootsplash",IGF_BSPL_CHROOT);
				umount_loop_device(init, loop_device, "/mnt-bootsplash");
				if (mount("/dev", IGF_BSPL_CHROOT_DEV, "none", MS_BIND, NULL) == 0) {
					if (mount("/proc", IGF_BSPL_CHROOT_PROC, "none", MS_BIND, NULL) == 0) {
						if (mount("/sys", IGF_BSPL_CHROOT_SYS, "none", MS_BIND, NULL) == 0) {
							msg(init, LOG_INFO, " * start bootsplash\n");
							bootsplash_start(init);
							bootsplash_running = 1;
						} else {
							umount(IGF_BSPL_CHROOT_DEV);
							umount(IGF_BSPL_CHROOT_PROC);
						}
					} else {
						umount(IGF_BSPL_CHROOT_DEV);
					}
				}
			}
		} else {
			msg(init, LOG_INFO, " * mount of bootsplash image failed.\n");
		}
	}

	if (bootsplash_token != NULL) {
		free(bootsplash_token);
		bootsplash_token = NULL;
	}

	/* copy ddimage to tmpfs */
	msg(init,LOG_NOTICE," * loading boot image ...\n");
	if (osc_path != NULL) {
		snprintf(name, sizeof(name), "/token/%s/ddimage.bin", osc_path);
	} else {
		snprintf(name, sizeof(name), "%s", IGF_TOKEN_DD_IMAGE);
	}
	if (part_del_num > 0) {
		int src_fd = -1, dest_fd = -1;
		err = 1;
		src_fd = open(name, O_RDONLY);
		if (src_fd >= 0) {
			unlink(IGF_IMAGE_NAME);
			dest_fd = open(IGF_IMAGE_NAME, O_RDWR|O_CREAT, 0644);
			if (dest_fd >= 0) {
				err = 0;
			} else {
				close(src_fd);
			}
		}
		if (err == 0) {
			err = delete_parts(init, src_fd, dest_fd, part_del_num, parts_to_del);
			close(src_fd);
			close(dest_fd);
		}
		if (err != 0) {
			unlink(IGF_IMAGE_NAME);
			err = copy_file(name, IGF_IMAGE_NAME);
		}
	} else {
		err = copy_file(name, IGF_IMAGE_NAME);
	}

	if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
		start_rescue_shell(init);

	if (! err && to_ram && firmware_dir) {
		mkdir(ISO_TRGT_PATH, 0777);
		err = copy_files_and_dirs(firmware_dir, ISO_TRGT_PATH);
		if (err) {
			msg(init, LOG_ERR, "Copying %s to %s failed.\n", firmware_dir, ISO_TRGT_PATH);
			if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
				start_rescue_shell(init);
		}
	}
	if (firmware_dir) {
		free(firmware_dir);
		firmware_dir = NULL;
	}
	if (access(FW_DISK_NAME, R_OK) == 0) {
		umount("/firmware");
		unlink(FW_DISK_NAME);
	}
	if (loop_iso_in_use != 0) {
		umount_loop_device(init, iso_loop_device, "/token");
		umount(ISO_SRC_PATH);
		unlink(ISO_SRC_NAME);
	} else {
		umount_loop_device(init, loop_device, "/cdrom");
		umount("/token");
	}

	if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
		start_rescue_shell(init);

	if (err) {
		unlink(IGF_DISK_NAME);
		umount(IGF_IMAGE_MOUNTPOINT);
		return (0);
	}

	/* UDC case only search 1st partition so set it */

	if (loop_iso_in_use != 0) {
		init->part = iso_part;
	} else {
		init->part = 1;
	}

	/* insmod igel driver */
	msg(init,LOG_ERR,"Loading IGEL Flash driver for %s ", IGF_IMAGE_NAME);
	err = load_igel_flash_driver(init, IGF_IMAGE_NAME);

	/* driver was not loaded successfully so delete IGF_IMAGE_NAME and umount IGF_IMAGE_MOUNTPOINT */
	if (err == 0) {
		msg(init,LOG_ERR,"failed...\n");
		if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
			start_rescue_shell(init);

		unlink(IGF_IMAGE_NAME);
		umount(IGF_IMAGE_MOUNTPOINT);
	}
	msg(init,LOG_ERR,"done...\n");

	/* we do not need IGF_BOOT_NAME and IGF_DISK_NAME
	  if we boot from ddimage */
	unlink(IGF_BOOT_NAME);
	unlink(IGF_DISK_NAME);
	
	return (err);
}

 static int

check_igel_udc_pxe(init_t *init)
{
	int err;
	struct stat st;
	long ddimage_size;
	int part_del_num = 0;
	int parts_to_del[10];
	struct vendor_list *vendors;
	char option[128];

	if (init->osc_unattended) {
		parts_to_del[part_del_num] = 29;
		part_del_num++;
	}

	vendors = malloc(sizeof (struct vendor_list));
	if (vendors) {
		memset(vendors, 0, sizeof (struct vendor_list));
		strncpy(vendors->name, "0x10DE", 7);

		if (find_pci_vendors(vendors) == 0) {
			parts_to_del[part_del_num] = 60;
			part_del_num++;
		}
		free(vendors);
	}
		
	/* check ddimage file */
	if (stat(IGF_PXE_DD_IMAGE,&st) != 0) {
		msg(init,LOG_ERR,"%s was not provided in initrd\n"
							,IGF_PXE_DD_IMAGE);
		return (0);
	}
	/* ddimage_size + 10 MB reserve */
	ddimage_size = st.st_size + 10*1024*1024;

	msg(init, LOG_INFO, "init: found boot image %s.\n",IGF_PXE_DD_IMAGE);

	unlink(IGF_IMAGE_NAME);
	if (access(IGF_IMAGE_MOUNTPOINT, F_OK) != 0)
		mkdir(IGF_IMAGE_MOUNTPOINT, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH );
	else
		umount(IGF_IMAGE_MOUNTPOINT);

	if (ddimage_size < 200*1024*1204) {
		err  = mount("none", IGF_IMAGE_MOUNTPOINT, "tmpfs", 0, NULL);
	} else {
		snprintf(option, sizeof(option), "size=%ld", ddimage_size);
		err  = mount("none", IGF_IMAGE_MOUNTPOINT, "tmpfs", 0, option);
	}

	if (part_del_num > 0) {

		if (err) {
			msg(init, LOG_ERR, 
				"Error: cannot create and mount tmpfs: %s\n", 
				IGF_IMAGE_MOUNTPOINT);
		} else {
			int src_fd = -1, dest_fd = -1;
			err = 1;
			src_fd = open(IGF_PXE_DD_IMAGE, O_RDONLY);
			if (src_fd >= 0) {
				unlink(IGF_IMAGE_NAME);
				dest_fd = open(IGF_IMAGE_NAME, O_RDWR|O_CREAT, 0644);
				if (dest_fd >= 0) {
					err = 0;
				} else {
					close(src_fd);
				}
			}
			if (err == 0) {
				err = delete_parts(init, src_fd, dest_fd, part_del_num, parts_to_del);
				close(src_fd);
				close(dest_fd);
				if (err != 0) {
					err = move_file(IGF_PXE_DD_IMAGE, IGF_IMAGE_NAME);
				}
			}
		}
		if (err != 0) {
                        unlink(IGF_IMAGE_NAME);
			umount(IGF_IMAGE_MOUNTPOINT);
                } else {
			/* insmod igel driver */
	                err = load_igel_flash_driver(init, IGF_IMAGE_NAME);
			if (err == 0) {
                        	unlink(IGF_IMAGE_NAME);
				umount(IGF_IMAGE_MOUNTPOINT);
			} else {
				unlink(IGF_PXE_DD_IMAGE);
			}
		}
	} else {
		if (move_file(IGF_PXE_DD_IMAGE, IGF_IMAGE_NAME) != 0) {
			umount(IGF_IMAGE_MOUNTPOINT);
			err = load_igel_flash_driver(init, IGF_PXE_DD_IMAGE);
		} else {
			/* insmod igel driver */
			err = load_igel_flash_driver(init, IGF_IMAGE_NAME);
			if (err == 0) {
				unlink(IGF_IMAGE_NAME);
				umount(IGF_IMAGE_MOUNTPOINT);
			} else {
				unlink(IGF_PXE_DD_IMAGE);
			}
		}
	}

	/* we do not need IGF_BOOT_NAME and IGF_DISK_NAME
	  if we boot from ddimage */
	unlink(IGF_BOOT_NAME);
	unlink(IGF_DISK_NAME);	

	return (err);
}

static int
check_igel_winlinux(init_t *init){
	struct stat st;
	if (stat(INITRD_IMG,&st) != 0) {
		msg(init,LOG_ERR,"sys part was not provided as initrd\n");
		if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
			start_rescue_shell(init);
		return (0);
	}
	/* insmod igel driver */
	return (load_igel_flash_driver(init, INITRD_IMG));
}

static int
find_igel_device(init_t *init)
{
	DIR *dir;
	struct dirent *dent;
	char *str = NULL;
        bootreg_data* hndl = NULL;

	if (init->devname) {
		free(init->devname);
		init->devname = NULL;
	}
	if (init->part_prefix) {
		free(init->part_prefix);
		init->part_prefix = NULL;
	}

	dir = opendir("/sys/block");
	if (dir != NULL) {
		for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
			init->major_update = 0;
			init->major_update_keep_jre = 0;
			init->major_update_keep_nvidia = 0;
			if (dent->d_name[0] == '.')
				continue;
			if(NULL != strstr(dent->d_name,"ram"))
				continue;
			if(NULL != strstr(dent->d_name,"loop"))
				continue;
			init->devname = strdup(dent->d_name);
			if(strncmp(init->devname,"mmcblk",6)== 0){
				init->part_prefix = strdup("p");
			} else if(strncmp(init->devname,"nvme",4)== 0){
				init->part_prefix = strdup("p");
			} else {
				init->part_prefix = strdup("");
			}
			if (get_igfdisk_partition_data(init) != 0) {
				init->devsize = 0;
				memset(init->part_start, 0, MAX_PART_NUM * sizeof(uint64_t));
				memset(init->part_size, 0, MAX_PART_NUM * sizeof(uint64_t));
			}
			switch (init->boot_type) {
			  case BOOT_STANDARD:
			  	if (check_igel_standard_device(init)) {
					init->found = 1;
					hndl = bootreg_init(IGF_DISK_NAME, BOOTREG_RDWR, BOOTREG_LOG_NONE);
					if (hndl != NULL)
					{
						bootreg_get(hndl, "major_update", &str);
					}
					if (hndl != NULL && str != NULL && str[0] == '1')
					{
						free(str);
						str = NULL;
                                                /* do reset major update flag if no_major_update is set (from parse_cmdline
                                                 * if failsafe boot, emergency boot or resetdefaults was set) */
						if (init->no_major_update != 0)
						{
							msg(init, LOG_ERR, "major update: disabled due to choosen boot mode");
							bootreg_set(hndl, "major_update", "0");
							init->major_update = 0;
						}
						else
						{
							init->major_update = 1;
						}
					}
					if (hndl != NULL && init->major_update == 1)
					{
						bootreg_get(hndl, "major_update_keep_jre", &str);
						if (hndl != NULL && str != NULL && str[0] == '1')
						{
							free(str);
							str = NULL;
							init->major_update_keep_jre = 1;
						} else {
							init->major_update_keep_jre = 0;
						}
						bootreg_get(hndl, "major_update_keep_nvidia", &str);
						if (hndl != NULL && str != NULL && str[0] == '1')
						{
							free(str);
							str = NULL;
							init->major_update_keep_nvidia = 1;
						} else {
							init->major_update_keep_nvidia = 0;
						}
					}
					if (hndl != NULL)
					{
						bootreg_deinit(&hndl);
					}
				}
				break;
			  case BOOT_OSC_TOKEN:
			  	if (check_igel_osc_token(init)) {
					init->found = 1;
				}
				break;
			  case BOOT_OSC_PXE:
				if (check_igel_udc_pxe(init)) {
					init->found = 1;
				}
				break;
			  case BOOT_WINLINUX:
				if (check_igel_winlinux(init)) {
					init->found = 1;
				}
				break;
			}
			if (init->found == 1) {
				closedir(dir);
				return (1);
			}
			free(init->devname);
			init->devname = NULL;
			free(init->part_prefix);
			init->part_prefix = NULL;
			init->devsize = 0;
			memset(init->part_start, 0, MAX_PART_NUM * sizeof(uint64_t));
			memset(init->part_size, 0, MAX_PART_NUM * sizeof(uint64_t));
		}
		closedir(dir);
	}
	
	return (0);
}

static void
find_igel_device_loop(init_t *init)
{
	while (1) {
		if (find_igel_device(init))
			break;
			
		usleep(WAIT_TIME);
		init->try++;
	
		/* check for newly plugged usb devices via module alias */
		if ((init->try % 4) == 0) {
			if (load_alias_modules(init, "usb") == 0)
				msg(init,LOG_NOTICE,
				  " * looking for usb devices (via alias)\n");
		}
		/* check for all available devices via module alias */
		if ((init->try % 11) == 0) {
			if (load_alias_modules(init, "all") == 0)
				msg(init,LOG_NOTICE,
				  " * looking for devices (via module alias)\n");
		}
		/* load usb-storage anyways */
		if ((init->try % 12) == 0) {
			/* ensure usb-storage will be loaded */
			if (kmodule_already_loaded(init, "usb-storage") != 1 ){
				msg(init,LOG_NOTICE,
				  " * loading usb-storage anyways\n");
				load_kernel_module(init,"usb-storage");
			}
			/* ensure nvme will be loaded */
			if (kmodule_already_loaded(init, "nvme") != 1 ){
				msg(init,LOG_NOTICE,
				  " * loading nvme anyways\n");
				load_kernel_module(init,"nvme");
			}
		}


		/* ensure eMMC drivers load also mmc_core and mmc_block */

		if (kmodule_already_loaded(init, "sdhci") == 1 ||
		    kmodule_already_loaded(init, "sdhci-acpi") == 1 ||
		    kmodule_already_loaded(init, "sdhci-pci") == 1 ||
		    kmodule_already_loaded(init, "mmc_core") == 1) {
		    if (kmodule_already_loaded(init, "mmc_core") != 1)
			load_kernel_module(init,"mmc_core");
		    if (kmodule_already_loaded(init, "mmc_block") != 1)
			load_kernel_module(init,"mmc_block");
		}
	
		/* timeout: */
		/* try==4 -> 1 sec, try==120 -> 30 sec */
		if ((init->try % 120) == 0) {
			break;
		}
	}
}

static int
create_sysfs_device(init_t *init, mode_t type, const char *sysfs_path, char *devname,
		    char *devdir, uid_t owner, gid_t group)
{
	char path[PATH_SIZE], *buf;
	int err;
	struct mmdev dev;
	struct stat st;
	
	buf = get_sysfs_entry(buffer, 255, "/%s/%s/dev", sysfs_path, devname);
	if (buf == NULL)
		return 0;
	
	if (devdir)
		snprintf(path, sizeof(path), "/dev/%s/%s", devdir, devname);
	else
		snprintf(path, sizeof(path), "/dev/%s", devname);
	path[sizeof(path)-1] = '\0';
	
	/* if the device node exists, do not change it */
	if (stat(path, &st) == 0) return 1;
	
	if (sscanf(buf,"%d:%d",&dev.major,&dev.minor)!=2)
		return 0;
	if (mknod(path, type | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 
	    makedev(dev.major, dev.minor)) != 0)
		return 0;
	
	err = chown(path,owner,group);
	
	return 1;
}

/* create recursivly block devices:
   look for devices /sys/block/ and its subdirectories */
static void
create_block_devices(init_t *init, const char *base, int depth)
{
	DIR *dir;
	struct dirent *dent;
	char path[PATH_SIZE];
	struct stat st;
	
	if (depth > 2) return;
	
	dir = opendir(base);
	if (dir != NULL) {
		for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
			if (dent->d_name[0] == '.')
				continue;
			
			snprintf(path, sizeof(path), "/%s/%s", base, dent->d_name);
			path[sizeof(path)-1] = '\0';
			
			if ((stat(path, &st)==0) && S_ISDIR(st.st_mode)) {
				
				create_sysfs_device(init, 
						S_IFBLK, base, dent->d_name,
						NULL, /* no devdir */
						0,6);/*uid=root,gid=disk*/
			
				if ((depth == 1) && 
				    (strncmp(dent->d_name,"igf",3) != 0)) {
					depth++;
					create_block_devices(init, path, depth);
				}
			}
		}
		closedir(dir);
	}
}

/* create tty devices:
   look for devices /sys/devices/virtual/tty without subdirectories */
static void
create_tty_devices(init_t *init, const char *base)
{
	DIR *dir;
	struct dirent *dent;
	
	dir = opendir(base);
	if (dir != NULL) {
		for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
			if (dent->d_name[0] == '.')
				continue;
			
			create_sysfs_device(init, S_IFCHR, base, dent->d_name,
					    NULL, /* no devdir */
					    0,100);/*uid=root,gid=users*/
		}
		closedir(dir);
	}
}

/* create virtual devices:
   look for devices /sys/devices/virtual/... without subdirectories */
static void
create_virtual_devices(init_t *init, const char *base)
{
	DIR *dir;
	struct dirent *dent;
	char *devdir = NULL;
	
	if (strstr(base,"/input")) {
		devdir = (char *) "input";
		mkdir("/dev/input", 0755);
	}
	
	dir = opendir(base);
	if (dir != NULL) {
		for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
			if (dent->d_name[0] == '.')
				continue;
			
			
			create_sysfs_device(init, S_IFCHR, base, dent->d_name,
					    devdir,
					    0,0);/*uid=root,gid=root*/
		}
		closedir(dir);
	}
}

#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC		0x858458f6
#endif

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC		0x01021994
#endif

static dev_t rootdev;

static void delete_contents(init_t *init, const char *directory)
{
	DIR *dir;
	struct dirent *d;
	struct stat st;

	/* Don't descend into other filesystems */
	if (lstat(directory,&st) || st.st_dev != rootdev) return;

	/* Recursively delete the contents of directories. */
	if (S_ISDIR(st.st_mode)) {
		if((dir = opendir(directory))) {
			while ((d = readdir(dir))) {
				char *newdir=d->d_name;

				/* Skip . and .. */
				if(*newdir=='.' && 
				   (!newdir[1] || (newdir[1]=='.' && !newdir[2])))
					continue;

				/* Recurse to delete contents */
				newdir = alloca(strlen(directory) + 
					 strlen(d->d_name) + 2);
				sprintf(newdir, "%s/%s", directory, d->d_name);
				delete_contents(init, newdir);
			}
			closedir(dir);

			/* Directory should now be empty.  Zap it. */
			rmdir(directory);
		}
	} else {
		if (strcmp(IGF_PXE_DD_IMAGE, directory) != 0)
			unlink(directory);
	}
}

static int switch_root(init_t *init, const char *newroot, const char *console,
		       char *initcmd, char **initargs)
{
	struct stat st1, st2;
	struct statfs stfs;


	/* Change to new root directory and verify it's a different fs. */
	if (chdir(newroot) || lstat(".", &st1) || lstat("/", &st2) ||
		st1.st_dev == st2.st_dev)
	{
		msg(init,LOG_ERR,"switch_root: wrong new root %s",newroot);
		return (1);
	}
	rootdev=st2.st_dev;

	/* 
	 * Additional sanity checks: we're about to rm -rf /,  so be REALLY SURE
	 * we mean it.  (I could make this a CONFIG option, but I would get email
	 * from all the people who WILL eat their filesystemss.)
	 */
	if (lstat("/init", &st1) || !S_ISREG(st1.st_mode) || statfs("/", &stfs) ||
		(stfs.f_type != RAMFS_MAGIC && stfs.f_type != TMPFS_MAGIC) ||
		getpid() != 1)
	{
		msg(init,LOG_ERR,"switch_root: wrong root fs");
		return (1);
	}

	delete_contents(init, "/");

	/*
	 * Overmount / with newdir and chroot into it.  The chdir is needed to
	 * recalculate "." and ".." links.
	 */
	if (mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/")) {
		msg(init,LOG_ERR,"switch_root: can not move root");
		return (1);
	}
	
	/* If a new console specified, redirect stdin/stdout/stderr to that. */
	if (console) {
		close(0);
		if(open(console, O_RDWR) < 0) {
			msg(init,LOG_ERR,"switch_root: wrong console '%s'",console);
			return (1);
		}
		dup2(0, 1);
		dup2(0, 2);
	}

	/* Exec real init.  (This is why we must be pid 1.) */
	execv(initcmd,initargs);
	
	/* execv should not return */
	msg(init,LOG_ERR,"switch_root: wrong init command '%s'",initcmd);
	return (1);
}

static int
copy_device(const char *src_filename, const char *dest_filename, uint64_t size)
{
	int src_fd;
	int dest_fd;
	char *buf;
	int s;
	int n;
	size_t len;
	struct stat st;
	int err = 0;
	
	src_fd = open(src_filename, O_RDONLY);
	if (src_fd < 0) {
		return (-1);
	}

	err = fstat(src_fd, &st);

	dest_fd = open(dest_filename, O_WRONLY);
	if (dest_fd < 0) {
		close(src_fd);
		return (-1);
	}

	n = 0;
	s = 1048576;
	buf = (char *) malloc(s);

	do {
		n = read(src_fd, buf, s);
		if (n > 0) {
			len = write(dest_fd, buf, n);
			if (size < len)
				size=0;
			else
				size -= len;
		}
	} while (n > 0);

	free(buf);
	fsync(dest_fd);
	close(dest_fd);
	close(src_fd);

	if (size > 0)
		return ((int)size);
	else
		return (0);
}

static void
cleanup_zram(void)
{
	DIR *dp;
	struct dirent *ep;
	FILE *f = NULL;
	struct stat st;
	char buf[255];

	dp = opendir("/sys/block");
	if (NULL == dp) return;
	while((ep = readdir (dp)))
	{
		if (ep->d_name[0] == '.')
			continue;

		if (strncmp(ep->d_name, "zram", 4) == 0) {
			/* drop all allocated memory */
			snprintf(buf, 255, "/sys/block/%s/reset", ep->d_name);
			buf[254] = '\0';
			if (lstat(buf, &st))
				continue;
			f = fopen(buf, "w");
			if (!f)
				continue;

			fprintf(f, "1");
			fclose(f);
			if (ep->d_name[4] != '0') {
				/* delete zram device */
				f = fopen("/sys/class/zram-control/hot_remove", "w");
				if (!f)
					continue;

				fprintf(f, "%s", &(ep->d_name[4]));
				fclose(f);
			}
		}
	}
}

static void cleanup_inf(init_t *init, const char* file, int num, int *parts)
{
	FILE *f, *w;
	char line[255];
	char cmp[255];
	int part = 0, i, max, r = 0, p = 0;
	char *buf;
	struct stat st;

	f = fopen(file, "r");
	if (f == NULL) {
		return;
	}
	w = fopen("/tmp/firmware.inf.tmp", "w");
	if (w == NULL) {
		fclose(f);
		return;
	}

	buf = malloc(100 * 255);
	if (buf == NULL) {
		fclose(f);
		fclose(w);
                return;
	}

	stat(file, &st);
 	//msg(init,LOG_ERR,"cleanup_inf : File %s with size %lu\n", file, (unsigned long)st.st_size);

	while (fgets(line, 255, f) != NULL)
	{
		r++;
		if (strncmp(line, "[PART]", 6) == 0) {
			p++;
			max = 100 * 255;
			part=1;
			buf[0]='\0';
			strncat(buf, line, 255);
			max -= strlen(line);
			continue;
		}
		if (part == 0) {
			fprintf(w, "%s", line);
		} else {
			max -= strlen(line);
			if (max > 0) {
				strncat(buf, line, 255);
				for (i=0;i<num;i++)
				{
					snprintf(cmp, 255, "number=\"%d\"", parts[i]);
					if (strncmp(line, cmp, strlen(cmp)) == 0) {
						fprintf(w, "%s", buf);
						buf[0]='\0';
						part=0;
						break;
					}
				}
			}
		}
	}
	free(buf);
	fclose(f);
	fclose(w);

	if (p == num) {
		 msg(init,LOG_ERR,"cleanup_inf : Found %d partitions which is the same as the reduced set so do nothing here\n", p);
		 unlink("/tmp/firmware.inf.tmp");
		 return;
	} else {
		 msg(init,LOG_ERR,"cleanup_inf : Reducing %d partitions to %d\n", p, num);
	}

	if (r == 0) {
 		msg(init,LOG_ERR,"cleanup_inf : Processing File %s failed could not read data\n", file);
	} else {
 		msg(init,LOG_ERR,"cleanup_inf : Processing File %s read %d lines\n", file, r);
	}

	stat("/tmp/firmware.inf.tmp", &st);
 	// msg(init,LOG_ERR,"cleanup_inf : Generated /tmp/firmware.inf.tmp with size %lu\n", (unsigned long)st.st_size);

	if (st.st_size < 20) {
		msg(init,LOG_ERR,"cleanup_inf : Generated /tmp/firmware.inf.tmp is too small abort here\n");
		return;
	}

	if (access("/tmp/firmware.inf.bak", F_OK) == 0)
		unlink("/tmp/firmware.inf.bak");

	if (copy_file(file, "/tmp/firmware.inf.bak") == 0) {
		unlink(file);
		if (copy_file("/tmp/firmware.inf.tmp", file) == 0) {
 			// msg(init,LOG_ERR,"cleanup_inf : File copy of /tmp/firmware.inf.tmp to %s successful\n", file);
		} else {
 			msg(init,LOG_ERR,"cleanup_inf : File copy of /tmp/firmware.inf.tmp to %s failed restore old file\n", file);
			unlink(file);
			move_file("/tmp/firmware.inf.bak", file);
		}
	}

	stat(file, &st);
 	msg(init,LOG_ERR,"cleanup_inf : Generated %s with size %lu\n", file, (unsigned long)st.st_size);
}

/* TODO : This needs a complete rework */

static void
boot_igel_for_major_update(init_t *init)
{
	int i, d, fd, err;
	char *initcmd = (char *)"/sbin/init";
	int runlevel = 3;
	char *initargs[3];
	char tmp[10];
	FILE *f;
        char ro_mnt[512], rw_mnt[512], root_ro[512], root_rw[512];
	char string[1582], loop_device[30];
	bootreg_data* hndl;
	int loop_minor = 0;
	/*
	 *   1: igf1   -> system partition
	 *  23: igf23  -> bootsplash partition
	 *  25: igf25  -> extra fonts
	 *  29: igf29  -> oracle_jre8
	 *  60: igf60  -> nvgfx partition
	 * 254: igf254 -> license partition
	 * 255: igf255 -> wfs partition
	 */
	int to_copy[7] = {1, 23, 25, 254, 255}, copy_count = 5;
	struct kmod_struct kmod;
	char *migrate_kmod = (char *)"igel-flash.ko";
	struct stat st;

	to_copy[0] = init->sys_minor;

	/* keep jre partition for major update only if bootreg is set to do so */

	if (init->major_update_keep_jre == 1) {
		to_copy[copy_count] = 29;
		copy_count++;
	}

	/* keep nvgfx partition for major update only if bootreg is set to do so */

	if (init->major_update_keep_nvidia == 1) {
		to_copy[copy_count] = 60;
		copy_count++;
	}

	sprintf(ro_mnt, "%s/ro/sys", IGF_MNT_SYSTEM);
	sprintf(rw_mnt, "%s/rw", IGF_MNT_SYSTEM);
	sprintf(root_ro, "/root%s/ro/sys", IGF_MNT_SYSTEM);
	sprintf(root_rw, "/root%s/rw", IGF_MNT_SYSTEM);

	/* reset major update bootreg value */
	hndl = bootreg_init(IGF_DISK_NAME, BOOTREG_RDWR, BOOTREG_LOG_NONE);
	if (hndl != NULL) {
		bootreg_set(hndl, "major_update", "0");
		bootreg_deinit(&hndl);
	}

	if (igf_to_ddimage(init, copy_count, to_copy) != 0) {
		msg(init,LOG_ERR,"init: Creating /dev/ddimage.dd for major update failed\n");
		return;
	}

	err = symlink(init->devname, "/dev/igfboot_old");

	/* rmmod igel driver */
	rmmod_cmd("igel-flash");

	fd = open("/dev/major_update", O_WRONLY|O_CREAT, 0644);
	if (fd < 0) {
		msg(init,LOG_ERR,"init: Creating /dev/major_update file failed\n");
		return;
	}
	d = write(fd, "1", 1);
	close(fd);

	unlink("/dev/igf");
	unlink("/dev/igf0");
	unlink("/dev/igfdisk");
	unlink("/dev/igfboot");

	/* insmod igel driver */
	msg(init,LOG_ERR,"init: load_igel_flash_driver\n");
	err = load_igel_migrate_flash_driver(init, "/dev/ddimage.dd");

	/* driver was not loaded successfully so delete IGF_IMAGE_NAME and umount IGF_IMAGE_MOUNTPOINT */
	if (err == 0) {
		unlink("/dev/ddimage.dd");
		msg(init,LOG_ERR,"init: Loading IGEL driver with /dev/ddimage.dd failed\n");
	}

	unlink("/dev/igmdisk");
	unlink("/dev/igmboot");
	loop_minor = get_free_loopdev_num();
	snprintf(loop_device, 30, "/dev/loop%d", loop_minor);
	if (loop_minor < 0 || loopdev_setup_device("/dev/ddimage.dd", loop_device, O_RDWR, 0, 0) != 0) {
		rmmod_cmd("igel-migrate-flash");
		unlink("/dev/ddimage.dd");
		msg(init,LOG_ERR,"init: Getting free loop device number.\n");
		return;
	}
	err = create_blk_device("/dev/igmdisk", LOOP_MAJOR, loop_minor);

	if (init->devname) {
		free(init->devname);
		init->devname = NULL;
	}
	if (init->part_prefix) {
		free(init->part_prefix);
		init->part_prefix = NULL;
	}

	for (i=0; i<8; i++) {
		if (mount_fs(init, "squashfs", MS_RDONLY|MS_NOATIME, "/dev/igmsys", "%s", ro_mnt) == 0)
			break;
		if (i==7) {
			msg(init,LOG_ERR,"init: mount of /dev/igmsys failed.\n");
			msg(init,LOG_ERR,"init: %s\n",strerror(errno));
			unlink("/dev/major_update");
			err = loopdev_delete_device(loop_device);
			rmmod_cmd("igel-migrate-flash");
			unlink("/dev/ddimage.dd");
			return;
		}
		usleep(WAIT_TIME);
	}
	
	if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
		start_rescue_shell(init);

	if (init->initcmd && file_exists("%s%s",ro_mnt,init->initcmd) == 1) {
		if (init->runlevel==0) init->runlevel=3;
		initcmd = init->initcmd;
		runlevel = init->runlevel;
	}
	
	if (file_exists("%s%s",ro_mnt,initcmd) != 1) {
		if (umount(ro_mnt) != 0)
			umount2(ro_mnt,MNT_FORCE);
		msg(init,LOG_ERR,"init: can not find init cmd '%s' "
				 "on sys partition.\n",initcmd);
		unlink("/dev/major_update");
		err = loopdev_delete_device(loop_device);
		rmmod_cmd("igel-migrate-flash");
		unlink("/dev/ddimage.dd");
		return;
	}

	sprintf(string, "%s/sys", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "%s/sys/upper", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "%s/sys/work", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "lowerdir=%s,upperdir=%s/sys/upper,workdir=%s/sys/work", ro_mnt, rw_mnt, rw_mnt);
	for (i=0; i<8; i++) {
		if (mount("overlay","/root","overlay",MS_NOATIME,
		     string) == 0)
			break;
		if (i==7) {
			if (umount(ro_mnt) != 0)
				umount2(rw_mnt,MNT_FORCE);
			msg(init,LOG_ERR,"init: mount of overlayfs to /root failed.\n");
			msg(init,LOG_ERR,"init: %s\n",strerror(errno));
			unlink("/dev/major_update");
			err = loopdev_delete_device(loop_device);
			rmmod_cmd("igel-migrate-flash");
			unlink("/dev/ddimage.dd");
			return;
		}
		usleep(WAIT_TIME);
	}

	fd = open("/root/etc/igel/IGEL_DEVICE", O_WRONLY|O_CREAT, 0644);
	if (fd >= 0) {
		iwrite(fd,(unsigned char *)"igm", 3);
		close(fd);
	}

	cleanup_inf(init, "/root/etc/firmware.inf", copy_count, to_copy);

	/* copy igel-migrate-flash.ko to /root/ if present */

	kmod.name = migrate_kmod;
	kmod.realname = NULL;
	find_kernel_module_by_name(&kmod, init->moddir);
	if (kmod.realname != NULL) {
		mkdir("/root/igel-migrate-flash", 0755);
		copy_file(kmod.abs_name, "/root/igel-migrate-flash/igel-flash.ko");
		free(kmod.realname);
		free(kmod.abs_name);
	}

	/* move tmpfs filesystems to new root */
	if (mount("/dev","/root/dev","tmpfs",MS_MOVE,NULL) != 0) {
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move tmpfs to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		unlink("/dev/major_update");
		err = loopdev_delete_device(loop_device);
		rmmod_cmd("igel-migrate-flash");
		unlink("/dev/ddimage.dd");
		return;
	}

	if (mount("/sys","/root/sys","sysfs",MS_MOVE,NULL) != 0) {
		mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move sysfs to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		unlink("/dev/major_update");
		err = loopdev_delete_device(loop_device);
		rmmod_cmd("igel-migrate-flash");
		unlink("/dev/ddimage.dd");
		return;
	}

	if (mount("/proc","/root/proc","proc",MS_MOVE,NULL) != 0) {
		mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
		mount("/root/sys","/sys","sysfs",MS_MOVE,NULL);
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move proc to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		unlink("/dev/major_update");
		err = loopdev_delete_device(loop_device);
		rmmod_cmd("igel-migrate-flash");
		unlink("/dev/ddimage.dd");
		return;
	}

	sprintf(string, "/root/%s", IGEL_PREPARE_MIGRATION);

	if (access(string, X_OK) == 0) {
		pid_t pid = 0;

		switch((pid = fork())) {
			case -1: /* error */
				msg(init,LOG_ERR,"fork error\n");
				break;
			case 0:	/* child */
				if (chdir("/root") != 0) {
					msg(init,LOG_ERR,"Error changing dir to /root\n");
					exit(1);
				}
				if (chroot(".") != 0) {
					msg(init,LOG_ERR,"Error chroot call failed\n");
					exit(1);
				}
				err = system(IGEL_PREPARE_MIGRATION);
				exit(err);
				break;
			default: /* parent */
				waitpid(pid, NULL, 0);
				break;
		}
	}

	/* switch to new root */
	sprintf(tmp,"%d",runlevel);
	initargs[0]=initcmd;
	initargs[1]=tmp;
	initargs[2]=NULL;

	/* drop all caches used in initramfs */

	f = fopen("/proc/sys/vm/drop_caches", "w");
	if (f) {
		fprintf(f, "3");
		fclose(f);
	}

	switch_root(init,"/root","/dev/console",initcmd,initargs);

	rmmod_cmd("igel-migrate-flash");
	unlink("/dev/ddimage.dd");
}

static void
boot_igel_device(init_t *init)
{
	int i;
	char *initcmd = (char *) "/sbin/init";
	int runlevel = 3;
	char *initargs[3];
	char tmp[10];
	FILE *f;
        char ro_mnt[512], rw_mnt[512], root_ro[512], root_rw[512];
	char string[1582];
	struct stat st;
	int to_copy[255] = { 0 }, copy_count = 0;
	char mod_dir[255], mod_w_backports[255], mod_wo_backports[255], mod_link[255];
	char mod_rel_w_backports[85], mod_rel_wo_backports[85];
	struct utsname un;

	uname(&un);
	sprintf(ro_mnt, "%s/ro/sys", IGF_MNT_SYSTEM);
	sprintf(rw_mnt, "%s/rw", IGF_MNT_SYSTEM);
	sprintf(root_ro, "/root%s/ro/sys", IGF_MNT_SYSTEM);
	sprintf(root_rw, "/root%s/rw", IGF_MNT_SYSTEM);

	if (init->devname) {
		free(init->devname);
		init->devname = NULL;
	}
	if (init->part_prefix) {
		free(init->part_prefix);
		init->part_prefix = NULL;
	}

	for (i=0; i<8; i++) {
		if (mount_fs(init, "squashfs", MS_RDONLY|MS_NOATIME, "/dev/igfsys", "%s", ro_mnt) == 0)
			break;
		if (i==7) {
			msg(init,LOG_ERR,"init: mount of /dev/igfsys failed.\n");
			msg(init,LOG_ERR,"init: %s\n",strerror(errno));
			return;
		}
		usleep(WAIT_TIME);
	}
	
	if (init->initcmd && file_exists("%s%s",ro_mnt,init->initcmd)==1) {
		if (init->runlevel==0) init->runlevel=3;
		initcmd = init->initcmd;
		runlevel = init->runlevel;
	}

	if (file_exists("%s%s",ro_mnt,initcmd) != 1) {
		msg(init,LOG_ERR,"init: can not find init cmd '%s%s' "
				 "on sys partition %d.\n",ro_mnt,initcmd, file_exists("%s%s",ro_mnt,initcmd));
		if (init->verbose && (stat("/initramfs_debug_lx",&st)==0))
			start_rescue_shell(init);
		if (umount(ro_mnt) != 0)
			umount2(ro_mnt,MNT_FORCE);
		msg(init,LOG_ERR,"init: can not find init cmd '%s' "
				 "on sys partition.\n",initcmd);
		return;
	}

	sprintf(string, "%s/sys", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "%s/sys/upper", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "%s/sys/work", rw_mnt);
	mkdir(string, 0755);
	sprintf(string, "lowerdir=%s,upperdir=%s/sys/upper,workdir=%s/sys/work", ro_mnt, rw_mnt, rw_mnt);
	for (i=0; i<8; i++) {
		if (mount("overlay","/root","overlay",MS_NOATIME,
		     string) == 0)
			break;
		if (i==7) {
			if (umount(ro_mnt) != 0)
				umount2(rw_mnt,MNT_FORCE);
			msg(init,LOG_ERR,"init: mount of overlayfs to /root failed.\n");
			msg(init,LOG_ERR,"init: %s\n",strerror(errno));
			return;
		}
		usleep(WAIT_TIME);
	}

	/* check if use_backports entry is set and switch modules link accordingly */

	if (init->use_backports == 1 || init->use_backports == -1) {
		snprintf(mod_rel_w_backports, 85, "%s-with-backports", un.release);
		snprintf(mod_rel_wo_backports, 85, "%s-without-backports", un.release);
		if (snprintf(mod_dir, 255, "/root%s", init->moddir) > 0 &&
		    snprintf(mod_w_backports, 255, "/root%s-with-backports", init->moddir) > 0 &&
		    snprintf(mod_wo_backports, 255, "/root%s-without-backports", init->moddir) > 0) {
			memset(mod_link, 0, 255);
			if (lstat(mod_dir, &st) == 0 && S_ISLNK(st.st_mode) &&
			    readlink(mod_dir, mod_link, 254) > 0) {
				if (strcmp(mod_link, mod_w_backports) != 0 &&
				    strcmp(mod_link, mod_rel_w_backports) != 0 &&
				    init->use_backports == 1 &&
				    access(mod_w_backports, R_OK) == 0) {
					unlink(mod_dir);
					if (symlink(mod_rel_w_backports, mod_dir) != 0 ||
					    stat(mod_dir, &st) != 0 || S_ISDIR(st.st_mode) == 0) {
						symlink(mod_link, mod_dir);
					}
				} else if (strcmp(mod_link, mod_wo_backports) != 0 &&
					   strcmp(mod_link, mod_rel_wo_backports) != 0 &&
					   init->use_backports == -1 &&
					   access(mod_wo_backports, R_OK) == 0) {
					unlink(mod_dir);
					if (symlink(mod_rel_wo_backports, mod_dir) != 0 ||
					    stat(mod_dir, &st) != 0 || S_ISDIR(st.st_mode) == 0) {
						symlink(mod_link, mod_dir);
					}
				}
			}
		}
	}

	/* special case if ddimage was altered somehow, delete not present partitions */

	if (init->boot_type == BOOT_OSC_PXE) {
		get_present_igfs(to_copy, &copy_count);
		if (copy_count > 3)
			cleanup_inf(init, "/root/etc/firmware.inf", copy_count, to_copy);
		copy_file("/root/etc/firmware.inf", "/dev/firmware.inf");
		msg(init,LOG_ERR,"copy_count = %d\n", copy_count);
		for (i=0;i<copy_count;i++) {
			msg(init,LOG_ERR,"to_copy[%d] = %d\n", i, to_copy[i]);
		}
	}

	/* move tmpfs filesystems to new root */
	if (mount("/dev","/root/dev","tmpfs",MS_MOVE,NULL) != 0) {
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move tmpfs to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		return;
	}
	if (mount("/sys","/root/sys","sysfs",MS_MOVE,NULL) != 0) {
		mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move sysfs to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		return;
	}
	if (mount("/proc","/root/proc","proc",MS_MOVE,NULL) != 0) {
		mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
		mount("/root/sys","/sys","sysfs",MS_MOVE,NULL);
		mount(root_rw,rw_mnt,"tmpfs",MS_MOVE,NULL);
		if (umount(root_ro) != 0)
			umount2(root_ro,MNT_FORCE);
		if (umount("/root") != 0)
			umount2("/root",MNT_FORCE);
		msg(init,LOG_ERR,"init: can not move proc to new root\n");
		msg(init,LOG_ERR,"init: %s\n",strerror(errno));
		return;
	}

	switch(init->boot_type){
		case BOOT_STANDARD:
			break;
		case BOOT_OSC_TOKEN:
		case BOOT_OSC_CD:
			/* we boot from ddimage (UDC Token or UDC CD) */
			if (mount(IGF_IMAGE_MOUNTPOINT,"/root/igfimage","tmpfs",MS_MOVE,NULL) != 0) {
				mount("/root/proc","/proc","tmpfs",MS_MOVE,NULL);
				mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
				mount("/root/sys","/sys","sysfs",MS_MOVE,NULL);
				if (umount("/root") != 0)
					umount2("/root",MNT_FORCE);
				msg(init,LOG_ERR,"init: can not move igfimage to new root\n");
				msg(init,LOG_ERR,"init: %s\n",strerror(errno));
				return;
			}
			break;
		case BOOT_OSC_PXE:
			if (stat(IGF_PXE_DD_IMAGE,&st) != 0 || stat(IGF_IMAGE_NAME,&st) == 0) {
				/* we boot from ddimage (UDC Token or UDC CD) */
				if (mount(IGF_IMAGE_MOUNTPOINT,"/root/igfimage","tmpfs",MS_MOVE,NULL) != 0) {
					mount("/root/proc","/proc","tmpfs",MS_MOVE,NULL);
					mount("/root/dev","/dev","tmpfs",MS_MOVE,NULL);
					mount("/root/sys","/sys","sysfs",MS_MOVE,NULL);
					if (umount("/root") != 0)
						umount2("/root",MNT_FORCE);
					msg(init,LOG_ERR,"init: can not move igfimage to new root\n");
					msg(init,LOG_ERR,"init: %s\n",strerror(errno));
					return;
				}
			}
			break;
		case BOOT_WINLINUX:
			break;
	}

	/* switch to new root */
	sprintf(tmp,"%d",runlevel);
	initargs[0]=initcmd;
	initargs[1]=tmp;
	initargs[2]=NULL;

	/* drop all caches used in initramfs */

	f = fopen("/proc/sys/vm/drop_caches", "w");
	if (f) {
		fprintf(f, "3");
		fclose(f);
	}

	bootsplash_stop(init);

	switch_root(init,"/root","/dev/console",initcmd,initargs);
}

static char *
get_version_signature(char *default_version)
{
	int len;
	char *buf;

	buf = read_file(255, buffer, sizeof(buffer), "/proc/version_signature");
	if (buf != NULL) {
		len = strlen(buf);
		if (buf[len-1] == '\n')
			buf[len-1] = '\0';
		
		return strdup(buf);
	}
	
	/* fallback */
	return strdup(default_version);
}

static void
check_other_programs(int argc, char **argv)
{
	char *myname = strdup(argv[0]);
	char *program = basename(myname);
	int err;
	
	if (!strcmp(program,"modprobe")) {
		err = modprobe(argc, argv);
		exit (err);
	}
	
	free(myname);
}

static void
run_initsplash(init_t *init)
{
	pid_t pid = 0;
	int err = 0; 

	switch((pid = fork())) {
		case -1: /* error */
			msg(init,LOG_ERR,"fork error\n");
			break;
		case 0:	/* child */
			if (chdir(IGF_BSPL_CHROOT) != 0) {
				msg(init,LOG_ERR,"Error changing dir to %s\n", IGF_BSPL_CHROOT);
				exit(1);
			}
			if (chroot(".") != 0) {
				msg(init,LOG_ERR,"Error chroot call failed\n");
				exit(1);
			}
			err = system("/etc/igel/kms/load_drm_module");
			err = system("/etc/igel/bootsplash_config/run_bootsplash");
			exit(1);
			break;
		default: /* parent */
			init->progress_pid = pid;
			break;
	}
}

static void
bootsplash_start(init_t *init)
{
	struct stat st;
	char bash[255], load_drm_module[255], run_bootsplash[255];
	
	if (! init->splash)
		return;

	snprintf(bash, 255, "%s/bin/bash", IGF_BSPL_CHROOT); 
	snprintf(load_drm_module, 255, "%s/etc/igel/kms/load_drm_module", IGF_BSPL_CHROOT); 
	snprintf(run_bootsplash, 255, "%s/etc/igel/bootsplash_config/run_bootsplash", IGF_BSPL_CHROOT);

	if ((stat(bash ,&st) != 0) ||
	    (stat(load_drm_module ,&st) != 0) ||
	    (stat(run_bootsplash ,&st) != 0)) {
		return;
	}
	
	msg(init,LOG_NOTICE," * showing bootsplash ...\n");
	
	/* /bin/initsplash (LRMI) needs /dev/mem */
	mknod("/dev/mem", S_IFCHR | S_IRUSR|S_IWUSR, makedev(1, 1));
	
	run_initsplash(init);
}

static void
bootsplash_stop(init_t *init)
{			
	if (init->progress_pid > 0) {
		msg(init,LOG_NOTICE," * stopping bootsplash ...\n");
		kill(init->progress_pid, SIGTERM);
		wait(NULL);
				
		init->progress_pid = 0;
		umount(IGF_BSPL_CHROOT_SYS);
		umount(IGF_BSPL_CHROOT_PROC);
		umount(IGF_BSPL_CHROOT_DEV);
		umount(IGF_BSPL_CHROOT);
	}
}

/* xhci workaround is easy to understand unbind xhci_hcd devices and bind them again */
int
xhci_workaround(void)
{
	DIR *dir = opendir("/sys/bus/pci/drivers/xhci_hcd");
	FILE *f = NULL;
	struct dirent *dirent;
	char name[PATH_MAX];
	struct stat st;

	if (dir == NULL)
		return (0);

	while ((dirent = readdir(dir))) 
	{
		if (dirent->d_name[0] == '.')
			continue;

		if (fnmatch("[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f][.][0-9A-Fa-f]", dirent->d_name, FNM_NOESCAPE) != 0)
			continue;

		snprintf(name, sizeof (name), "/sys/bus/pci/drivers/xhci_hcd/%s", dirent->d_name);
		if (lstat(name, &st))
			continue;

		if (S_ISLNK(st.st_mode)) {
			f = fopen("/sys/bus/pci/drivers/xhci_hcd/unbind", "w");
			if (!f)
				continue;
			fprintf(f, "%s", dirent->d_name);
			fclose(f);
			usleep(200000);
			f = fopen("/sys/bus/pci/drivers/xhci_hcd/bind", "w");
			if (!f)
				continue;
			fprintf(f, "%s", dirent->d_name);
			fclose(f);
		}
	}
	
	return (0);
}

int main(int argc, char **argv)
{
	static struct utsname un;
	char shellprompt[255];
	char *kernel_signature;
	init_t init;
	struct stat st;
	
	check_other_programs(argc, argv);
	
	umask(000);

	memset(&init, 0, sizeof(init_t));
	init.current_console = BOOT_TTY;

	/* get kernel version and module directory */
	uname(&un);
	snprintf(init.moddir,255,"/lib/modules/%s",un.release);

	snprintf(shellprompt,255,"(%s:$PWD) # ",un.release);
	setenv("PS1",shellprompt,1);
  
	prepare_filesystem();
	parse_cmdline(&init);

	/* redirect log console */
	setconsole(&init, CONSOLE_TTY);
	setlogcons(&init, CONSOLE_CONS);
	/* switch to splash console */
	cursor_off(SPLASH_TTY);
	change_vt(&init, SPLASH_CONS);
	
	/* in verbose and failsafe boot we show all messages on the log console */
	if (init.verbose || init.failsafe) {
		change_vt(&init, CONSOLE_CONS);	
	}
	
	/* print igel copyright and kernel version */
	kernel_signature = get_version_signature(un.release);
	msg(&init,LOG_NOTICE,"\n(c)2019, IGEL Technology GmbH\n\n");
	msg(&init,LOG_NOTICE," * booting, kernel '%s' ...\n",kernel_signature);

	if (needs_xhci_workaround() == 1) {
		xhci_workaround();
	}

	load_kernel_modules(&init);

	/* eMMC drivers */	
	// 'load_kernel_modules' calls 'load_pci_modules' which reads the pcimap
	// and should autoload 'sdhci' and 'sdhci-pci'

   // load_kernel_module(&init,"sdhci");
   // load_kernel_module(&init,"sdhci-pci");

	if (kmodule_already_loaded(&init, "sdhci") == 1 ||
	    kmodule_already_loaded(&init, "sdhci-acpi") == 1 ||
	    kmodule_already_loaded(&init, "sdhci-pci") == 1 ||
	    kmodule_already_loaded(&init, "mmc_core") == 1) {
	    if (kmodule_already_loaded(&init, "mmc_core") == 0)
		    load_kernel_module(&init,"mmc_core");
	    if (kmodule_already_loaded(&init, "mmc_block") == 0)
	    	load_kernel_module(&init,"mmc_block");
	}

        if (kmodule_already_loaded(&init, "pcspkr") == 0)
	        load_kernel_module(&init,"pcspkr");

	/* HyperV storage driver */
	if (is_hyperv() == 1) {
		load_kernel_module(&init,"hv_vmbus");
		/* 
		 * only if hv_vmbus could be loaded we are in a HyperV 
		 * machine and need the HyperV storage driver to be loaded
		 */
		if (kmodule_already_loaded(&init, "hv_vmbus") == 1 ){
		    load_kernel_module(&init,"hv_storvsc");
		}
	}

	check_boot_type(&init);

	/* start a debug shell in initramfs_debug_lx */
	if ((stat("/initramfs_debug_lx",&st)==0))
		start_rescue_shell(&init);

	(void)igel_keyring();
  
  	while (1) {
	
		msg(&init,LOG_INFO,"Looking for igel boot device:\n");
		
		find_igel_device_loop(&init);
  
  		if (init.found) {
			create_block_devices(&init, "/sys/block", 1);
			create_tty_devices(&init, "/sys/devices/virtual/tty");
			create_virtual_devices(&init, "/sys/devices/virtual/mem");
			create_virtual_devices(&init, "/sys/devices/virtual/misc");
			create_virtual_devices(&init, "/sys/devices/virtual/input");
			create_virtual_devices(&init, "/sys/devices/virtual/vc");
			/* TODO: do we need to fix permisions in /dev ? */
			
			if (init.verbose) {
				print_init(&init);
				start_rescue_shell(&init);
			}

			if (init.major_update) {
				boot_igel_for_major_update(&init);
			} else {
				boot_igel_device(&init);
			}
			
			/* when boot_igel_device returns, there was an error */
			print_init(&init);
			msg(&init,LOG_ERR,"init: can not boot igel device\n");
			start_rescue_shell(&init);
			
			/* cleanup */
			init.found = 0;
			unlink(IGF_BOOT_NAME);
			unlink(IGF_DISK_NAME);
			unlink(IGF_SYS_NAME);
			
			/* rmmod igel driver */
			igel_delete_dev();
			rmmod_cmd("igel-flash");
			
			/* add device to black list, so that it is not used anymore */
			add_device_to_blacklist(&(init.dev));
			
			/* give the user time for reading error messages */
			sleep (ERROR_TIME);
		}
		else {
			print_init(&init);
			msg(&init,LOG_ERR,"init: can not find igel device\n");
			start_rescue_shell(&init);
			beep(-ENOENT);
			reboot(LINUX_REBOOT_CMD_HALT);
		}
		if (init.devname) {
			free(init.devname);
			init.devname = NULL;
		}
		if (init.part_prefix) {
			free(init.part_prefix);
			init.part_prefix = NULL;
		}
	}

	/* should never exit ! */
	return (0);
}
