/**
 **  strip_ddimage.c
 **
 **  Tool for deleteing given partitions from a ddimage.bin
 **  Example:
 **
 **  strip_ddimage -d 29 -i ddimage.bin -o ddimage.new
 **
 **  generates a ddimage.new without partition 29
 **
 **  November 2017, IGEL Technology GmbH, Stefan Gottwald
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <igel.h>
#include "init.h"

/* imports from igel_crc.c */
extern uint32_t updcrc(unsigned char *s, unsigned n);
extern void makecrc(void);

static struct option prog_options[] =
{
	{ "infile"        ,1, 0, 'i'},
	{ "outfile"       ,1, 0, 'o'},
	{ "delete"        ,1, 0, 'd'},
	{ "help"          ,0, 0, '?'},
	{ NULL            ,0, 0,  0 }
};

static void usage(void)
{
	printf("Usage: strip_ddimage -d <minor> [-d <minor>] -i <file> -o <file>\n");
	printf("\n");
	printf("       -d <minor> : the IGEL partition minor which should be removed\n");
	printf("       -i <file>  : the input file to process\n");
	printf("       -o <file>  : the output file\n");
}


int main (int argc, char **argv)
{
	int i, t;
	int option_index = 0;
	int partition[255], num_partitions = 0;
	char *in = NULL;
	char *out = NULL;
	int in_fd, out_fd;
	int err = 0;
	
	while ((i = getopt_long(argc, argv, "i:o:d:h", prog_options,
		&option_index)) != -1)
	{	
		switch (i)
		{
			case 'i':
				free(in);
				in = malloc(strlen(optarg)+1);
				strcpy(in, optarg);
				break;
			case 'o':
				free(out);
				out = malloc(strlen(optarg)+1);
				strcpy(out, optarg);
				break;
			case 'd':
				t = atoi(optarg);
				if (t <= 1 || t > 255) {
					fprintf(stderr, "Given partition %d out of range\n", t);
					usage();
					err = -1;
					goto out_mem;
				}
				partition[num_partitions] = t;
				num_partitions++;
				break;
			case 'h':
			default:
				usage();
				goto out_mem;
		}
	}

	if (in == NULL)
	{
	 	fprintf(stderr, "Input filename is missing\n");
		usage();
		err = -1;
		goto out_mem;
	}
	else if (out == NULL)
	{
	 	fprintf(stderr, "Output filename is missing\n");
		usage();
		err = -1;
		goto out_mem;
	}
	else if (num_partitions < 1)
	{
	 	fprintf(stderr, "No partitions to delete were given\n");
		usage();
		err = -1;
		goto out_mem;
	}

	in_fd = open(in, O_RDONLY);

	if (in_fd < 0) {
		fprintf(stderr, "Could not open input file %s\n", in);
		err = -1;
		goto out_mem;
	}

	/* delete file before writting to it otherwise old content could survive */

	unlink(out);

	out_fd = open(out, O_RDWR|O_CREAT, 0644);
	
	if (out_fd < 0) {
		fprintf(stderr, "Could not open output file %s\n", out);
		err = -1;
		goto out_in;
	}

	err = delete_parts(NULL, in_fd, out_fd, num_partitions, partition);

	close(out_fd);

out_in:
	close(in_fd);
out_mem:
	free(in);
	free(out);
	return err;

}

