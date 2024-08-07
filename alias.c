/*
 * initramfs init program for kernel 3.19.x.
 * load drivers for pci devices.
 * Copyright (C) by IGEL Technology GmbH 2015
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

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include "init.h"

#define ALIASFILE	"modules.alias"
#define CHECK_LEN	1024
#define SYS_PATH	"/sys/devices"
#define LINELENGTH	8000

/* find_modalias search /sys/devices (SYS_PATH) for all present modalias files
 * and store the contents of all found modalias files in the mod_list linked 
 * list */

static int find_modalias (struct mod_list **list, const char *path)
{
	struct dirent *dirent;
	struct mod_list *entry = NULL;
	DIR *dir = opendir(path);
	FILE *f = NULL;
	char line[512], name[PATH_MAX];
	struct stat st;
	int i;

	while ((dirent = readdir(dir))) 
	{
		if (dirent->d_name[0] == '.')
			continue;

		snprintf(name, sizeof (name), "%s/%s", path, dirent->d_name);
		if (lstat(name, &st))
			continue;

		if (S_ISLNK(st.st_mode))
			continue;

		if (S_ISDIR(st.st_mode)) {
			find_modalias(list, name);
		} else if (strcmp(dirent->d_name, "modalias") == 0) {
			f = fopen(name, "r");
			if (!f)
				continue;

			while (fgets(line, sizeof(line), f) != NULL) {
				for (i = 0; i < sizeof(line) && (line[i] != '\n'); i++);
				line[i] = '\0';
				entry = (struct mod_list *) malloc(sizeof(struct mod_list));
				if (entry == NULL) {
					return (1);
				}
				entry->alias = NULL;
				entry->alias = strdup(line);
				entry->next = *list;
				*list = entry;
			}
			fclose(f);
		}
	}
	closedir(dir);

	return (0);
}

/* search module in given dir by name and ignore '_' != '-' issues */

void find_kernel_module_by_name (struct kmod_struct *list, const char *path)
{
	struct dirent *dirent;
	DIR *dir = opendir(path);
	char name[PATH_MAX];
	struct stat st;
	int i;

	while ((dirent = readdir(dir))) 
	{
		if (dirent->d_name[0] == '.')
			continue;

		snprintf(name, sizeof (name), "%s/%s", path, dirent->d_name);
		if (lstat(name, &st))
			continue;

		if (S_ISLNK(st.st_mode))
			continue;

		if (S_ISDIR(st.st_mode)) {
			find_kernel_module_by_name(list, name);
		} else {
			if (strncmp(dirent->d_name, list->name, strlen(list->name)) == 0) {
				list->realname = strdup(list->name);
				list->abs_name = strdup(name);
				closedir(dir);
				return;
			} else {
				for (i=0; list->name[i] != '\0' && dirent->d_name[i] != '\0'; i++) {
					if (list->name[i] == '_' || list->name[i] == '-') {
						if (dirent->d_name[i] != '_' && dirent->d_name[i] != '-')
							break;
					} else {
						if (dirent->d_name[i] != list->name[i])
							break;
					}
				}
				if (list->name[i] == '\0') {
					list->realname = strndup(dirent->d_name, i);
					list->abs_name = strdup(name);
					closedir(dir);
					return;
				}
			}
		}
	}
	closedir(dir);
}

/* function to free linked list mod_list */

static void free_mod_list (struct mod_list **list)
{
	struct mod_list *p = NULL, *d = NULL;

	p = *list;

	while (p != NULL) {
		if (p->alias != NULL)
			free(p->alias);
		d = p;
		p = p->next;
		free(d);
	}
}

/* load all modules which modalias is present in the /sys/device path
 * if device is given you can limit the module load to pci or usb */

int
load_alias_modules(init_t *init, const char* device)
{
	char filename[MAXPATHLEN], *module, line[LINELENGTH];
	struct mod_list *list = NULL, *p = NULL;
	struct kmod_struct kmod;
	FILE *alias_file;
	int i;

	sprintf(filename, "%s/%s", init->moddir, ALIASFILE);
	if ((alias_file = fopen(filename, "r")) == NULL) {
		msg(init,LOG_ERR,"load_alias_modules: can not open %s\n",filename);
		return(1);
	}

	if (find_modalias (&list, SYS_PATH) != 0) {
		msg(init,LOG_ERR,"failed to allocate memory\n");
		free_mod_list(&list);
		return(1);
	}

	while(fgets(line, LINELENGTH, alias_file) != NULL) {
		
		if (strncmp(line, "alias ", 6) != 0)
			continue;

		/* if device is pci only load pci modules,
		 * if device is usb only load usb modules,
		 * if device is acpi only load acpi modules */

		if (strcmp(device, "usb") == 0) {
			if (strncmp(&(line[6]), "usb:", 4) != 0)
				continue;
		} else if (strcmp(device, "pci") == 0) {
			if (strncmp(&(line[6]), "pci:", 4) != 0)
				continue;
		} else if (strcmp(device, "acpi") == 0) {
			if (strncmp(&(line[6]), "acpi:", 5) != 0 && strncmp(&(line[6]), "acpi*:", 6) != 0)
				continue;
		}

		p = list;

		for (i=6; line[i] != '\0' && line[i] != ' ' && line[i] != '\n'; i++);
		line[i] = '\0';

		module = &(line[i+1]);
		for (i=0; module[i] != '\0' && (module[i] != '\n'); i++);
		module[i] = '\0';

		while (p != NULL) {
			if (fnmatch(&(line[6]), p->alias, FNM_NOESCAPE) == 0) {
				if (kmodule_already_loaded(NULL, module) != 1 ) {
					kmod.name = module;
					kmod.realname = NULL;
					find_kernel_module_by_name(&kmod, init->moddir);
					if (kmod.realname != NULL) {
						load_kernel_module(init, kmod.realname);
						free(kmod.abs_name);
						free(kmod.realname);
					}
				}
				break;
			}
			p = p->next;
		}
	}

	free_mod_list(&list);
	fclose(alias_file);

	return (0);
}


