#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "init.h"

/* util.c -- utility functions for gzip support
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

uint32_t crc_32_tab[256];	/* crc table, defined below */


/* ===========================================================================
 * Run a set of bytes through the crc shift register.  If s is a NULL
 * pointer, then initialize the crc shift register contents instead.
 * Return the current crc in either case.
 * Parameters:
 *   unsigned char *s;			pointer to bytes to pump through 
 *   unsigned n;		number of bytes in s[] 
 */
 
uint32_t updcrc(unsigned char* s, unsigned n)
{
    register uint32_t c;		/* temporary variable */

    static uint32_t crc = (uint32_t) 0xffffffffL;	/* shift register contents */

    if (s == NULL) {
	c = 0xffffffffL;
    } else {
	c = crc;
	while (n--) {
	    c = crc_32_tab[((int) c ^ (*s++)) & 0xff] ^ (c >> 8);
	}
    }
    crc = c;
    return c ^ 0xffffffffL;	/* (instead of ~c for 64-bit machines) */
}

/* initial table setup */

void makecrc(void)
{
/* Not copyrighted 1990 Mark Adler      */

    uint32_t c;			/* crc shift register */
    uint32_t e;			/* polynomial exclusive-or pattern */
    int i;			/* counter for all possible eight bit values */
    int k;			/* byte being shifted into crc apparatus */

    /* terms of polynomial defining this crc (except x^32): */
    static int p[] =
    {0, 1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23, 26};

    /* Make exclusive-or pattern from polynomial */
    e = 0;
    for (i = 0; i < sizeof(p) / sizeof(int); i++)

	e |= 1L << (31 - p[i]);

    crc_32_tab[0] = 0;

    for (i = 1; i < 256; i++) {
	c = 0;
	for (k = i | 256; k != 1; k >>= 1) {
	    c = c & 1 ? (c >> 1) ^ e : c >> 1;
	    if (k & 1)
		c ^= e;
	}
	crc_32_tab[i] = c;
    }
}

/* crc.c --- 32 bit crc test
 * this file was created from the gzip1.2.4 util.c file and from lib/inflate.c
 * of linux2.0.14
 * charles@igel.de: 28.Sep.96
 */


