#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>

#include <sys/param.h>
#include <sys/wait.h>
#include "tty.h"

#define DEFAULT_TERM   "linux"
#define MY_PATH_MAX    100

static void sigquit_handler (int signum) {
	struct sigaction act;
	sigset_t set;

	/* set default signal action */
  	act.sa_handler = SIG_DFL;
	act.sa_flags = 0;
	if (sigemptyset (&act.sa_mask)	||
	    sigaction (signum, &act, NULL))
		exit (1);

	/* unmask signum */
	if (sigemptyset (&set)		||
	    sigaddset (&set, signum)	||
	    sigprocmask (SIG_UNBLOCK, &set, NULL))
		exit (1);

	kill (getpid(), signum);
	abort();
}

/* open_tty - set up tty as standard { input, output, error } */
void open_tty (char *tty)
{
	struct sigaction sa;
	sigset_t set;
	/* Was `char buf[20];' but there is more than /dev/tty<number> */
	char buf[MY_PATH_MAX+1];
	int fd;
	gid_t gid = 0;

	setenv("TERM", DEFAULT_TERM, 1);
	
	/* Set up new standard input. */
	if (snprintf(buf, sizeof(buf), "/dev/%s", tty) < 0) {
		fprintf(stderr,"%s: %s\n", buf, strerror(EOVERFLOW));
	}
	if (chown (buf, 0, gid) || chmod (buf, (gid ? 0660 : 0600))) {
		if (errno == EROFS)
			fprintf(stderr,"%s: %s\n", buf, strerror(errno));
		else
			fprintf(stderr,"%s: %s\n", buf, strerror(errno));
	}

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset (&sa.sa_mask);
	sigaction (SIGHUP, &sa, NULL);
	sa.sa_handler = sigquit_handler;
	sigaction (SIGQUIT, &sa, NULL);

	/* vhangup() will replace all open file descriptors that point to our
	   controlling tty by a dummy that will deny further reading/writing
	   to our device. It will also reset the tty to sane defaults, so we
	   don't have to modify the tty device for sane settings.
	   We also get a SIGHUP/SIGCONT.
	 */
	if ((fd = open (buf, O_RDWR, 0)) < 0)
		fprintf(stderr,"%s: cannot open tty: %s\n", buf, strerror(errno));
	
	/* Get rid of the present stdout/stderr. */
	close (2);
	close (1);
	close (0);
	if (fd > 2)
		close (fd);

	if ((fd = open (buf, O_RDWR, 0)) < 0)
		fprintf(stderr,"%s: cannot open tty: %s\n", buf, strerror(errno));
	if (ioctl (fd, TIOCSCTTY, (void *)1) == -1)
		fprintf(stderr,"%s: cannot get controlling tty: %s\n", buf, strerror(errno));
	
	/* Set up standard output and standard error file descriptors. */
	if (dup2 (fd, 0) != 0)
		fprintf(stderr,"%s: dup problem: %s\n", buf, strerror(errno));
	if (dup2 (fd, 1) != 1)
		fprintf(stderr,"%s: dup problem: %s\n", buf, strerror(errno));
	if (dup2 (fd, 2) != 2)
		fprintf(stderr,"%s: dup problem: %s\n", buf, strerror(errno));
	if (fd > 2)
		close (fd);

	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	sigemptyset (&sa.sa_mask);
	sigaction (SIGHUP, &sa, NULL);

	/* Unmask SIGHUP if inherited */
	sigemptyset (&set);
	sigaddset (&set, SIGHUP);
	sigprocmask (SIG_UNBLOCK, &set, NULL);
	
	tcflush (0, TCIOFLUSH);
}
