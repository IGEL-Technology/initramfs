/*
 * initramfs init program for kernel 3.13.x.

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

#define _GNU_SOURCE

#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "init.h"

struct dep_t {	/* one-way list of dependency rules */
	/* a dependency rule */
	char *  m_name;				/* the module name*/
	char *  m_path;				/* the module file path */
	struct mod_opt_t *  m_options;	/* the module options */

	int     m_isalias  : 1;			/* the module is an alias */
	int     m_reserved : 15;		/* stuffin' */

	int     m_depcnt   : 16;		/* the number of dependable module(s) */
	char ** m_deparr;			/* the list of dependable module(s) */

	struct dep_t * m_next;			/* the next dependency rule */
};

struct mod_list_t {	/* two-way list of modules to process */
	/* a module description */
	char *  m_name;
	char *  m_path;
	struct mod_opt_t *  m_options;

	struct mod_list_t * m_prev;
	struct mod_list_t * m_next;
};


static struct dep_t *depend = NULL;

#define main_options "acdklnqrst:vVC:"
#define INSERT_ALL     1        /* a */
#define DUMP_CONF_EXIT 2        /* c */
#define D_OPT_IGNORED  4        /* d */
#define AUTOCLEAN_FLG  8        /* k */
#define LIST_ALL       16       /* l */
#define SHOW_ONLY      32       /* n */
#define QUIET          64       /* q */
#define REMOVE_OPT     128      /* r */
#define DO_SYSLOG      256      /* s */
#define RESTRICT_DIR   512      /* t */
#define VERBOSE        1024     /* v */
#define VERSION_ONLY   2048     /* V */
#define CONFIG_FILE    4096     /* C */

#define autoclean       (main_opts & AUTOCLEAN_FLG)
#define show_only       (main_opts & SHOW_ONLY)
#define quiet           (main_opts & QUIET)
#define remove_opt      (main_opts & REMOVE_OPT)
#define do_syslog       (main_opts & DO_SYSLOG)
#define verbose         (main_opts & VERBOSE)

static int main_opts = VERBOSE;

static int parse_tag_value ( char *buffer, char **ptag, char **pvalue )
{
	char *tag, *value;

	while ( isspace ( *buffer ))
		buffer++;
	tag = value = buffer;
	while ( !isspace ( *value ))
		if (!*value) return 0;
		else value++;
	*value++ = 0;
	while ( isspace ( *value ))
		value++;
	if (!*value) return 0;

	*ptag = tag;
	*pvalue = value;

	return 1;
}

/* Jump through hoops to simulate how fgets() grabs just one line at a
 * time... Don't use any stdio since modprobe gets called from a kernel
 * thread and stdio junk can overflow the limited stack...
 */
static char *reads ( int fd, char *buffer, size_t len )
{
	int n = read ( fd, buffer, len );

	if ( n > 0 ) {
		char *p;

		buffer [len-1] = 0;
		p = strchr ( buffer, '\n' );

		if ( p ) {
			off_t offset;

			offset = lseek ( fd, 0L, SEEK_CUR );               // Get the current file descriptor offset
			lseek ( fd, offset-n + (p-buffer) + 1, SEEK_SET ); // Set the file descriptor offset to right after the \n

			p[1] = 0;
		}
		return buffer;
	}

	else
		return 0;
}

/*
 * This function appends an option to a list
 */
static struct mod_opt_t *append_option( struct mod_opt_t *opt_list, char *opt )
{
	struct mod_opt_t *ol = opt_list;

	if( ol ) {
		while( ol-> m_next ) {
			ol = ol-> m_next;
		}
		ol-> m_next = malloc( sizeof( struct mod_opt_t ) );
		ol = ol-> m_next;
	} else {
		ol = opt_list = malloc( sizeof( struct mod_opt_t ) );
	}

	ol-> m_opt_val = strdup( opt );
	ol-> m_next = NULL;

	return opt_list;
}

#define parse_command_string(src, dst)	(0)

/*
 * This function builds a list of dependency rules from /lib/modules/`uname -r\modules.dep.
 * It then fills every modules and aliases with their  default options, found by parsing
 * modprobe.conf (or modules.conf, or conf.modules).
 */
