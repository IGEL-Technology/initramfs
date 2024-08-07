/*
 * initramfs init program for kernel 6.6.x
 * Copyright (C) by IGEL Technology GmbH 2024

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
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <asm/unistd.h>
#include "init.h"

#define IGEL_KEYRING_ERROR       0x420
#define KEY_SPEC_SESSION_KEYRING -3
#define KEY_SPEC_USER_KEYRING    -4
#define KEYCTL_SETPERM           5
#define KEYCTL_LINK              8
#define KEYCTL_UNLINK            9
#define KEYCTL_RES_KEYRING       29
/* set Keyring permissions: KEY_POS_ALL | KEY_USR_SEARCH | KEY_USR_LINK | KEY_USR_READ */
#define IGEL_KEYRING_PERMS 0x3f1b0000
#define __weak __attribute__((weak))

/* key serial number */
typedef int32_t key_serial_t;

/* syscall wrappers */
extern key_serial_t add_key(const char *type,
			    const char *description,
			    const void *payload,
			    size_t plen,
			    key_serial_t ringid);

extern long keyctl(int cmd, ...);

key_serial_t __weak add_key(const char *type,
                            const char *description,
                            const void *payload,
                            size_t plen,
                            key_serial_t ringid)
{
        return syscall(__NR_add_key,
                       type, description, payload, plen, ringid);
}

static inline long __keyctl(int cmd,
                            unsigned long arg2,
                            unsigned long arg3,
                            unsigned long arg4,
                            unsigned long arg5)
{
        return syscall(__NR_keyctl,
                       cmd, arg2, arg3, arg4, arg5);
}

long __weak keyctl(int cmd, ...)
{
         va_list va;
         unsigned long arg2, arg3, arg4, arg5;

         va_start(va, cmd);
         arg2 = va_arg(va, unsigned long);
         arg3 = va_arg(va, unsigned long);
         arg4 = va_arg(va, unsigned long);
         arg5 = va_arg(va, unsigned long);
         va_end(va);

	 return __keyctl(cmd, arg2, arg3, arg4, arg5);
}

/* igel keyring for community apps */
int igel_keyring(void) {
	FILE *fp = fopen("/etc/root.der", "rb");
	size_t len, newLen;
	char *data, *ca_id_string, *kr_id_string;
	char prefix[] = {"key_or_keyring:"};
	int ca_id_len, kr_id_len;
	key_serial_t kr_id, ca_id, kr_cmty;


	/* During initramfs build process CA.der needs to be created! */
	if (fp == NULL)
		return IGEL_KEYRING_ERROR+1;

	/* Get file length */
	fseek(fp, 0L, SEEK_END);
	len = ftell(fp);

	if (len < 1)
		return IGEL_KEYRING_ERROR+2;

	/* Allocate buffer */
	data = malloc(sizeof(char) * (len + 1));

	/* Go back to start of file. */
	if (fseek(fp, 0L, SEEK_SET) != 0) {
		free(data);
		return IGEL_KEYRING_ERROR+3;
	}

	/* Read entire file into memory. */
	newLen = fread(data, sizeof(char), len, fp);
	if ( ferror( fp ) != 0 ) {
		free(data);
		return IGEL_KEYRING_ERROR+4;
	}
	else
		data[newLen++] = '\0'; /* Just to be safe. */

	/* Create a new keyring, igel, and set permissions appropriately. Since new
	 * keys give only the posessor sufficient permissions, we add it to the session
	 * keyring (so we posess it), change the permissions and then move it to the
	 * user keyring. Sounds a bit odd but this is how it's been designed. */

	/* kr_id=`keyctl newring igel @s` */
	kr_id = add_key("keyring", "igel", NULL, 0, KEY_SPEC_SESSION_KEYRING);

	if (kr_id < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+5;
	}

	/* keyctl setperm $kr_id */
	if (keyctl(KEYCTL_SETPERM, kr_id, IGEL_KEYRING_PERMS) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+6;
	}

	/* keyctl link $kr_id @u */
	if (keyctl(KEYCTL_LINK, kr_id, KEY_SPEC_USER_KEYRING) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+7;
	}

	/* keyctl unlink $kr_id @s */
	if (keyctl(KEYCTL_UNLINK, kr_id, KEY_SPEC_SESSION_KEYRING) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+8;
	}

	/* add the CA to our keyring and make it read-only */
        /* ca_id=`openssl x509 -in CA.pem -outform DER | keyctl padd asymmetric igel_ca $kr_id` */
	ca_id = add_key("asymmetric", "igel_ca", data, len, kr_id);

	if (ca_id < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+9;
	}

	/* restrict igel keyring so it only accepts certs that are signed by igel_ca */
	/* keyctl restrict_keyring $kr_id asymmetric key_or_keyring:$ca_id */
	ca_id_len = snprintf(NULL, 0, "%d", ca_id);
	ca_id_string = malloc(sizeof(char) * (ca_id_len + 16));
	strcpy(ca_id_string, prefix);
	sprintf(&ca_id_string[15], "%d", ca_id);

	if (keyctl(KEYCTL_RES_KEYRING, kr_id, "asymmetric", ca_id_string) < 0) {
		free(data);
		free(ca_id_string);
		return IGEL_KEYRING_ERROR+10;
	}

	/* link igel_cmty keyring to user keyring but restrict to igel keyring */

	/* kr_cmty=`keyctl newring igel_cmty @s` */
	kr_cmty = add_key("keyring", "igel_cmty", NULL, 0, KEY_SPEC_SESSION_KEYRING);

	if (kr_cmty < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+11;
	}

	if (keyctl(KEYCTL_SETPERM, kr_cmty, IGEL_KEYRING_PERMS) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+12;
	}

	/* keyctl link $kr_cmty @u */
	if (keyctl(KEYCTL_LINK, kr_cmty, KEY_SPEC_USER_KEYRING) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+13;
	}

	/* keyctl unlink $kr_cmty @s */
	if (keyctl(KEYCTL_UNLINK, kr_cmty, KEY_SPEC_SESSION_KEYRING) < 0) {
		free(data);
		return IGEL_KEYRING_ERROR+14;
	}

	/* preparation for keyctl restrict_keyring ..*/
	kr_id_len = snprintf(NULL, 0, "%d", kr_id);
	kr_id_string = malloc(sizeof(char) * (kr_id_len + 16));
	strcpy(kr_id_string, prefix);
	sprintf(&kr_id_string[15], "%d", kr_id);

	/* keyctl restrict_keyring $kr_cmty asymmetric key_or_keyring:$kr_id */
	if (keyctl(KEYCTL_RES_KEYRING, kr_cmty, "asymmetric", kr_id_string) < 0) {
		free(data);
		free(ca_id_string);
		free(kr_id_string);
		return IGEL_KEYRING_ERROR+15;
	}

	free(data);
	free(ca_id_string);
	free(kr_id_string);
	return 0;
}
