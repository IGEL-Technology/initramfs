/*
 * initramfs init program for kernel 3.13.x.
 * Copyright (C) by IGEL Technology GmbH 2014

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
#include <syslog.h>
#include <stdint.h>
#include <sys/stat.h>
#include <limits.h>

#define BOOT_TTY	"/dev/tty1"
#define SPLASH_TTY	"/dev/tty7"
#define CONSOLE_TTY	"/dev/tty10"
#define BOOT_CONS	 1
#define SPLASH_CONS	 7
#define CONSOLE_CONS	10
#define MAX_PART_NUM    128

#define STRING_COMPARE			0
#define STRING_NOCASE_COMPARE		1
#define STRING_MODULE_COMPARE		2


extern uint32_t crc_32_tab[256];	/* crc table, used by crc functions */

struct mmdev
{
  unsigned int major;
  unsigned int minor;
};

struct kmod_struct {
	char *name;
	char *realname;
	char *abs_name;
};

struct mod_list {
	char 		*alias;
	struct mod_list	*next;
};

typedef struct devlist_s devlist_t;
struct devlist_s {
  struct mmdev  dev;
  devlist_t 	*next;
};

enum boot_type {
	BOOT_STANDARD = 0,
	BOOT_OSC_TOKEN,
	BOOT_OSC_CD,
	BOOT_WINLINUX,
	BOOT_OSC_PXE
};

typedef struct init_s init_t;
struct init_s {
	int           try;
	int           found;
	char	      *devname;
	char	      *part_prefix;
	int           part;
	int           no_major_update;
	int           major_update;
	int           major_update_keep_jre;
	int           major_update_keep_nvidia;
	uint64_t      devsize;
	uint64_t      part_start[MAX_PART_NUM];
	uint64_t      part_size[MAX_PART_NUM];
	struct mmdev  dev;
	char	      moddir[255];
	/* from kernel cmdline: */
	int           verbose;
	int	      bootversion;
	char	      *boot_id;
	char 	      *initcmd;
	int	      runlevel;
	int	      splash;
	int	      failsafe;
	/* misc */
	const char    *current_console;
	unsigned      boot_type;
	/* backports marker */
	char          use_backports;
	char          *isofilename;
	int           isopartnum;
	int           ram_install;
	int           osc_external_firmware;
	int           osc_partnum;
	char          *osc_path;
	int           osc_unattended;
	int           firmware_partnum;
	char          *firmware_path;
	uint32_t      sys_minor;
	uint64_t      igel_poffset;
	
	pid_t	      progress_pid;
};

struct vendor_list {
	char               name[8];
	struct vendor_list *next;
};

/* one-way list of options to pass to a insmod kernel module command */
struct mod_opt_t {
	char *  m_opt_val;
	struct mod_opt_t * m_next;
};

/* alias.c */
extern int load_alias_modules(init_t *init, const char *device);

/* console.c */
extern int setlogcons(init_t *init, int console);
extern int setconsole(init_t *init, const char *tty);
extern int cursor_off(const char *tty);
extern int cursor_on(const char *tty);
extern int change_vt(init_t *init, int console);
extern void msg(init_t *init, int level, const char *fmt, ...) __attribute__ ((format (gnu_printf, 3, 4)));

/* insmod.c */
extern int insmod_cmd(char *filename, struct mod_opt_t *opts);
/* modprobe.c */
extern int modprobe_cmd(const char *name);
extern int modprobe(int argc, char **argv);
/* rmmod.c */
extern int rmmod_cmd(const char *name);

/* igel_keyring.c*/
extern int igel_keyring(void);