static struct dep_t *build_dep ( void )
{
	int fd;
	struct utsname un;
	struct dep_t *first = 0;
	struct dep_t *current = 0;
	char buffer[2048];
	char *filename;
	char dirname[255];
	int continuation_line = 0;

	if ( uname ( &un )) {
		msg(NULL,LOG_EMERG,"modprobe: can not get kernel version\n");
		return NULL;
	}

	snprintf(dirname, 255, "/lib/modules/%s", un.release);
	filename = malloc(255);
	if (filename) {
		snprintf(filename,255,"%.241s/modules.dep", dirname);
		
		if (( fd = open ( filename, O_RDONLY )) < 0 ) {

			/* Ok, that didn't work.  Fall back to looking in /lib/modules */
			if (( fd = open ( "/lib/modules/modules.dep", O_RDONLY )) < 0 ) {
				return NULL;
			}
		}
		free(filename);
	} else {
		/* Ok, that didn't work.  Fall back to looking in /lib/modules */
		if (( fd = open ( "/lib/modules/modules.dep", O_RDONLY )) < 0 ) {
			return NULL;
		}
	}
	while ( reads ( fd, buffer, sizeof( buffer ))) {
		int l = strlen ( buffer );
		char *p = 0;

		while ( l > 0 && isspace ( buffer [l-1] )) {
			buffer [l-1] = 0;
			l--;
		}

		if ( l == 0 ) {
			continuation_line = 0;
			continue;
		}

		/* Is this a new module dep description? */
		if ( !continuation_line ) {
			/* find the dep beginning */
			char *col = strchr ( buffer, ':' );
			char *dot = col;

			if ( col ) {
				/* This line is a dep description */
				char *mods;
				char *modpath;
				char *mod;

				/* Find the beginning of the module file name */
				*col = 0;
				mods = strrchr ( buffer, '/' );

				if ( !mods )
					mods = buffer; /* no path for this module */
				else
					mods++; /* there was a path for this module... */

				/* find the path of the module */
				if ('/' == buffer[0]) {	/* old style deps - absolute path specified */
					modpath = strdup(buffer);
				}
				else { /* make an absolute path */
					if (asprintf(&modpath, "%s/%s", dirname, buffer) < 0) {
						msg(NULL,LOG_ERR,"modprobe: "
						  "Could not allocate memory for module path.\n");
						continue;
					}
				}
				/* find the end of the module name in the file name
				   ending with extension ".ko" */
				if ( ( *(col-3) == '.' ) &&
				     ( *(col-2) == 'k' ) && ( *(col-1) == 'o' ) )
					dot = col - 3;

				mod = strndup ( mods, dot - mods );

				/* enqueue new module */
				if ( !current ) {
					first = current = (struct dep_t *) malloc ( sizeof ( struct dep_t ));
				}
				else {
					current-> m_next = (struct dep_t *) malloc ( sizeof ( struct dep_t ));
					current = current-> m_next;
				}
				current-> m_name  = mod;
				current-> m_path  = modpath; /* modpath was already allocated */
				current-> m_options = NULL;
				current-> m_isalias = 0;
				current-> m_depcnt  = 0;
				current-> m_deparr  = 0;
				current-> m_next    = 0;

				p = col + 1;
			}
			else
				/* this line is not a dep description */
				p = 0;
		}
		else
			/* It's a dep description continuation */
			p = buffer;

		while ( p && *p && isblank(*p))
			p++;

		/* p points to the first dependable module; if NULL, no dependable module */
		if ( p && *p ) {
			char *end = &buffer [l-1];
			char *deps;
			char *dep;
			char *next;
			int ext = 0;

			while ( isblank ( *end ) || ( *end == '\\' ))
				end--;

			do
			{
				/* search the end of the dependency */
				next = strchr (p, ' ' );
				if (next)
				{
					*next = 0;
					next--;
				}
				else
					next = end;

				/* find the beginning of the module file name */
				deps = strrchr ( p, '/' );

				if ( !deps || ( deps < p )) {
					deps = p;

					while ( isblank ( *deps ))
						deps++;
				}
				else
					deps++;

				/* find the end of the module name in the file name,
				   ending with extension ".ko" */
				if ( ( *(next-2) == '.' ) &&
				     ( *(next-1) == 'k' )  && ( *next == 'o' ) )
					ext = 3;

				/* Cope with blank lines */
				if ((next-deps-ext+1) <= 0)
					continue;
				dep = strndup ( deps, next - deps - ext + 1 );

				/* Add the new dependable module name */
				current-> m_depcnt++;
				current-> m_deparr = (char **) realloc ( current-> m_deparr,
						sizeof ( char *) * current-> m_depcnt );
				current-> m_deparr [current-> m_depcnt - 1] = dep;

				p = next + 2;
			} while (next < end);
		}

		/* is there other dependable module(s) ? */
		if ( buffer [l-1] == '\\' )
			continuation_line = 1;
		else
			continuation_line = 0;
	}
	close ( fd );


