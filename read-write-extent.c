#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>                           
#include <sys/types.h>                           
#include <sys/stat.h>                           
#include <fcntl.h>
#include <string.h>
#include "igel64/igel.h"
#include "init.h"


#ifdef EXTENT_TYPE_WRITEABLE

#if 0
#define DEBUGE msg
#else
#define DEBUGE(i, l, format, x...)
#endif

/*
 * returns 0 if read-write extent is present for given igf device name
 */

int check_read_write_extent_present(init_t *init, char *igf_name)
{
	char str[25];
	int fd, ret = -1, extent, not_found = 1;

	for (extent = 1; extent <= MAX_EXTENT_NUM; extent++) {
		fd = open_file_read_only("/sys/block/%s/igel/extent%d_type" , igf_name, extent);
		if (fd > 0) {
			memset(str, 0, sizeof(str));
			ret = read(fd, str, 24);
			close(fd);
		}

		if (ret < 0) {
			DEBUGE(init, LOG_ERR, "%s : Could not get extent type for extent number %d\n", igf_name, extent);
			return(-1);
		}

		if (strncmp(str,"read-write", 10) != 0) {
			DEBUGE(init, LOG_ERR, "%s : Extent %d not a read-write extent.\n", igf_name, extent);
		} else {
			not_found = 0;
			break;
		}
	}

	return not_found;
}

/*
 * read from read-write extent and save content to given target file
 */

int read_from_read_write_extent(init_t *init, char *igf_name, char *target_file, uint8_t extent, uint64_t pos, uint64_t size)
{
	unsigned char *buf;
	char str[25];
	uint64_t esize = 0, rsize = 0;
	int fd, dest_fd, ret;
	struct part_ext_read_write eread;

	ret = -1;
	fd = open_file_read_only("/sys/block/%s/igel/extent%d_type" , igf_name, extent);
	if (fd > 0) {
		memset(str, 0, sizeof(str));
		ret = read(fd, str, 24);
		close(fd);
	}
	if (ret < 0) {
		DEBUGE(init, LOG_ERR, "%s : Could not get extent type for extent number %d\n", igf_name, extent);
		return(-1);
	}

	if (strncmp(str,"read-write", 10) != 0) {
		 DEBUGE(init, LOG_ERR, "%s : Extent %d not a read-write extent.\n", igf_name, extent);
		return (-1);
	}

	ret = -1;
        fd = open_file_read_only("/sys/block/%s/igel/extent%d_size" , igf_name, extent);
        if (fd > 0) {
		memset(str, 0, sizeof(str));
                ret = read(fd, str, 24);
                close(fd);
        }

	if (ret < 0) {
		DEBUGE(init, LOG_ERR, "%s : Could not get extent size for extent number %d\n", igf_name, extent);
		return(-1);
	}

	esize = strtoul(str, (char **)NULL, 10);
	if (esize <= 0) {
		DEBUGE(init, LOG_ERR, "%s : Got invalid size for extent number %d\n", igf_name, extent);
		return(-1);
	}

	if (size == 0) {
		if (pos > esize) {
			DEBUGE(init, LOG_ERR, "%s : Error you try to read beyond end of extent %d.\n", igf_name, extent);
			return(-1);
		}
		if (pos != 0) {
			size = esize - pos;
		} else {
			size = esize;
		}
	} else if ((size + pos) > esize) {
		DEBUGE(init, LOG_ERR, "%s : Error you try to read beyond end of extent %d.\n", igf_name, extent);
		return(-1);
	}

	if ((fd = open_file_read_only("/dev/%s", igf_name)) < 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not open igf device.\n", igf_name);
		return(-1);
	}

	rsize = size;
	if (size > EXTENT_MAX_READ_WRITE_SIZE)
		rsize = EXTENT_MAX_READ_WRITE_SIZE;

	buf = malloc(rsize * sizeof(uint8_t));
	if (!buf) {
		DEBUGE(init, LOG_ERR, "%s : Error could not allocate (%llu Bytes) memory\n", igf_name, (unsigned long long)rsize);
		close(fd);
		return(-1);
	}

	if (access(target_file, F_OK) == 0 && unlink(target_file) != 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not delete %s file.\n", igf_name, target_file);
		free(buf);
		return(-1);
	}

	dest_fd = open_file_write_only("%s", target_file);
	if (dest_fd < 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not open %s file for writing.\n", igf_name, target_file);
		free(buf);
		return(-1);
	}

	eread.ext_num = extent - 1;
	eread.data = (uint8_t *) buf;
	while (size > 0) {
		if (size > EXTENT_MAX_READ_WRITE_SIZE)
			rsize = EXTENT_MAX_READ_WRITE_SIZE;
		else
			rsize = size;

		eread.pos = pos;
		eread.size = rsize;

		if(ioctl(fd, IGFLASH_READ_EXTENT, &eread) == -1){
			DEBUGE(init, LOG_ERR, "%s : Error while reading data from extent %d\n", igf_name, extent);
			close(dest_fd);
			close(fd);
			free(buf);
			return(-1);
		}

		if (iwrite(dest_fd, buf, rsize) != rsize) {
			DEBUGE(init, LOG_ERR, "%s : Error could not write data to %s file.\n", igf_name, target_file);
			close(dest_fd);
			close(fd);
			free(buf);
			return(-1);
		}
		size = size - rsize;
		pos += rsize;
	}

	free(buf);
	close(dest_fd);
	close(fd);
	return 0;
}

