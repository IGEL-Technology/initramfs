#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "igel64/igel.h"
#include "init.h"

static int read_dir(int in_fd, struct directory *dir)
{
	off_t offset;
	uint32_t crc;

	/* crc offsets for directory header */
	static const uint32_t crc_dir_offset = (offsetof(struct directory, crc) + sizeof (((struct directory*)0)->crc));

	makecrc(); /* crc initial table setup */

	offset = DIR_OFFSET;

	if (lseek(in_fd, offset, SEEK_SET) == -1)
	{
		return 0;
	}
	if (iread(in_fd, (unsigned char *)dir, sizeof(struct directory)) != sizeof(struct directory))
	{
		return 0;
	}
	if (dir->magic != DIRECTORY_MAGIC)
		return 0;
	/*
	* calculate the checksum of the whole directory structure
	* except the first 8 bytes (magic and crc)
	*/
	(void) updcrc(NULL, 0); /* reset crc calculation state */
	crc = updcrc((uint8_t *)dir + crc_dir_offset, sizeof(struct directory) - crc_dir_offset);

	if (crc == dir->crc)
	{
		return (int)offset;
	}

	return 0;
}

/*
 * function to delete partitions in a disk image this is mostly used to reduce
 * the size of the OSC ddimage if some things are not needed (like nvidia or
 * JAVA)
 */