	continuation_line = 0;
	while ( reads ( fd, buffer, sizeof( buffer ))) {
		int l;
		char *p;

		p = strchr ( buffer, '#' );
		if ( p )
			*p = 0;

		l = strlen ( buffer );

		while ( l && isspace ( buffer [l-1] )) {
			buffer [l-1] = 0;
			l--;
		}

		if ( l == 0 ) {
			continuation_line = 0;
			continue;
		}

		if ( !continuation_line ) {
			if (( strncmp ( buffer, "alias", 5 ) == 0 ) && isspace ( buffer [5] )) {
				char *alias, *mod;

				if ( parse_tag_value ( buffer + 6, &alias, &mod )) {
					/* handle alias as a module dependent on the aliased module */
					if ( !current ) {
						first = current = (struct dep_t *) calloc ( 1, sizeof ( struct dep_t ));
					}
					else {
						current-> m_next = (struct dep_t *) calloc ( 1, sizeof ( struct dep_t ));
						current = current-> m_next;
					}
					current-> m_name  = strdup ( alias );
					current-> m_isalias = 1;

					if (( strcmp ( mod, "off" ) == 0 ) || ( strcmp ( mod, "null" ) == 0 )) {
						current-> m_depcnt = 0;
						current-> m_deparr = 0;
					}
					else {
						current-> m_depcnt  = 1;
						current-> m_deparr  = malloc ( 1 * sizeof( char * ));
						current-> m_deparr[0] = strdup ( mod );
					}
					current-> m_next    = 0;
				}
			}
			else if (( strncmp ( buffer, "options", 7 ) == 0 ) && isspace ( buffer [7] )) {
				char *mod, *opt;

				/* split the line in the module/alias name, and options */
				if ( parse_tag_value ( buffer + 8, &mod, &opt )) {
					struct dep_t *dt;

					/* find the corresponding module */
					for ( dt = first; dt; dt = dt-> m_next ) {
						if ( strcmp ( dt-> m_name, mod ) == 0 )
							break;
					}
					if ( dt ) {
						dt-> m_options = append_option( dt-> m_options, opt );
					}
				}
			}
		}
	}
	close ( fd );

	return first;
}

static int mod_process ( struct mod_list_t *list, int do_insert )
{
	int rc = 0;

	while ( list ) {
		if ( do_insert ) {
			if (kmodule_already_loaded (NULL, list->m_name) != 1) {
				rc = insmod_cmd(list->m_path, list->m_options);
			}
		} else {
			/* modutils uses short name for removal */
			if (kmodule_already_loaded (NULL, list->m_name) != 0) {
				rc = rmmod_cmd(list->m_name);
			}
		}
		list = do_insert ? list-> m_prev : list-> m_next;
	}
	return rc;
}

/*
 * Builds the dependency list (aka stack) of a module.
 * head: the highest module in the stack (last to insmod, first to rmmod)
 * tail: the lowest module in the stack (first to insmod, last to rmmod)
 */