/*
 * write given source file to read-write extent
 */

int write_to_read_write_extent(init_t *init, char *igf_name, char *source_file, uint8_t extent, uint64_t pos, uint64_t size)
{
	unsigned char *buf;
	char str[25];
	uint64_t esize = 0, rsize = 0, fsize = 0;
	int fd, src_fd, ret;
	struct part_ext_read_write ewrite;
	struct stat      st;

	ret = -1;
	fd = open_file_read_only("/sys/block/%s/igel/extent%d_type" , igf_name, extent);
	if (fd > 0) {
		memset(str, 0, sizeof(str));
		ret = read(fd, str, 24);
		close(fd);
	}
	if (ret < 0) {
		DEBUGE(init, LOG_ERR, "%s : Could not get extent type for extent number %d\n", igf_name, extent);
		return(-1);
	}

	if (strncmp(str,"read-write", 10) != 0) {
		DEBUGE(init, LOG_ERR, "%s : Extent %d not a read-write extent.\n", igf_name, extent);
		return (-1);
	}

	ret = -1;
        fd = open_file_read_only("/sys/block/%s/igel/extent%d_size" , igf_name, extent);
        if (fd > 0) {
		memset(str, 0, sizeof(str));
                ret = read(fd, str, 24);
                close(fd);
        }

	if (ret < 0) {
		DEBUGE(init, LOG_ERR, "%s : Could not get extent size for extent number %d\n", igf_name, extent);
		return(-1);
	}

	esize = strtoul(str, (char **)NULL, 10);
	if (esize <= 0) {
		DEBUGE(init, LOG_ERR, "%s : Got invalid size for extent number %d\n", igf_name, extent);
		return(-1);
	}


	if ((size + pos) > esize) {
		DEBUGE(init, LOG_ERR, "%s : Error you try to write beyond end of extent %d.\n", igf_name, extent);
		return(-1);
	}

	if ((fd = open_file_read_only("/dev/%s", igf_name)) < 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not open igf device.\n", igf_name);
		return(-1);
	}

	src_fd = open_file_read_only("%s", source_file);
	if (src_fd < 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not open %s file for reading.\n", igf_name, source_file);
		close(fd);
		return(-1);
	}

	if (fstat(src_fd, &st) != 0) {
		DEBUGE(init, LOG_ERR, "%s : Error could not get size of %s file.\n", igf_name, source_file);
		close(fd);
		close(src_fd);
		return(-1);
	}

	fsize = st.st_size;

	if (size == 0) {
		size = fsize;
		if ((size + pos) > esize) {
			DEBUGE(init, LOG_ERR, "%s : Error you try to write beyond end of extent %d.\n", igf_name, extent);
			return(-1);
		}
	}
	
	rsize = size;
	if (size > EXTENT_MAX_READ_WRITE_SIZE)
		rsize = EXTENT_MAX_READ_WRITE_SIZE;

	buf = malloc(rsize * sizeof(uint8_t));
	if (!buf) {
		DEBUGE(init, LOG_ERR, "%s : Error could not allocate (%llu Bytes) memory\n", igf_name, (unsigned long long)rsize);
		close(fd);
		return(-1);
	}

	ewrite.ext_num = extent - 1;
	ewrite.data = (uint8_t *) buf;
	while (size > 0) {
		if (size > EXTENT_MAX_READ_WRITE_SIZE)
			rsize = EXTENT_MAX_READ_WRITE_SIZE;
		else
			rsize = size;

		ewrite.pos = pos;
		ewrite.size = rsize;

		if (iread(src_fd, buf, rsize) != rsize) {
			DEBUGE(init, LOG_ERR, "%s : Error could not read data from %s file.\n", igf_name, source_file);
			close(src_fd);
			close(fd);
			free(buf);
			return(-1);
		}

		if(ioctl(fd, IGFLASH_WRITE_EXTENT, &ewrite) == -1){
			DEBUGE(init, LOG_ERR, "%s : Error while writing data to extent %d\n", igf_name, extent);
			close(src_fd);
			close(fd);
			free(buf);
			return(-1);
		}

		size = size - rsize;
		pos += rsize;
	}

	free(buf);
	close(src_fd);
	close(fd);
	return 0;
}

#else

int write_to_read_write_extent(init_t *init, char *igf_name, char *source_file, uint8_t extent, uint64_t pos, uint64_t size)
{
	return (-1);
}

int read_from_read_write_extent(init_t *init, char *igf_name, char *target_file, uint8_t extent, uint64_t pos, uint64_t size)
{
	return (-1);
}

#endif /* EXTENT_TYPE_WRITEABLE */