int delete_parts(init_t *init, int in_fd, int out_fd, int num, int *list)
{
	unsigned char *buf;
	int i, s, n, t, d, n_frags, minor[DIR_MAX_MINORS];
	struct directory dir;
	unsigned char *pdir;
	struct fragment_descriptor *fragments, dst_frags[DIR_MAX_MINORS];
	uint16_t type[DIR_MAX_MINORS];
	uint64_t n_sections = 1;
	uint64_t curr_section = 0;
	struct igf_sect_hdr *sect_hdr;
	struct igf_part_hdr *part_hdr;
	struct directory gdir;

	/* TODO Fallback if directory is damaged */
	if (read_dir(in_fd, &gdir) == 0)
	{
		msg(init,LOG_ERR,"Unable to reade the partition directory\n");
		return (-1);
	}

	/* allocate buffer for section copying */
	buf = (unsigned char *) malloc(sizeof(unsigned char)* IGF_SECTION_SIZE);
	if (! buf) {
		msg(init,LOG_ERR, "Could not alloc %lu bytes of memory\n", (unsigned long) IGF_SECTION_SIZE);
		return (-1);
	}
	/* zero out section buffer */
	bzero(buf, IGF_SECTION_SIZE);

	/* jump to position of BOOTREG on disk */
	if (lseek(in_fd, IGEL_BOOTREG_OFFSET, SEEK_SET) == -1) {
		msg(init,LOG_ERR, "Error while seeking input file to pos %llu\n", (unsigned long long) IGEL_BOOTREG_OFFSET);
		free(buf);
		return (-1);
	}
	/* read BOOTREG from disk to buffer (no check if bootreg is valid) */
	n = iread(in_fd, buf + IGEL_BOOTREG_OFFSET, IGEL_BOOTREG_SIZE);
	if (n != IGEL_BOOTREG_SIZE) {
		msg(init,LOG_ERR, "Could not read bootreg section\n");
		free(buf);
		return (-1);
	}

	/* write first section which means write a empty first section with only
	 * the former read BOOTREGISTRY in it. Which means the directory area is
	 * completely zerod out */
	n = iwrite(out_fd, buf, IGF_SECTION_SIZE);
	if (n != IGF_SECTION_SIZE) {
		msg(init,LOG_ERR, "Could not write 1st section\n");
		free(buf);
		return (-1);
	}

	/* Initialize CRC32 */
	makecrc();

	/* set sect_hdr pointer to start of buffer */
	sect_hdr = (struct igf_sect_hdr *) buf;

	part_hdr = (struct igf_part_hdr *) (buf + (uintptr_t)IGF_SECT_HDR_LEN);

	/* Loop over all possible minors */
	d=0;
	for (i=1;i<DIR_MAX_MINORS;i++)
	{
		/* loop over deleted partitions */
		int found = 0;
		for (t=0;t<num;t++) {
			if (i == list[t]) {
				found = 1;
				break;
			}
		}

		if (found) {
			msg(init,LOG_ERR, "Ignoring minor %lu\n", (unsigned long) i);
			continue;
		}

		/* a partition is present if n_frags for this partition is > 0 */

		if ((n_frags = gdir.partition[i].n_fragments) > 0) {
			curr_section = 0;
			/* minor is needed later for writting directory structure */
			minor[d] = i;

			/* set first section of fragment to current section (n_sections) */
			dst_frags[d].first_section = n_sections;

			/* get first fragment struct of partition in source from global directory */

			fragments = &(gdir.fragment[gdir.partition[i].first_fragment]);
			for (t=0; t<n_frags; t++) {
				if (lseek(in_fd, (fragments[t].first_section * IGF_SECTION_SIZE), SEEK_SET) == -1) {
					msg(init,LOG_ERR, "Error while seeking input file to pos %llu\n", (unsigned long long) (fragments[t].first_section * IGF_SECTION_SIZE));
					free(buf);
					return (-1);
				}
				for (s=0;s<fragments[t].length;s++) {
					n = iread(in_fd, buf, IGF_SECTION_SIZE);
					if (n != IGF_SECTION_SIZE) {
						msg(init,LOG_ERR, "Error while reading %lu bytes (read %lu) from input file\n", (unsigned long) IGF_SECTION_SIZE, (unsigned long) n);
						free(buf);
						return (-1);
					}
					if (curr_section == 0) {
						type[d] = part_hdr->type;
					}
					sect_hdr->section_in_minor = curr_section;
					sect_hdr->generation = 1;
					/* detect last section and mark it with setting next_section to 0xffffffff */
					if (t + 1 >= n_frags && s + 1 >= fragments[t].length) {
						sect_hdr->next_section = 0xffffffff;
						++n_sections;
					} else { 
						sect_hdr->next_section = ++n_sections;
					}

					/* generate CRC for section header */
					(void) updcrc(NULL, 0);
					sect_hdr->crc = updcrc(buf + sizeof(uint32_t),
						IGF_SECTION_SIZE-sizeof(uint32_t));

					n = iwrite(out_fd, buf, IGF_SECTION_SIZE);
					if (n != IGF_SECTION_SIZE) {
						msg(init,LOG_ERR, "Error while writing %lu bytes (written %lu) to output file\n", (unsigned long) IGF_SECTION_SIZE, (unsigned long)n);
						free(buf);
						return (-1);
					}
					curr_section++;
				}
			}
			dst_frags[d++].length = curr_section;
		}
	}

	 makecrc();

        /* create initial directory */

        bzero(&dir, sizeof(struct directory));
        dir.magic = DIRECTORY_MAGIC;
        dir.crc = CRC_DUMMY;
        dir.dir_type = 0;
        dir.max_minors = DIR_MAX_MINORS;
        dir.version = 1;
        dir.n_fragments = 0;
        dir.max_fragments = MAX_FRAGMENTS;
        for (i = 0; i < 8; i++)
                dir.extension[i] = 0;

        /* Initialize the freelist */
        dir.partition[0].minor = 0;
        dir.partition[0].type = PTYPE_IGEL_FREELIST;
        dir.partition[0].first_fragment = 0;
        dir.partition[0].n_fragments = 0;

	/* add all partitions to directory structures */

	for (i=0;i<d;i++) {
		dir.partition[minor[i]].minor = minor[i];
		dir.partition[minor[i]].type = type[i];
		dir.partition[minor[i]].first_fragment = dir.n_fragments;
		dir.partition[minor[i]].n_fragments = 1;
		dir.fragment[dir.n_fragments].first_section = dst_frags[i].first_section;
		dir.fragment[dir.n_fragments].length = dst_frags[i].length;
		dir.n_fragments++;
	}

	/* set directory version */

	dir.version = 1;

	free(buf);

	/* Update directory CRC and write directory to ddimage */

	(void) updcrc(NULL, 0);
	pdir = (unsigned char *) &dir;
	pdir += 8;
	dir.crc = updcrc((void *)pdir, sizeof(struct directory) - 8);

	if (lseek(out_fd, DIR_OFFSET, SEEK_SET) == -1) {
		msg(init,LOG_ERR, "Could not seek to start of ddimage\n");
		return (-1);
	}

	if (iwrite(out_fd, (unsigned char *)&dir, sizeof(struct directory)) != sizeof(struct directory))
	{
		msg(init,LOG_ERR, "Could not write %lu bytes to start of ddimage\n", (unsigned long) sizeof(struct directory));
		return (-1);
	}

	return 0;
}

