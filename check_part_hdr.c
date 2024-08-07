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

#define SECTION_IMAGE_CRC_START_V5 (2*sizeof(uint32_t))
#define SECTION_IMAGE_CRC_START_V6 (sizeof(uint32_t))
#define IGF_SECTION_SIZE_V5  0x10000L	/* 64 K */
#define IGF_SECTION_SIZE_V6  0x40000L	/* 256 K */

struct igf_sect_hdr_v5
{
	uint32_t magic;		/* magic number (was erase count!) */
	uint32_t crc;		/* crc of the rest of the section */
	uint32_t partition;	/* the partition we belong to     */
	uint8_t section_in_minor;	/* n = 0,...,(number of sect.-1)  */
	uint16_t version;		/* the version of "partition"     */
	int16_t next_section;		/* index of the next section of   */
					/* "partition", -1 otherwise      */
} __attribute__((packed));

struct igf_sect_hdr_v6
{
	uint32_t crc;			/* crc of the rest of the section         */
	uint32_t magic;			/* magic number (erase count long ago)    */
	uint16_t section_type;
	uint16_t section_size;		/* log2((section size in bytes)/65536)    */
	uint32_t partition_minor;	/* partition number (driver minor number) */
	uint16_t generation;		/* update generation count                */
	uint32_t section_in_minor;	/* n = 0,...,(number of sect.-1)          */
	uint32_t next_section;		/* index of the next section or           */
					/* 0xffffffff = end of chain              */
} __attribute__((packed));

/*
 * function checks if a given file or device is a v5 or v6 igel partition
 *
 * returns: 5 for Linux V5 partitions
 *	    6 for Linux 10 partitions
 *	    0 for unknown (not found)
 *	   -1 for errors or Linux V5 and 10 CRC found
 */

int
check_igel_part(char *filename)
{
	unsigned char *buf;
	int fd, i, t, size;
	uint32_t crc;
	struct igf_sect_hdr_v5 *hdr_v5 = NULL;
	struct igf_sect_hdr_v6 *hdr_v6 = NULL;
	int found_v6 = 0, found_v5 = 0;

	size = IGF_SECTION_SIZE_V6;

	if (size < IGF_SECTION_SIZE_V5)
		size = IGF_SECTION_SIZE_V5;

	buf = (unsigned char*) malloc(size * sizeof(unsigned char));
	if (! buf) {
		return -1;
	}

	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		free(buf);
		return -1;
	}

	makecrc();


	for (i=0;i<1024;i++) {
		if (read (fd, (char *)buf, size) < size) {
			free(buf);
			close(fd);
			return -1;
		}

		/* ignore first block */

		if (i == 0)
			continue;

		/* check Linux 10 CRC */

		for (t=0; t<size/IGF_SECTION_SIZE_V6; t++) {
			(void) updcrc(NULL, 0);
			hdr_v6 = (struct igf_sect_hdr_v6 *) (buf + (t * IGF_SECTION_SIZE_V6));
			crc = updcrc(buf + SECTION_IMAGE_CRC_START_V6 + (t * IGF_SECTION_SIZE_V6),
				     IGF_SECTION_SIZE_V6-SECTION_IMAGE_CRC_START_V6);
			if (crc == hdr_v6->crc) {
				found_v6++;
			}
		}
		
		/* check Linux 5 CRC */

		for (t=0; t<size/IGF_SECTION_SIZE_V5; t++) {
			(void) updcrc(NULL, 0);
			hdr_v5 = (struct igf_sect_hdr_v5 *) (buf + (t * IGF_SECTION_SIZE_V5));
			crc = updcrc(buf + SECTION_IMAGE_CRC_START_V5 + (t * IGF_SECTION_SIZE_V5),
				     IGF_SECTION_SIZE_V5-SECTION_IMAGE_CRC_START_V5);
			if (crc == hdr_v5->crc) {
				found_v5++;
			}
		}

		/* if two times a correct header (CRC valid) was found we see the partition as correctly detected */
		
		if (found_v5 >= 2 || found_v6 >= 2) {
			free(buf);
			close(fd);
			if (found_v5 == 0) {
				return 6;
			} else if (found_v6 == 0) {
				return 5;
			}
			return -1;
		}
	}

	free(buf);
	close(fd);

	return 0;
}

