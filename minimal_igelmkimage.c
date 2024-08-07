#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>                           
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "init.h"
#include "igel64/igel.h"

/*
 * read data from device/file to dump file with given offset and size
 *
 * return 0 if everything is ok
 *        1 if source file could not be opened for reading
 *        2 if target file could not be opened for writing
 *        3 if seek to offset (start) position failed
 *        4 if malloc was not successful
 *        5 if a error occured while reading data
 *        6 if a error occured while writing data
 */

static int read_to_dump_file(const char *src_file, const char *trgt_file, uint64_t start, uint64_t size)
{
	int src_fd;
	int trgt_fd;
	unsigned char *buf = NULL;
	uint64_t dumped;
	int blk, n, t;

	src_fd = open(src_file, O_RDONLY);
	if (src_fd < 0) {
		return 1;
	}
	unlink(trgt_file);
	trgt_fd = open(trgt_file, O_WRONLY|O_CREAT, 0644);
	if (trgt_fd < 0) {
		close(src_fd);
		return 2;
	}

	if (start > 0 ) {
		if (lseek (src_fd, start, SEEK_SET) != start) {
			close(trgt_fd);
			close(src_fd);
			return 3;
		}
        }

	if (size > 256 * 1024) {
		blk = 256 * 1024;
	} else {
		blk = size;
	}

	buf = malloc(blk);
	if (buf == NULL) {
		close(trgt_fd);
		close(src_fd);
		return 4;
	}

	dumped = 0;

	while (dumped < size) {
		t = blk;
		if (size - dumped < t)
			t = size - dumped;

		n = iread(src_fd, buf, t);
		if (n != t) {
			free(buf);
			close(trgt_fd);
	                close(src_fd);
        	        return 5;
		}
		n = iwrite(trgt_fd, buf, t);
		if (n != t) {
			free(buf);
			close(trgt_fd);
	                close(src_fd);
        	        return 6;
		}
		dumped += t;
	}

	free(buf);
	close(trgt_fd);
	close(src_fd);

	return 0;
}

/* copy igfdata from proc to a ddimage file */

