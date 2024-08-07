/*
 * initramfs init program for kernel 5.4.x.
 * functions for getting sysfs or dmi values
 * Copyright (C) by IGEL Technology GmbH 2020
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libsysfs.h>
#include "init.h"

/*
 * binary_files - defines existing sysfs binary files. These files will be
 * printed in hex.
 */
static const char *binary_files[] = {
        "config",
        "data",
	NULL
};

/**
 * isbinaryvalue: checks to see if attribute is binary or not.
 * @attr: attribute to check.
 * returns 1 if binary, 0 if not.
 */
static int isbinaryvalue(struct sysfs_attribute *attr)
{
	char *p = (char *)*binary_files;

        if (!attr || !attr->value)
                return 0;

	while (*p) {
                if ((strcmp(attr->name, p)) == 0)
                        return 1;
		p++;
	}

        return 0;
}


/*
 * get_attribute_value : will return the attribute string without newline.
 */
static char* get_attribute_value(struct sysfs_attribute *attr)
{
	if (!attr)
		return NULL;

	if (attr->method & SYSFS_METHOD_SHOW) {
		if (!isbinaryvalue(attr)) {
			if (attr->value && strlen(attr->value) > 0) {
				remove_end_newline(attr->value);
				return (attr->value);
			}
		}
	}

	return NULL;
}


/*
 * Find a specific vendor ID in sysfs pci bus.
 *
 * This function returns 1 if one found and 0 otherwise.
 */

int find_pci_vendors (struct vendor_list *vendors)
{
	struct sysfs_bus   *bus    = NULL;
	struct dlist       *list   = NULL;
	struct vendor_list *vendor = NULL;

	if (vendors == NULL)
		return (0);

	bus = sysfs_open_bus("pci");
	if (bus == NULL) {
		return (0);
	}

	list = sysfs_get_bus_devices(bus);
	if (list) {
		struct sysfs_device    *dev  = NULL;
		struct sysfs_attribute *attr = NULL;
		dlist_for_each_data(list, dev, struct sysfs_device) {
			attr = sysfs_get_device_attr(dev, "vendor");
			if (attr) {
				vendor = vendors;
				do {
					if (match_string_nocase(attr->value, vendor->name) == 0) {
						sysfs_close_bus(bus);
						return (1);
					}
				} while ((vendor = vendor->next) != NULL);
			}
		}
	}
	sysfs_close_bus(bus);

	return (0);
}

/*
 * get sysfs dmi entries
 *
 * returns value of entry or NULL if not found
 */

char *get_dmi_data(const char *field, char *buffer, size_t len_buf)
{
	char                      *buf = NULL;
	struct sysfs_class        *cls = NULL;
	struct sysfs_class_device *clsdev = NULL;
	struct sysfs_device       *dev = NULL;
	struct sysfs_attribute    *attr = NULL;
	char                      *p = NULL;

	cls = sysfs_open_class("dmi");
	if (cls == NULL)
		return NULL;

	clsdev = sysfs_get_class_device(cls, "id");
	if (clsdev) {
		dev = sysfs_get_classdev_device(clsdev);
		if (dev) {
			attr = sysfs_get_device_attr(dev, field);
			if (attr) {
				p = get_attribute_value(attr);
			}
		} else {
			attr = sysfs_get_classdev_attr(clsdev, field);
			if (attr) {
				p = get_attribute_value(attr);
			}
		}
	}

	if (p != NULL) {
		if (buffer && len_buf > 0) {
			memset(buffer, 0, len_buf);
			strncpy(buffer, p, len_buf);
			buf = buffer;
		} else {
			buf = strdup(p);
		}
	}

	sysfs_close_class(cls);
	return buf;
}

/*
 * get sysfs block entries
 *
 * returns value of entry or NULL if not found
 */

char *get_block_data(const char *blk_dev, const char *field, char *buffer, size_t len_buf)
{
	char                      *buf = NULL;
	struct sysfs_class        *cls = NULL;
	struct sysfs_class_device *clsdev = NULL;
	struct sysfs_device       *dev = NULL;
	struct sysfs_attribute    *attr = NULL;
	char                      *p = NULL;
	char                       path[SYSFS_PATH_MAX];

	cls = sysfs_open_class("block");
	if (cls == NULL)
		return NULL;

	clsdev = sysfs_get_class_device(cls, blk_dev);
	if (clsdev != NULL) {
		attr = sysfs_get_classdev_attr(clsdev, field);
		if (attr) {
			p = get_attribute_value(attr);
		} else {
			dev = sysfs_get_classdev_device(clsdev);
			while (dev && p == NULL) {
				attr = sysfs_get_device_attr(dev, field);
				if (attr) {
					p = get_attribute_value(attr);
				} else {
					dev = sysfs_get_device_parent(dev);
				}
			}
		}
		attr = NULL;
		if (!p) {
			memset(path, 0, SYSFS_PATH_MAX);
			strncpy(path, clsdev->path, SYSFS_PATH_MAX);
			strncat(path, "/queue/", SYSFS_PATH_MAX - 1);
			strncat(path, field, SYSFS_PATH_MAX - 1);
			attr = sysfs_open_attribute(path);
			if (attr) {
				if (!sysfs_read_attribute(attr)) {
					p = get_attribute_value(attr);
				}
			}
		}
	}

	if (p != NULL) {
		if (buffer && len_buf > 0) {
			memset(buffer, 0, len_buf);
			strncpy(buffer, p, len_buf);
			buf = buffer;
		} else {
			buf = strdup(p);
		}
	}

	if (attr)
		sysfs_close_attribute(attr);

	sysfs_close_class(cls);
	return buf;
}

