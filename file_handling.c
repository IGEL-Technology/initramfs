#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include "init.h"
#include <sys/stat.h>
#include <fnmatch.h>
#include <stdint.h>
#include <limits.h>

static int _grep_file(char *search_string, int match, const char* format, va_list list);

/*
 * helper functions adopted from clone2fs and a little bit changed
 * should make reading and writing much more robust.
 */

ssize_t
iread(int fd, unsigned char *buf, size_t len) {
	ssize_t n, l = len;

	while (len) {
		n = read(fd, buf, len);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return -1;
		}
		if (n == 0) {
			return l - len;
		}
		buf += n;
		len -= n;
	}
	return l;
}

ssize_t
iwrite(int fd, const unsigned char *buf, size_t len) {
	ssize_t n, l = len;

	while (len) {
		n = write(fd, buf, len);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return l - len;
		}
		if (n == 0) {
			return l - len;
		}
		buf += n;
		len -= n;
	}
	return l;
}

ssize_t
ipread(int fd, unsigned char *buf, size_t len, off_t offset) {
	ssize_t n, l = len;

	while (len) {
		n = pread(fd, buf, len, offset);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return -1;
		}
		if (n == 0) {
			return l - len;
		}
		buf += n;
		len -= n;
	}
	return l;
}

ssize_t
ipwrite(int fd, const unsigned char *buf, size_t len, off_t offset) {
	ssize_t n, l = len;

	while (len) {
		n = pwrite(fd, buf, len, offset);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			return l - len;
		}
		if (n == 0) {
			return l - len;
		}
		buf += n;
		len -= n;
	}
	return l;
}

/*
 * grep for a string in a file
 *
 * returns 1  if matching string was found
 * returns 0  if no matching string was found
 * returns <0 in case of errors
 */

int
grep_file(char *search_string, int ignore_case, const char* format, ...)
{
	int ret = 0;
	va_list list;

	va_start(list, format);
	if (ignore_case == 0) {
		ret = _grep_file(search_string, STRING_COMPARE, format, list);
	} else {
		ret = _grep_file(search_string, STRING_NOCASE_COMPARE, format, list);
	}
	va_end(list);

	return ret;
}

/*
 * grep for a kernel module name in a file
 *
 * returns 1  if matching string was found
 * returns 0  if no matching string was found
 * returns <0 in case of errors
 */

int
grep_kernel_module_in_file(char *search_string, const char* format, ...)
{
	int ret = 0;
	va_list list;

	va_start(list, format);
	ret = _grep_file(search_string, STRING_MODULE_COMPARE, format, list);
	va_end(list);

	return ret;
}

static int
_grep_file(char *search_string, int match, const char* format, va_list list)
{
	FILE		*fp = NULL;
	char	 	*filename = NULL;
	char	 	*line = NULL;
	struct stat 	 st;

	if (search_string == NULL)
		return -1;

	if (vasprintf(&filename, format, list) < 0) {
		va_end(list);
		return -2;
	}

	if (stat(filename,&st) != 0 || S_ISDIR(st.st_mode)) {
		free(filename);
		return -3;
	}

	fp = fopen(filename, "r");
	free(filename);
	line = malloc(4096);
	if (line == NULL) {
		fclose(fp);
		return -4;
	}
	if (match == STRING_NOCASE_COMPARE) {
		while (fgets(line, 4096, fp) != NULL) {
			if (strcasestr(line, search_string) != NULL) {
				free(line);
				fclose(fp);
				return 1;
			}
		}
	} else if (match == STRING_COMPARE) {
		while (fgets(line, 4096, fp) != NULL) {
			if (strstr(line, search_string) != NULL) {
				free(line);
				fclose(fp);
				return 1;
			}
		}
	} else if (match == STRING_MODULE_COMPARE) {
		while (fgets(line, 4096, fp) != NULL) {
			if (search_match_module(line, search_string) == 0) {
				free(line);
				fclose(fp);
				return 1;
			}
		}
	}
	free(line);
	fclose(fp);
	return 0;
}

int
open_file_read_only(const char* format, ...)
{
	va_list		 list;
	int		 fd = 0;
	char	 	*filename = NULL;
	struct stat 	 st;

	va_start(list, format);

	if (vasprintf(&filename, format, list) < 0) {
		va_end(list);
		return -1;
	}

	va_end(list);

	if (stat(filename,&st) != 0 || S_ISDIR(st.st_mode)) {
		free(filename);
		return -2;
	}

	fd = open(filename, O_RDONLY);
	free(filename);
	if (fd < 0)
		return -3;

	return fd;
}

int
open_file_write_only(const char* format, ...)
{
	va_list		 list;
	int		 fd = 0;
	char	 	*filename = NULL;
	char	 	*dirname = NULL;
	char		*p;
	struct stat 	 st;

	va_start(list, format);

	if (vasprintf(&filename, format, list) < 0) {
		va_end(list);
		return -1;
	}

	dirname = strdup(filename);
	if (!dirname) {
		free(filename);
		va_end(list);
		return -1;
	}

	va_end(list);

	p = strrchr(dirname, (int)'/');
	if (p != NULL) {
		*p = '\0';
		if (stat(dirname,&st) != 0) {
			mkdir(dirname, 0755);
		}

		if (stat(dirname,&st) != 0 || ! S_ISDIR(st.st_mode)) {
			free(dirname);
			free(filename);
			return -2;
		}
	}
	free(dirname);

	fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	free(filename);
	if (fd < 0)
		return -3;

	return fd;
}

int
file_exists(const char *format, ...)
{
	va_list list;
	struct stat st;
	char *filename;
	int ret = 0;

	va_start(list, format);

	if (vasprintf(&filename, format, list) < 0) {
		va_end(list);
		return -1;
	}

	if (lstat(filename,&st)==0)
		ret = 1;

	free(filename);
	
	return ret;
}

char *read_file (int len, char *buffer, int buf_len, const char* format, ...)
{
	va_list          list;
	int              fd = 0;
	char            *filename = NULL;
	long             read = 0;
	struct stat      st;

	if (buffer == NULL)
		buf_len = 0;

	if (len >= buf_len)
		len = buf_len - 1;

	if (len <= 0) {
		if (buf_len > 0)
			buffer[read] = '\0';
		else
			buffer = strdup("");
		return buffer;
	}

	va_start(list, format);

	if (vasprintf(&filename, format, list) < 0) {
		va_end(list);
		return NULL;
	}

	va_end(list);

	if (stat(filename,&st) != 0 || S_ISDIR(st.st_mode)) {
		free(filename);
		return NULL;
	}

	fd = open(filename, O_RDONLY);
	free(filename);
	if (fd < 0)
		return NULL;

	if (buf_len <= 0)
		buffer = malloc(len);

	if (!buffer) {
		close(fd);
		return NULL;
	}

	if (! (read = iread (fd, (unsigned char *)buffer, len))) {
		close(fd);
		if (buf_len <= 0 && buffer) {
			free(buffer);
		}
		return NULL;
	}

	buffer[read] = '\0';
	close(fd);

	return buffer;
}
