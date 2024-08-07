#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include "tty.h"

#define LOGINPROGRAM "/bin/sh"
#define LOGINOPT "-login"
#define LOGIN_TTY "console"

/* on which tty line are we sitting? (e.g. tty1) */
static char *tty = (char *)LOGIN_TTY;

/* process ID of the rescue_shell */
static pid_t pid;
/* session ID of the rescue_shell */
static pid_t sid;

/* login program */
static char* loginprog;
static char* loginopt;

/* do_prompt */
static void do_prompt (void)
{

	fprintf (stderr,"\n--- rescue shell %s ---\n", tty);
	fflush (stdout);
}

/*
 * main program
 */
int main (int argc, char **argv)
{
	char *logarr[3];
	
	loginprog = (char *) LOGINPROGRAM;
	loginopt = (char *) LOGINOPT;
	
	/* if you want to start the shell on another tty than /dev/console */
	if (argc>1) {
		if ((strncmp (argv[1], "/dev/tty", 8) == 0) &&
		    (strlen(argv[1])<12))
			tty=strdup(argv[1]);
		if ((strncmp (argv[1], "tty", 3) == 0) &&
		    (strlen(argv[1])<7))
		    	tty=strdup(argv[1]);
	}
	/* if you want another login program and login option */
	if (argc>2) {
		if (strlen(argv[2])<20)
			loginprog=strdup(argv[2]);
	}
	if (argc>3) {
		if (strlen(argv[3])<20)
			loginopt=strdup(argv[3]);
	}
	
	logarr[0]=loginprog;
	logarr[1]=loginopt;
	logarr[2]=0;
	
	/* Skip the "/dev/", we may add it later */
	if (strncmp (tty, "/dev/", 5) == 0)
		tty += 5;
		
	/* clear controling tty and session leader */
	setsid ();
		
	pid = getpid ();
	sid = getsid (0);

	open_tty (tty);
	
	do_prompt ();
	
	execv (loginprog,logarr);
	  	
	exit (0);
}
