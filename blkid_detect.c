#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <blkid/blkid.h>
#include "init.h"

/*
 * returns 0 if a luks header was found in given device / file
 */

int detect_luks_header(const char* format, ...)
{
	va_list list;
	char *devicename = NULL;
	blkid_probe pr;
	char luks[] = "crypto_LUKS";
	char *luks_filter[] = {
		luks,
		NULL
	};
	int r = -1;

	va_start(list, format);

        if (vasprintf(&devicename, format, list) < 0) {
                va_end(list);
                return -1;
        }

        va_end(list);

	pr = blkid_new_probe_from_filename(devicename);
	free(devicename);
	if (pr) {
		r = blkid_probe_filter_superblocks_type(pr, BLKID_FLTR_NOTIN, luks_filter);
	}

	blkid_free_probe(pr);

	if (r == 0) 
		return 1;

	return 0;
}

const char *fstype_of(const char* format, ...)
{
	va_list list;
	char *devicename = NULL;
	const char *value, *type;
	blkid_dev dev;
	blkid_tag_iterate iter = NULL;
	static blkid_cache cache = NULL;

	va_start(list, format);

        if (vasprintf(&devicename, format, list) < 0) {
                va_end(list);
                return NULL;
        }

        va_end(list);

	if (cache == NULL)
		blkid_get_cache(&cache, "/dev/null");

	dev = blkid_get_dev(cache, devicename, BLKID_DEV_NORMAL);
	free(devicename);
	if (dev) {
		iter = blkid_tag_iterate_begin(dev);
  		while (blkid_tag_next(iter, &type, &value) == 0) {
    			if (!strcasecmp(type, "TYPE")) {
				blkid_tag_iterate_end(iter);
      				return value;
			}
		}
		blkid_tag_iterate_end(iter);
		iter = blkid_tag_iterate_begin(dev);
  		while (blkid_tag_next(iter, &type, &value) == 0) {
    			if (!strcasecmp(type, "SEC_TYPE")) {
				blkid_tag_iterate_end(iter);
      				return value;
			}
		}
		blkid_tag_iterate_end(iter);
	}

	return NULL;
}

