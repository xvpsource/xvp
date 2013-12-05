/*
 * password.c - password handling for Xen VNC Proxy
 *
 * Copyright (C) 2009-2010, Colin Dean
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/*
 * Note: This file is used by xvpdiscover as well as xvp, so cannot have
 *       any dependencies on xvp's code for logging, config, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/des.h>

#include "xvp.h"

xvp_otp     xvp_otp_mode;
xvp_ipcheck xvp_otp_ipcheck;
int         xvp_otp_window;

/*
 * Encrypt or decrypt standard VNC password.
 *
 * These passwords can have any length, but only the first 8 characters
 * are significant, which is convenient for using with DES.  Only ASCII
 * non-space characters should be used in VNC passwords.
 */
static void xvp_password_crypt_vnc(char *src, char *dst, int direction)
{
    static char key[] = { 0xc1, 0x24, 0x08, 0x99, 0xc2, 0x26, 0x07, 0x05 };
    static char tmp[XVP_MAX_VNC_PW];
    DES_key_schedule schedule;

    if (direction == DES_ENCRYPT)
	strncpy(tmp, src, sizeof(tmp)); /* zero pad printable src */
    else
	memcpy(tmp, src, sizeof(tmp));  /* encrypted could contain zeroes */

    DES_set_key_unchecked((DES_cblock *)key, &schedule);
    DES_ecb_encrypt((DES_cblock *)tmp, (DES_cblock *)dst,
		    &schedule, direction);
}

/*
 * Encrypt or decrypt XenServer or Xen Cloud Platform pool manager password.
 *
 * We have no control over how long this password is, but we need some
 * sort of sensible limit, so we work with 16.
 */
static void xvp_password_crypt_xen(char *src, char *dst, int direction)
{
    static char key[] = { 0xcc, 0x10, 0x10, 0x58, 0xbe, 0x03, 0x07, 0x66 };
    char ivec[8];
    static char tmp[XVP_MAX_XEN_PW];
    DES_key_schedule schedule;

    if (direction == DES_ENCRYPT)
	strncpy(tmp, src, sizeof(tmp)); /* zero pad printable src */
    else
	memcpy(tmp, src, sizeof(tmp));  /* encrypted could contain zeroes */
	
    DES_set_key_unchecked((DES_cblock *)key, &schedule);

    memset(ivec, 0, sizeof(ivec));
    DES_ncbc_encrypt((unsigned char *)tmp + 8, (unsigned char *)dst + 8, 8,
			 &schedule, (DES_cblock *)ivec, direction);
    memcpy(ivec, direction == DES_ENCRYPT ? dst + 8 : tmp + 8, 8);
    DES_ncbc_encrypt((unsigned char *)tmp, (unsigned char *)dst, 8,
			 &schedule, (DES_cblock *)ivec, direction);
}

void xvp_password_encrypt(char *src, char *dst, xvp_password_type type)
{
    if (type == XVP_PASSWORD_XEN)
	xvp_password_crypt_xen(src, dst, DES_ENCRYPT);
    else
	xvp_password_crypt_vnc(src, dst, DES_ENCRYPT);
}

void xvp_password_decrypt(char *src, char *dst, xvp_password_type type)
{
    if (type == XVP_PASSWORD_XEN) {
	memset(dst, 0, XVP_MAX_XEN_PW + 1);
	xvp_password_crypt_xen(src, dst, DES_DECRYPT);
    } else {
	memset(dst, 0, XVP_MAX_VNC_PW + 1);
	xvp_password_crypt_vnc(src, dst, DES_DECRYPT);
    }
}

bool xvp_password_hex_to_text(char *hex, char *text, xvp_password_type type)
{
    int i, len;
    unsigned char c;

    switch (type) {
    case XVP_PASSWORD_XEN:
	len = XVP_MAX_XEN_PW;
	break;
    case XVP_PASSWORD_VNC:
	len = XVP_MAX_VNC_PW;
	break;
    default:
	return false;
    }

    for (i = 0; i < len; i++) {
	c = hex[i];
	sprintf(text + 2 * i, "%02x", c);
    }

    text[2 * i] = '\0';
    return true;

}

