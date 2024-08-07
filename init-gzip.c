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
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <zlib.h>
#include "init.h"


static void usage(void)
{
	printf("init-gzip -i <file> [-f <outfile>] [-l <gzip level>] [-o <offset in bytes>] [-s <size in bytes>]\n");
}

static struct option prog_options[] =
{
	{ "infile"  ,1, 0, 'i'},
	{ "outfile" ,1, 0, 'f'},
	{ "offset"  ,1, 0, 'o'},
	{ "size"    ,1, 0, 's'},
	{ "level"   ,1, 0, 'l'},
	{ "help"    ,0, 0, 'h'},
	{ NULL      ,0, 0,  0 }
};

int main(int argc, char **argv)
{
	int i, ret, option_index = 0;
	char *infile = NULL, *outfile = NULL;
	uint64_t size = 0, offset = 0;
	long long value = 0;
	int level = Z_DEFAULT_COMPRESSION;

	/* get options */
	while ((i = getopt_long(argc, argv, "hf:i:o:s:l:", prog_options,
		&option_index)) != -1) {
		switch(i) {
			case 'i':
				infile = optarg;
				break;
			case 'f':
				outfile = strdup(optarg);
				break;
			case 'h':
				usage();
				return(0);
				break;
			case 'o':
				value = strtoll(optarg, NULL, 10);
				if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
					usage();
					return(-1);
				}
				offset = (uint64_t)value;
				break;
			case 's':
				value = strtoll(optarg, NULL, 10);
				if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
					usage();
					return(-1);
				}
				size = (uint64_t)value;
				break;
			case 'l':
				value = strtoll(optarg, NULL, 10);
				if ((value == LLONG_MIN || value == LLONG_MAX) && (errno == ERANGE)) {
					usage();
					return(-1);
				}
				level = (int)(value & 0xFF);
				if (level > 9) {
					level = 9;
				} else if (level < 1) {
					level = 1;
				}
				break;
			default:
				usage();
				return(-1);
				break;
		}
	}

	if (!infile)
	{
		fprintf(stderr, "Error: no file given.\n");
		usage();
		return(-1);
	}

	if (!outfile) {
		if (asprintf(&outfile, "%s.gz", infile) < 0) {
			fprintf(stderr, "Error: asprintf failed.\n");
			return(-1);
		}
	}

	ret = compress_file_enhanced(infile, outfile, level, offset, size);

	free(outfile);

	return ret;
}