/*
 * get sysfs block partition entries
 *
 * returns value of entry or NULL if not found
 */

char *get_block_partition_data(const char *blk_dev, int part_num, const char *field, char *buffer, size_t len_buf)
{
	char                      *buf = NULL;
	struct sysfs_class        *cls = NULL;
	struct sysfs_class_device *clsdev = NULL;
	struct sysfs_attribute    *attr = NULL;
	char                      *p = NULL;
	char                       path[SYSFS_PATH_MAX];

	cls = sysfs_open_class("block");
	if (cls == NULL)
		return NULL;

	clsdev = sysfs_get_class_device(cls, blk_dev);
	if (clsdev != NULL) {
		memset(path, 0, SYSFS_PATH_MAX);
		strncpy(path, clsdev->path, SYSFS_PATH_MAX);
		snprintf(path + (uintptr_t)strlen(path), SYSFS_PATH_MAX - strlen(path) - 1, "/%s%d/%s", blk_dev, part_num, field);
		attr = sysfs_open_attribute(path);
		if (!attr) {
			snprintf(path + (uintptr_t)strlen(path), SYSFS_PATH_MAX - strlen(path) - 1, "/%sp%d/%s", blk_dev, part_num, field);
			attr = sysfs_open_attribute(path);
		}
		if (attr) {
			if (!sysfs_read_attribute(attr)) {
				p = get_attribute_value(attr);
			}
		}
	}

	if (p != NULL) {
		if (buffer && len_buf > 0) {
			memset(buffer, 0, len_buf);
			strncpy(buffer, p, len_buf);
			buf = buffer;
		} else {
			buf = strdup(p);
		}
	}

	if (attr)
		sysfs_close_attribute(attr);

	sysfs_close_class(cls);
	return buf;
}

/*
 * function to read a sysfs entry
 */

char *get_sysfs_entry(char *buffer, size_t len_buf, const char *format, ...)
{
	char                   path[SYSFS_PATH_MAX];
	char                   *ret = NULL, *p = NULL;
	va_list                list;
	struct sysfs_attribute *attr = NULL;

	va_start(list, format);
	if (vsnprintf(path, SYSFS_PATH_MAX, format, list) < 0) {
		va_end(list);
		return NULL;
	}
	va_end(list);

	attr = sysfs_open_attribute(path);
	if (attr) {
		if (!sysfs_read_attribute(attr)) {
			p = get_attribute_value(attr);
		}
	}

	if (p != NULL) {
		if (buffer && len_buf > 0) {
			memset(buffer, 0, len_buf);
			strncpy(buffer, p, len_buf);
			ret = buffer;
		} else {
			ret = strdup(p);
		}
	}

	if (attr)
		sysfs_close_attribute(attr);

	return ret;
}

/*
 * wrapper function for get_block_data function to use printf style parameter
 */

char *get_block_data_printf(const char *field, char *buffer, size_t len_buf, const char *format, ...)
{
	char *blk_dev = NULL;
	char *ret = NULL;
	va_list list;

	va_start(list, format);
	if (vasprintf(&blk_dev, format, list) < 0) {
                va_end(list);
                return NULL;
        }

	ret = get_block_data(blk_dev, field, buffer, len_buf);

	free(blk_dev);
	return ret;
}

/*
 * wrapper function for get_block_partition_data function to use printf style parameter
 */

char *get_block_partition_data_printf(int part_num, const char *field, char *buffer, size_t len_buf, const char *format, ...)
{
	char *blk_dev = NULL;
	char *ret = NULL;
	va_list list;

	va_start(list, format);
	if (vasprintf(&blk_dev, format, list) < 0) {
                va_end(list);
                return NULL;
        }

	ret = get_block_partition_data(blk_dev, part_num, field, buffer, len_buf);

	free(blk_dev);
	return ret;
}