static void check_dep (const char *mod, struct mod_list_t **head, struct mod_list_t **tail )
{
	struct mod_list_t *find;
	struct dep_t *dt;
	struct mod_opt_t *opt = 0;
	char *path = 0;

	// check dependencies
	for ( dt = depend; dt; dt = dt-> m_next ) {
		if ( strcmp ( dt-> m_name, mod ) == 0) {
			break;
		}
	}

	if( !dt ) {
		msg(NULL,LOG_ERR,"modprobe: module %s not found.\n", mod);
		return;
	}

	// resolve alias names
	while ( dt-> m_isalias ) {
		if ( dt-> m_depcnt == 1 ) {
			struct dep_t *adt;

			for ( adt = depend; adt; adt = adt-> m_next ) {
				if ( strcmp ( adt-> m_name, dt-> m_deparr [0] ) == 0 )
					break;
			}
			if ( adt ) {
				/* This is the module we are aliased to */
				struct mod_opt_t *opts = dt-> m_options;
				/* Option of the alias are appended to the options of the module */
				while( opts ) {
					adt-> m_options = append_option( adt-> m_options, opts-> m_opt_val );
					opts = opts-> m_next;
				}
				dt = adt;
			}
			else {
				msg(NULL,LOG_ERR,"modprobe: module %s not found.\n", mod);
				return;
			}
		}
		else {
			msg(NULL,LOG_ERR,"modprobe: bad alias %s\n", dt-> m_name);
			return;
		}
	}

	mod = dt-> m_name;
	path = dt-> m_path;
	opt = dt-> m_options;

	// search for duplicates
	for ( find = *head; find; find = find-> m_next ) {
		if ( !strcmp ( mod, find-> m_name )) {
			// found -> dequeue it

			if ( find-> m_prev )
				find-> m_prev-> m_next = find-> m_next;
			else
				*head = find-> m_next;

			if ( find-> m_next )
				find-> m_next-> m_prev = find-> m_prev;
			else
				*tail = find-> m_prev;

			break; // there can be only one duplicate
		}
	}

	if ( !find ) { // did not find a duplicate
		find = (struct mod_list_t *) malloc ( sizeof(struct mod_list_t));
		find-> m_name = (char *) mod;
		find-> m_path = path;
		find-> m_options = opt;
	}

	// enqueue at tail
	if ( *tail )
		(*tail)-> m_next = find;
	find-> m_prev   = *tail;
	find-> m_next   = 0;

	if ( !*head )
		*head = find;
	*tail = find;

	if ( dt ) {
		int i;

		/* Add all dependable module for that new module */
		for ( i = 0; i < dt-> m_depcnt; i++ )
			check_dep ( dt-> m_deparr [i], head, tail );
	}
}

static int mod_insert (const char *mod )
{
	struct mod_list_t *tail = 0;
	struct mod_list_t *head = 0;
	int rc;

	// get dep list for module mod
	check_dep ( mod, &head, &tail );

	if ( head && tail ) {
		// process tail ---> head
		rc = mod_process ( tail, 1 );
	}
	else
		rc = 1;

	return rc;
}

static int mod_remove (const char *mod )
{
	int rc;
	static struct mod_list_t rm_a_dummy = { (char *) "-a", NULL, NULL, NULL, NULL };

	struct mod_list_t *head = 0;
	struct mod_list_t *tail = 0;

	if ( mod )
		check_dep ( mod, &head, &tail );
	else  // autoclean
		head = tail = &rm_a_dummy;

	if ( head && tail )
		rc = mod_process ( head, 0 );  // process head ---> tail
	else
		rc = 1;
	return rc;

}

int modprobe_cmd(const char *name)
{
	int rc = EXIT_SUCCESS;

	if ( !depend )
		depend = build_dep ( );

	if ( !depend ) {
		msg(NULL,LOG_INFO, "modprobe: could not parse modules.dep\n" );
		return (1);
	}
	if (remove_opt) {
		if (mod_remove (name)) {
			msg(NULL,LOG_ERR,"modprobe: failed to remove module %s\n", name );
			rc = EXIT_FAILURE;
		}
	} else {
		if (mod_insert (name)) {
			msg(NULL,LOG_ERR,"modprobe: failed to load module %s\n", name );
			return 1;
		}
	}

	/* Here would be a good place to free up memory allocated during the dependencies build. */

	return rc;
}

int modprobe(int argc, char **argv)
{
	int i;
	int rc = EXIT_SUCCESS;
	
	for (i=1; i < argc; i++) {
		if (!strcmp(argv[i],"-r")) {
			main_opts = main_opts | REMOVE_OPT;
			continue;
		}
		if (!strncmp(argv[i],"-",1)) {
			continue;
		}
		
		rc = modprobe_cmd(argv[i]);
	}
	
	return rc;
}
