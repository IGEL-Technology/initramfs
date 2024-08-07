/*
 * initramfs init program for kernel 4.10.x.
 * handle gzip compression and decompression.
 * Copyright (C) by IGEL Technology GmbH 2017
 * @author: Stefan Gottwald

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


#define _LARGEFILE64_SOURCE
#define _LARGEFILE_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#ifndef BLKFLSBUF
#include <linux/fs.h>
#endif
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <zlib.h>
#include "init.h"

#define CHUNK 0x8000
#define windowBits 15
#define GZIP_ENCODING 16
#define ENABLE_ZLIB_GZIP 32

/*
 * helper function to check if filename contains .gz suffix
 *
 * returns 1 if suffix is .gz and 0 otherwise
 */

int check_gz (const char *string)
{
	int len;

	len = strlen(string);
	if (string[len-3] == '.'
	    && string[len-2] == 'g'
	    && string[len-1] == 'z')
		return 1;

	return 0;
}

/*
 * write compressed data to given output file descriptor
 */

static int write_compress (z_stream *stream, int chunk, int flush,
			   unsigned char *out, int wr)
{
	int to_write = 0;
	int ret = 0;
	static ssize_t written = 0;

	do {
		stream->avail_out = chunk;
		stream->next_out = out;
		ret = deflate(stream, flush);
		if (ret != Z_STREAM_ERROR) {
			to_write = chunk - stream->avail_out;
			if (iwrite(wr, out, to_write) != to_write) {
				deflateEnd (stream);
				return 1;
			}
			written += to_write;

			/* sync all 10 MByte */

			if (written >= 10 * 1024 * 1024) {
				fsync(wr);
				written = 0;
			}
		} else {
			return 1;
		}
	} while (stream->avail_out == 0 || (flush == Z_FINISH && (ret == Z_OK || ret == Z_BUF_ERROR)));

	if (flush == Z_FINISH) {
		if (ret != Z_STREAM_END)
			return 1;
		deflateEnd (stream);
	}

	return 0;
}

/*
 * write decompressed data to given output file descriptor
 *
 * return values: 0 -> completed, 1 -> more data, 2 -> error
 */

static int write_decompress (z_stream *stream, int chunk,
			     unsigned char *out, int wr)
{
	int to_write = 0;
	int ret;
	static ssize_t written = 0;

	do {
		stream->avail_out = chunk;
		stream->next_out = out;
		ret = inflate(stream, Z_NO_FLUSH);
		if (ret == Z_STREAM_ERROR
		    || ret == Z_NEED_DICT
		    || ret == Z_DATA_ERROR
		    || ret == Z_MEM_ERROR) {
			inflateEnd(stream);
			return 1;
		}
		to_write = chunk - stream->avail_out;
		if (iwrite(wr, out, to_write) != to_write) {
			inflateEnd (stream);
			return 1;
		}
		written += to_write;

		/* sync all 10 MByte */

		if (written >= 10 * 1024 * 1024) {
			fsync(wr);
			written = 0;
		}
	} while (stream->avail_out == 0);

	if (ret == Z_STREAM_END) {
		inflateEnd (stream);
		return 0;
	}
	return 1;
}

/*
 * helper function to avoid goto constructs, used to clean up everything
 */

static void cleanup (int fd, int wr, unsigned char *in, unsigned char *out)
{
	if (fd >= 0)
		close(fd);
	if (wr >= 0) {
		fsync(wr);
		close(wr);
	}

	if (in) {
		free(in);
		in = NULL;
	}

	if (out) {
		free(out);
		out = NULL;
	}
}

/*
 * compress (gzip format) given input src file to given target file
 */

int compress_file(const char *srcfile, const char *trgtfile)
{
	return compress_file_enhanced(srcfile, trgtfile, Z_DEFAULT_COMPRESSION, 0ULL, 0ULL);
}