bool xvp_password_text_to_hex(char *text, char *hex, xvp_password_type type)
{
    int i, len;
    unsigned int c;

    switch (type) {
    case XVP_PASSWORD_XEN:
	len = XVP_MAX_XEN_PW;
	break;
    case XVP_PASSWORD_VNC:
	len = XVP_MAX_VNC_PW;
	break;
    default:
	return false;
    }

    if (strlen(text) != len * 2)
	return false;

    for (i = 0; i < len; i++) {
	if (sscanf(text + i * 2, "%02x", &c) != 1)
	    return false;
	hex[i] = c;
    }

    return true;
}

bool xvp_password_vnc_ok(char *password, unsigned int client_ip, char *challenge, char *response)
{
    int i, j;
    time_t now;
    unsigned char key[8], encrypted[16], nowthere[8];
    DES_key_schedule schedule;

    xvp_password_crypt_vnc(password, key, DES_DECRYPT);

    for (i = 0; i < 8; i++) {
	/*
	 * Reverse bits in byte, algorithm attributed to Sean Anderson,
         * July 13, 2001, referred to at:
	 *
	 *    http://www-graphics.stanford.edu/~seander/bithacks.html
	 *
	 * as being in public domain.
	 */
	key[i] = ((key[i] * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
    }

    DES_set_key_unchecked((DES_cblock *)key, &schedule);

    if (xvp_otp_mode != XVP_OTP_REQUIRE) {
	/*
	 * First try, use permanent password to encrypt challenge, and see
	 * if this matches the response.
	 */
	DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)encrypted,
			&schedule, DES_ENCRYPT);
	DES_ecb_encrypt((DES_cblock *)challenge + 1,
			(DES_cblock *)encrypted + 1,
			&schedule, DES_ENCRYPT);
	if (memcmp(encrypted, response, 16) == 0)
	    return true;
    }

    if (xvp_otp_mode == XVP_OTP_DENY)
	return false;

    now = ((time(NULL) + xvp_otp_window * 0.5) / xvp_otp_window);
    now = (time_t)(now * xvp_otp_window);

    for (i = 0; i != 1; i = (i == 0 ? -1 : (i == -1 ? 2 : 1))) {
	/*
	 * Subsequent tries, use permanent password to encrypt current
	 * time rounded to nearest xvp_otp_window, and then that -/+
	 * xvp_otp_window (to allow for delay and clock discrepancy),
	 * use encrypted time to encrypt challenge, and see if any of
	 * these match the response.
	 */
	unsigned char newkey[8];
	DES_key_schedule newschedule;

	now += xvp_otp_window * i;

	nowthere[0] = ((now & 0xff000000) >> 24);
	nowthere[1] = ((now & 0xff0000) >> 16);
	nowthere[2] = ((now & 0xff00) >> 8);
	nowthere[3] = (now & 0xff);

	switch (xvp_otp_ipcheck) {
	case XVP_IPCHECK_OFF:
	    nowthere[4] = nowthere[0];
	    nowthere[5] = nowthere[1];
	    nowthere[6] = nowthere[2];
	    nowthere[7] = nowthere[3];
	    break;
	case XVP_IPCHECK_ON:
	    // client_ip is in network byte order (big endian)
	    memcpy(nowthere + 4, &client_ip, 4);
	    break;
	case XVP_IPCHECK_HTTP:
	    nowthere[4] = nowthere[0] ^ 'H';
	    nowthere[5] = nowthere[1] ^ 'T';
	    nowthere[6] = nowthere[2] ^ 'T';
	    nowthere[7] = nowthere[3] ^ 'P';
	    break;
	}

	DES_ecb_encrypt((DES_cblock *)nowthere, (DES_cblock *)newkey,
			&schedule, DES_ENCRYPT);
	for (j = 0; j < 8; j++)
	    newkey[j] = ((newkey[j] * 0x80200802ULL) & 0x0884422110ULL)
		* 0x0101010101ULL >> 32;
	DES_set_key_unchecked((DES_cblock *)newkey, &newschedule);
	DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)encrypted,
			&newschedule, DES_ENCRYPT);
	DES_ecb_encrypt((DES_cblock *)challenge + 1,
			(DES_cblock *)encrypted + 1,
			&newschedule, DES_ENCRYPT);

	if (memcmp(encrypted, response, 16) == 0)
	    return true;
    }

    return false;
}
