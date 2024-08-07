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
	printf("init-systool [-f <pci vendor>] [-d <dmi field>] [-s <sysfs path>] [-b <blockdev> -e <entry> [-p <partnum>]\n");
}

static struct option prog_options[] =
{
	{ "find-pci"   ,1, 0, 'f'},
	{ "get-dmi"    ,1, 0, 'd'},
	{ "blockdev"   ,1, 0, 'b'},
	{ "entry"      ,1, 0, 'e'},
	{ "partnum"    ,1, 0, 'p'},
	{ "sysfs-path" ,1, 0, 's'},
	{ "help"       ,0, 0, 'h'},
	{ NULL         ,0, 0,  0 }
};

static void free_list(struct vendor_list *list)
{
	struct vendor_list *p;

	if (!list)
		return;
	p = list->next;
	do {
		free(list);
		list = p;
	} while (list);
}


int main(int argc, char **argv)
{
	int i, option_index = 0;
	char *dmi = NULL, *blkdev = NULL, *entry = NULL, *partnum = NULL, *syspath = NULL;
	char buffer[255], *p;
	struct vendor_list *pci_vendor = NULL, *l = NULL;

	/* get options */
	while ((i = getopt_long(argc, argv, "hf:d:b:e:p:s:", prog_options,
		&option_index)) != -1) {
		switch(i) {
			case 'f':
				if (!l) {
					pci_vendor = malloc(sizeof(struct vendor_list));
					if (!pci_vendor) {
						return(-1);
					}
					l = pci_vendor;
				} else {
					l->next = malloc(sizeof(struct vendor_list));
					if (!l->next) {
						free_list(pci_vendor);
						return(-1);
					}
					l = l->next;
				}
				strncpy(l->name, optarg, 8);
				break;
			case 'd':
				dmi = optarg;
				break;
			case 'b':
				blkdev = optarg;
				break;
			case 'e':
				entry = optarg;
				break;
			case 'p':
				partnum = optarg;
				break;
			case 's':
				syspath = optarg;
				break;
			case 'h':
				usage();
				return(0);
				break;
			default:
				usage();
				return(-1);
				break;
		}
	}

	if (syspath) {
		p = get_sysfs_entry(buffer, 255, "%s", syspath);
		if (!p) {
			printf("Could not get content of \"%s\".\n", syspath);
		} else {
			printf("Syspath \"%s\" has value: %s\n", syspath, p);
		}
	}
	if (pci_vendor) {
		if (find_pci_vendors(pci_vendor) == 1) {
			printf("Found PCI vendor %s on system.\n", pci_vendor->name);
		}
		free_list(pci_vendor);
	}

	if (dmi) {
		p = get_dmi_data(dmi, buffer, 255);
		if (!p) {
			printf("No DMI entry %s found.\n", dmi);
		} else {
			printf("DMI entry %s has value: %s\n", dmi, p);
		}
	}

	if (blkdev && !entry) {
		printf("Given blockdev %s but no entry to look for.\n", blkdev);
		return (-1);
	} else if (!blkdev && entry) {
		printf("Given entry %s but no blockdev to look into.\n", entry);
		return (-1);
	} else if (blkdev && entry) {
		if (partnum) {
			p = get_block_partition_data(blkdev, atoi(partnum), entry, buffer, 255);
			if (!p) {
				printf("No Entry %s for blockdev %s partition %s found.\n", entry, blkdev, partnum);
			} else {
				printf("Entry %s for blockdev %s partition %s has value: %s\n", entry, blkdev, partnum, p);
			}
		} else {
			p = get_block_data(blkdev, entry, buffer, 255);
			if (!p) {
				printf("No Entry %s for blockdev %s found.\n", entry, blkdev);
			} else {
				printf("Entry %s for blockdev %s has value: %s\n", entry, blkdev, p);
			}
		}
	}

	return 0;
}