int compress_file_enhanced(const char *srcfile, const char *trgtfile, int compress_level, uint64_t pos, uint64_t size)
{
	unsigned char	*in = NULL;
	unsigned char 	*out = NULL;
	int		 fd = -1;
	int		 wr = -1;
	int		 len;
	z_stream	 stream;
	struct stat	 st;
	int		 chunk = CHUNK;
	int		 rsize = 2 * CHUNK;
	int		 flush = Z_NO_FLUSH;
	ssize_t		 fsize = 0;
	int		 no_size = 0;

	if (stat(srcfile, &st) != 0)
		return 1;

	if (size == 0) {
		fsize = 0;
		if S_ISREG(st.st_mode) {
			fsize = st.st_size;
		} else if S_ISBLK(st.st_mode) {
			fd = open64(srcfile, O_RDONLY);
			if (fd > 0) {
				if (ioctl(fd,BLKGETSIZE64,&fsize) < 0) {
					fsize = 0;
				}
				close(fd);
			}
		}
	} else {
		fsize = size;
	}

	if (fsize == 0) {
		no_size = 1;
	} else {
		if (fsize < chunk) {
			chunk = fsize;
		}
		if (fsize < rsize) {
			rsize = fsize;
		}
	}

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	if (deflateInit2 (&stream, compress_level, Z_DEFLATED,
                             windowBits | GZIP_ENCODING, 8,
                             Z_DEFAULT_STRATEGY) != Z_OK) {
		cleanup(fd, wr, in, out);
		return 2;
	}

	fd = open64(srcfile, O_RDONLY);

	if (fd < 0) {
		cleanup(fd, wr, in, out);
		return 3;
	}

	if (pos != 0) {
		if (lseek64(fd, pos, SEEK_SET) != pos) {
			cleanup(fd, wr, in, out);
			return 1;
		}
	}

	wr = open64(trgtfile, O_WRONLY | O_CREAT | O_TRUNC, 0664);

	if (wr < 0) {
		cleanup(fd, wr, in, out);
		return 4;
	}

	chunk = deflateBound(&stream, rsize);

	out = (unsigned char *) malloc(chunk * sizeof(unsigned char));
	if (!out) {
		cleanup(fd, wr, in, out);
		return 5;
	}

	in = (unsigned char *) malloc(rsize * sizeof(unsigned char));
	if (!in) {
		cleanup(fd, wr, in, out);
		return 6;
	}

	while ((fsize > 0 || no_size != 0) && (len = iread(fd, in, rsize)) > 0) {
		stream.next_in = in;
		stream.avail_in = len;
		fsize -= len;
		if (fsize <= 0 && no_size == 0) {
			flush = Z_FINISH;
		}
	
		if (write_compress(&stream, chunk, flush, out, wr) != 0) {
			cleanup(fd, wr, in, out);
			unlink(trgtfile);
			return 7;
		}
	}

	if (flush != Z_FINISH) {
		stream.next_in = in;
		stream.avail_in = 0;
		if (write_compress(&stream, chunk, Z_FINISH, out, wr) != 0) {
			cleanup(fd, wr, in, out);
			unlink(trgtfile);
			return 8;
		}
	}
	cleanup(fd, wr, in, out);
	return 0;
}

/*
 * decompress (gzip format) given input src file to given target file
 */

int decompress_file(const char *srcfile, const char *trgtfile)
{
	unsigned char	*in = NULL;
	unsigned char 	*out = NULL;
	int		 fd = -1;
	int		 wr = -1;
	int		 len;
	z_stream	 stream;
	struct stat	 st;
	int		 chunk = CHUNK;
	int		 ret = 1;

	if (stat(srcfile, &st) != 0)
		return 1;

	if S_ISREG(st.st_mode) {
		if (st.st_size < chunk) {
			chunk = st.st_size;
		}
	}

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	stream.avail_in = 0;
	stream.next_in = Z_NULL;
	if (inflateInit2 (&stream, windowBits | ENABLE_ZLIB_GZIP) != Z_OK)
	{
		cleanup(fd, wr, in, out);
		return 1;
	}

	fd = open64(srcfile, O_RDONLY);

	if (fd < 0) {
		cleanup(fd, wr, in, out);
		return 1;
	}

	wr = open64(trgtfile, O_WRONLY | O_CREAT | O_TRUNC, 0664);

	if (wr < 0) {
		cleanup(fd, wr, in, out);
		return 1;
	}

	out = (unsigned char *) malloc(chunk * sizeof(unsigned char));
	if (!out) {
		cleanup(fd, wr, in, out);
		return 1;
	}

	in = (unsigned char *) malloc(chunk * sizeof(unsigned char));
	if (!in) {
		cleanup(fd, wr, in, out);
		return 1;
	}

	ret = 1;
	while ((len = iread(fd, in, chunk)) > 0 && ret != 0) {
		stream.next_in = in;
		stream.avail_in = len;
		ret = write_decompress(&stream, chunk, out, wr);
		if (ret == 2) {
			cleanup(fd, wr, in, out);
			unlink(trgtfile);
			return 1;
		}
	}
	if (ret != 0) {
		cleanup(fd, wr, in, out);
		unlink(trgtfile);
		return 1;
	}

	cleanup(fd, wr, in, out);
	return 0;
}