int igf_to_ddimage(init_t *init, int num, int *minor)
{
	int fd, src_fd, i, n, w;
	struct directory dir;
	int n_sections = 1, to_copy[256], num_copy = 0;
	struct stat s;
	char str[256];
	int curr_section;
	struct igf_part_hdr *part_hdr;
	struct igf_sect_hdr *sect_hdr;
	unsigned char *buf, *p;
	uint32_t nsections = 0;
	struct stat st;
	uint64_t efi_start, efi_size, start, size, devsize;

	buf = (unsigned char *) malloc(sizeof(unsigned char)* IGF_SECTION_SIZE);

	sect_hdr = (struct igf_sect_hdr *) buf;
	p = buf + IGF_SECT_HDR_LEN;
	part_hdr = (struct igf_part_hdr *) p;

	for (i=0;i<num;i++)
	{
		snprintf(str, 256, "/dev/igf%d", minor[i]);
		if (stat(str, &s) < 0)
			continue;
		to_copy[num_copy] = minor[i];
		num_copy++;
	}

	efi_start = init->part_start[1];
	efi_size = init->part_start[2] + init->part_size[2] - init->part_start[1];
	start = init->part_start[init->part - 1];
	size = init->part_size[init->part - 1];
	devsize = init->devsize;

	snprintf(str, 255, "/dev/%s", init->devname);
	str[255] = '\0';
	unlink("/dev/EFI.dd");
	unlink("/dev/EFI.dd.gz");
	if (compress_file_enhanced(str, "/dev/EFI.dd.gz", 1, efi_start, efi_size) != 0) {
		unlink("/dev/EFI.dd.gz");
		if (read_to_dump_file(str, "/dev/EFI.dd", efi_start, efi_size) != 0) {
			msg(init, LOG_ERR, "init: ERROR could not save EFI partitions to file /dev/EFI.dd\n");
			free(buf);
			return 1;
		}
	}
	unlink("/dev/mbr-part-header.dd");
	if (read_to_dump_file(str, "/dev/mbr-part-header.dd", 0, (34 * 512)) != 0) {
		msg(init, LOG_ERR, "init: ERROR could not save bootsector to file\n");
		free(buf);
		return 1;
	}

	unlink("/dev/gpt-suffix.dd");
	if (read_to_dump_file(str, "/dev/gpt-suffix.dd", devsize - (34 * 512), (34 * 512)) != 0) {
		msg(init, LOG_ERR, "init: ERROR could not save GPT header at the end of the device to file\n");
		free(buf);
		return 1;
	}

	unlink("/dev/ddimage.dd");
	fd = open("/dev/ddimage.dd", O_WRONLY|O_CREAT, 0644);
	if (fd < 0) {
		msg(init, LOG_ERR, "init: ERROR could not create /dev/ddimage.dd file");
		free(buf);
		return 1;
	}

	/* write Section 0 */

	bzero(buf, IGF_SECTION_SIZE);

	/* copy stage2 bootloader and bootregistry, if this fails it is not fatal */

	src_fd = open ("/dev/igfdisk", O_RDONLY);
	if (src_fd >= 0) {
		/* bootregistry 32k size on 32k from igfdisk start */
		if (lseek(src_fd, IGEL_BOOTREG_OFFSET, SEEK_SET) != -1) {
			n = iread(src_fd, (buf + IGEL_BOOTREG_OFFSET), IGEL_BOOTREG_SIZE);
			if (n != IGEL_BOOTREG_SIZE) {
				bzero(buf, IGEL_BOOTREG_SIZE);
			}
		}
		close(src_fd);
	} else {
		msg(init, LOG_ERR, "init: ERROR while opening /dev/igfdisk for reading\n");
		free(buf);
		close(fd);
		return 1;
	}

	/* write section 0 which contain bootregistry and the partition directory */

	w = iwrite(fd, buf, IGF_SECTION_SIZE);
	if (w != IGF_SECTION_SIZE) {
		msg(init, LOG_ERR, "init: ERROR while writting %lu bytes to destination\n", (unsigned long) IGF_SECTION_SIZE);
		free(buf);
		close(fd);
		close(src_fd);
		return 1;
	}

	/* initialize CRC */

	makecrc();

	/* create initial directory */

	bzero(&dir, sizeof(struct directory));
	dir.magic = DIRECTORY_MAGIC;
	dir.crc = CRC_DUMMY;
	dir.dir_type = 0;
	dir.max_minors = DIR_MAX_MINORS;
	dir.version = 1;
	dir.n_fragments = 1; /* the freelist has exactly one fragment */
	dir.max_fragments = MAX_FRAGMENTS;
	for (i = 0; i < 8; i++)
		dir.extension[i] = 0;

	/* Initialize the freelist */
	dir.partition[0].minor = 0;
	dir.partition[0].type = PTYPE_IGEL_FREELIST;
	dir.partition[0].first_fragment = 0;
	dir.partition[0].n_fragments = 1;

	/* start copying the data from the igf partitions */

	for (i=0;i<num_copy;i++)
	{
		snprintf(str, 256, "/proc/igel/firmware/%d", to_copy[i]);
		if (stat(str ,&st) != 0) {
			msg(init, LOG_ERR, "init: ERROR  %s does not exists\n", str);
			return 1;
		} 
		src_fd = open (str, O_RDONLY);
		if (src_fd < 0) {
			msg(init, LOG_ERR, "init: ERROR could not open %s for reading\n", str);
			close(fd);
			return 1;
		} else {
			msg(init, LOG_NOTICE, "init: Started reading from %s\n", str);
		}
		curr_section = 0;
		nsections = 0;
		do {
			n = iread(src_fd, buf, IGF_SECTION_SIZE);
			if (n != IGF_SECTION_SIZE) {
				msg(init, LOG_ERR, "init: ERROR while reading %lu bytes from source\n", (unsigned long) IGF_SECTION_SIZE);
				free(buf);
				close(fd);
				close(src_fd);
				return 1;
			}
			/* first section contains the partition header so get the data from it */

			if (curr_section == 0) {
				/* reading from proc means sect_hdr->next_section contains number of sections */
				nsections = sect_hdr->next_section;
				dir.partition[to_copy[i]].minor = sect_hdr->partition_minor;
				dir.partition[to_copy[i]].type = part_hdr->type;
				dir.partition[to_copy[i]].first_fragment = dir.n_fragments;
				dir.partition[to_copy[i]].n_fragments = 1;
				dir.fragment[dir.n_fragments].first_section = n_sections;
				dir.fragment[dir.n_fragments].length = nsections;
				dir.n_fragments++;
				sect_hdr->section_in_minor = 0;
			} else {
				sect_hdr->section_in_minor = curr_section;
			}

			/* set proper section data to match new positions and so on */
			
			sect_hdr->generation = 1;

			/* very important last section must point to -1 in next_section otherwise
			 * the failsafe mode of the igel flash drivers fails */

			if (curr_section + 1 >= nsections) {
				sect_hdr->next_section = 0xffffffff;
				n_sections++;
			} else {
				sect_hdr->next_section = ++n_sections;
			}

			/* calculate CRC of section header */

			(void) updcrc(NULL, 0);
			sect_hdr->crc = updcrc(buf + SECTION_IMAGE_CRC_START,
					IGF_SECTION_SIZE-SECTION_IMAGE_CRC_START);

			w = iwrite(fd, buf, IGF_SECTION_SIZE);
			if (w != IGF_SECTION_SIZE) {
				msg(init, LOG_ERR, "init: ERROR while writting %lu bytes to destination\n", (unsigned long) IGF_SECTION_SIZE);
				free(buf);
				close(fd);
				close(src_fd);
				return 1;
			}
			curr_section++;
		} while (nsections > curr_section);
		close(src_fd);

		msg(init, LOG_NOTICE, "init: Ended reading from %s\n", str);
	}

	/* set freelist to 0 */

	dir.fragment[0].first_section = n_sections;
	dir.fragment[0].length = size / IGF_SECTION_SIZE - 1 - n_sections;

	/* generate directory CRC */

	(void) updcrc(NULL, 0);
	dir.crc = updcrc((unsigned char *)&dir + 8, sizeof(struct directory) - 8);

	/* write partition directory to ddimage */

	if (lseek(fd, DIR_OFFSET, SEEK_SET) == -1) {
		msg(init, LOG_ERR, "init: seek failed to write directory");
		close(fd);
		return 1;
	}

	if (iwrite(fd, (unsigned char *)&dir, sizeof(struct directory)) != sizeof(struct directory))
	{
		msg(init, LOG_ERR, "init: failed to write directory");
		close(fd);
		return 1;
	}

	fsync(fd);
	close(fd);

	unlink("/dev/recovery.sh");
	fd = open("/dev/recovery.sh", O_RDWR|O_CREAT, 0755);
	if (fd < 0) {
		msg(init, LOG_ERR, "init: ERROR could not create /dev/recovery.sh file");
		free(buf);
		return 1;
	}

	bzero(buf, IGF_SECTION_SIZE);

	if (access("/dev/EFI.dd.gz", R_OK) == 0) {
		snprintf((char *)buf, IGF_SECTION_SIZE, "#!/bin/sh\n"
			 "dd if=/dev/mbr-part-header.dd of=/dev/%s bs=512 oflag=direct 2> /dev/null\n"
			 "dd if=/dev/gpt-suffix.dd of=/dev/%s bs=512 seek=%llu oflag=seek_bytes,direct 2> /dev/null\n"
			 "if [ -x /usr/bin/bar ]; then\n"
			 "     gzip -dc /dev/EFI.dd.gz | bar -lpt \"Restoring old EFI\" -lpm \"Copying old EFI partitions to flash device\" -lpg 55555 -lpp 1 -i \"fw-update-old\" -s \"%llu\"| dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "     cat /dev/ddimage.dd /dev/zero | dd bs=1M iflag=count_bytes count=%llu bar -lpt \"Restoring old firmware\" -lpm \"Copying old firmware to flash device\" -lpg 55555 -lpp 1 -i \"fw-update-old\" -s \"%llu\" | dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "else\n"
			 "     gzip -dc /dev/EFI.dd.gz | dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "     cat /dev/ddimage.dd /dev/zero | dd of=/dev/%s bs=1M count=%llu seek=%llu iflag=count_bytes,fullblock oflag=seek_bytes,direct\n"
			 "fi\n"
			 "mkdir -p /mnt-efi2 && mount /dev/%s3 /mnt-efi2 || exit 0\n"
			 "rm -rf /mnt-efi2/migration_backup\n"
			 "umount /mnt-efi2 && rmdir /mnt-efi2\n",
			 init->devname, init->devname, (unsigned long long)(devsize - (34 * 512)),
			 (unsigned long long) efi_size, init->devname, (unsigned long long) efi_start,
			 (unsigned long long) size, (unsigned long long) size, init->devname,
			 (unsigned long long) start, init->devname, (unsigned long long) efi_start,
			 init->devname, (unsigned long long) size, (unsigned long long) start, init->devname);
	} else {
		snprintf((char *)buf, IGF_SECTION_SIZE, "#!/bin/sh\n"
			 "dd if=/dev/mbr-part-header.dd of=/dev/%s bs=512 oflag=direct 2> /dev/null\n"
			 "dd if=/dev/gpt-suffix.dd of=/dev/%s bs=512 seek=%llu oflag=seek_bytes,direct 2> /dev/null\n"
			 "if [ -x /usr/bin/bar ]; then\n"
			 "     cat /dev/EFI.dd | bar -lpt \"Restoring old EFI\" -lpm \"Copying old EFI partitions to flash device\" -lpg 55555 -lpp 1 -i \"fw-update-old\" -s \"%llu\"| dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "     cat /dev/ddimage.dd /dev/zero | dd bs=1M iflag=count_bytes count=%llu bar -lpt \"Restoring old firmware\" -lpm \"Copying old firmware to flash device\" -lpg 55555 -lpp 1 -i \"fw-update-old\" -s \"%llu\" | dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "else\n"
			 "     dd if=/dev/EFI.dd of=/dev/%s bs=1M seek=%llu iflag=fullblock oflag=seek_bytes,direct 2> /dev/null\n"
			 "     cat /dev/ddimage.dd /dev/zero | dd of=/dev/%s bs=1M count=%llu seek=%llu iflag=count_bytes,fullblock oflag=seek_bytes,direct\n"
			 "fi\n"
			 "mkdir -p /mnt-efi2 && mount /dev/%s3 /mnt-efi2 || exit 0\n"
			 "rm -rf /mnt-efi2/migration_backup\n"
			 "umount /mnt-efi2 && rmdir /mnt-efi2\n",
			 init->devname, init->devname, (unsigned long long)(devsize - (34 * 512)),
			 (unsigned long long) efi_size, init->devname, (unsigned long long) efi_start,
			 (unsigned long long) size, (unsigned long long) size, init->devname,
			 (unsigned long long) start, init->devname, (unsigned long long) efi_start,
			 init->devname, (unsigned long long) size, (unsigned long long) start, init->devname);
	}

	if (iwrite(fd, buf, strlen((char *)buf)) != strlen((char *)buf))
	{
		msg(init, LOG_ERR, "init: failed to write /dev/recovery.sh file");
		free(buf);
		close(fd);
		return 1;
	}

	free(buf);
	fsync(fd);
	close(fd);

	return 0;
}