/* file-handling.c */
int open_file_write_only(const char* format, ...) __attribute__ ((format (gnu_printf, 1, 2)));
int open_file_read_only(const char* format, ...) __attribute__ ((format (gnu_printf, 1, 2)));
int grep_file(char *search_string, int ignore_case, const char* format, ...) __attribute__ ((format (gnu_printf, 3, 4)));
int grep_kernel_module_in_file(char *search_string, const char* format, ...) __attribute__ ((format (gnu_printf, 2, 3)));
ssize_t iread(int fd, unsigned char *buf, size_t len);
ssize_t iwrite(int fd, const unsigned char *buf, size_t len);
ssize_t ipread(int fd, unsigned char *buf, size_t len, off_t offset);
ssize_t ipwrite(int fd, const unsigned char *buf, size_t len, off_t offset);
int file_exists(const char* format, ...) __attribute__ ((format (gnu_printf, 1, 2)));
char *read_file (int len, char *buffer, int buf_len, const char* format, ...) __attribute__ ((format (gnu_printf, 4, 5)));

/* init.c */
void start_rescue_shell(init_t *init);
int kmodule_already_loaded (init_t *init, const char *name);
int is_hyperv (void);
int needs_xhci_workaround (void);
void load_kernel_module(init_t *init, const char *name);
int xhci_workaround(void);
int mount_fs(init_t *init, const char *fallback_fstype, unsigned long mountflags, const char *device, const char* format, ...) __attribute__ ((format (gnu_printf, 5, 6)));

/* crc.c */
void makecrc(void);
uint32_t updcrc(unsigned char* s, unsigned n);

/* alias.c */
void find_kernel_module_by_name (struct kmod_struct *list, const char *path);

/* gzip.c */
extern int check_gz(const char * string);
extern int compress_file(const char *srcfile, const char *trgtfile);
extern int compress_file_enhanced(const char *srcfile, const char *trgtfile, int compress_level, uint64_t pos, uint64_t size);
extern int decompress_file(const char *srcfile, const char *trgtfile);

/* check_part_hdr.c */
int check_igel_part(char *filename);

/* read-write-extent.c */
int check_read_write_extent_present(init_t *init, char *igf_name);
int write_to_read_write_extent(init_t *init, char *igf_name, char *source_file, uint8_t extent, uint64_t pos, uint64_t size);
int read_from_read_write_extent(init_t *init, char *igf_name, char *target_file, uint8_t extent, uint64_t pos, uint64_t size);

/* blkid_detect.c */
int detect_luks_header(const char* format, ...) __attribute__ ((format (gnu_printf, 1, 2)));
const char *fstype_of(const char* format, ...) __attribute__ ((format (gnu_printf, 1, 2)));

/* minimal_igelmkimage.c */
int igf_to_ddimage(init_t *init, int num, int *minor);

/* loopdev.c */
int get_free_loopdev_num(void);
int loopdev_delete_device(const char *loopdev);
int loopdev_setup_device(const char *file, const char *loopdev, mode_t mode, uint64_t offset, uint64_t size);

/* beep.c */
void beep(int error);

/* strip_ddimage.c */
int delete_parts(init_t *init, int in_fd, int out_fd, int num, int *list);

/* sysfs-handling.c */
int find_pci_vendors (struct vendor_list *vendors);
char *get_dmi_data(const char *field, char *buffer, size_t len_buf);
char *get_block_data(const char *blk_dev, const char *field, char *buffer, size_t len_buf);
char *get_block_partition_data(const char *blk_dev, int part_num, const char *field, char *buffer, size_t len_buf);
char *get_block_data_printf(const char *field, char *buffer, size_t len_buf, const char *format, ...) __attribute__ ((format (gnu_printf, 4, 5)));
char *get_block_partition_data_printf(int part_num, const char *field, char *buffer, size_t len_buf, const char *format, ...) __attribute__ ((format (gnu_printf, 5, 6)));
char *get_sysfs_entry(char *buffer, size_t len_buf, const char *format, ...) __attribute__ ((format (gnu_printf, 3, 4)));

/* string_helper.c */
int match_n_module(const char *s1, char *s2, int n);
int match_module(const char *s1, char *s2);
int search_match_module(const char *s1, char *s2);
int match_string_nocase(const char *s1, char *s2);
int match_string(const char *s1, char *s2);
void remove_end_newline(char *s1);
