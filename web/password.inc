<?php
/*
 * password.inc - password handling for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2009, Colin Dean
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

function xvp_password_crypt_vnc($src, &$dst, $direction)
{
    $key = "\xc1\x24\x08\x99\xc2\x26\x07\x05";
    $td = mcrypt_module_open(MCRYPT_DES, "", MCRYPT_MODE_ECB, "");

    $iv = "\x00\x00\x00\x00\x00\x00\x00\x00";
    mcrypt_generic_init($td, $key, $iv);
    $dst = ($direction == MCRYPT_ENCRYPT) ?
	mcrypt_generic($td, $src) : mdecrypt_generic($td, $src);
    mcrypt_generic_deinit($td);

    mcrypt_module_close($td);
}

function xvp_password_crypt_xen($src, &$dst, $direction)
{
    $src1 = substr($src, 0, 8);
    $src2 = substr($src, 8, 8);

    $key = "\xcc\x10\x10\x58\xbe\x03\x07\x66";
    $td = mcrypt_module_open(MCRYPT_DES, "", MCRYPT_MODE_CBC, "");

    $iv = "\x00\x00\x00\x00\x00\x00\x00\x00";
    mcrypt_generic_init($td, $key, $iv);
    $dst2 = ($direction == MCRYPT_ENCRYPT) ?
	mcrypt_generic($td, $src2) : mdecrypt_generic($td, $src2);
    mcrypt_generic_deinit($td);

    $iv = ($direction == MCRYPT_ENCRYPT) ? $dst2 : $src2;
    mcrypt_generic_init($td, $key, $iv);
    $dst1 = ($direction == MCRYPT_ENCRYPT) ?
	mcrypt_generic($td, $src1) : mdecrypt_generic($td, $src1);
    mcrypt_generic_deinit($td);

    mcrypt_module_close($td);

    $dst = $dst1 . $dst2;
}

function xvp_password_encrypt($src, &$dst, $type)
{
    switch ($type) {
    case XVP_PASSWORD_XEN:
	$len = XVP_MAX_XEN_PW;
	break;
    case XVP_PASSWORD_VNC:
	$len = XVP_MAX_VNC_PW;
	break;
    default:
	return;
    }

    if (strlen($src) < $len)
	$src = str_pad($src, $len, "\x00");
    else if (strlen($src) > $len)
	$src = substr($src, 0, $len);

    if ($type == XVP_PASSWORD_XEN)
	xvp_password_crypt_xen($src, $dst, MCRYPT_ENCRYPT);
    else
	xvp_password_crypt_vnc($src, $dst, MCRYPT_ENCRYPT);
}

function xvp_password_decrypt($src, &$dst, $type)
{
    if ($type == XVP_PASSWORD_XEN)
	xvp_password_crypt_xen($src, $dst, MCRYPT_DECRYPT);
    else
	xvp_password_crypt_vnc($src, $dst, MCRYPT_DECRYPT);

    $dst = rtrim($dst, "\x00");
}

function xvp_password_hex_to_text($hex, &$text, $type)
{
    switch ($type) {
    case XVP_PASSWORD_XEN:
	$len = XVP_MAX_XEN_PW;
	break;
    case XVP_PASSWORD_VNC:
	$len = XVP_MAX_VNC_PW;
	break;
    default:
	return false;
    }

    $text = "";
    for ($i = 0; $i < $len; $i++) {
	$text .= sprintf("%02x", ord($hex[$i]));
    }

    return true;

}

function xvp_password_text_to_hex($text, &$hex, $type)
{
    switch ($type) {
    case XVP_PASSWORD_XEN:
	$len = XVP_MAX_XEN_PW;
	break;
    case XVP_PASSWORD_VNC:
	$len = XVP_MAX_VNC_PW;
	break;
    default:
	return false;
    }

    $hex = "";
    for ($i = 0; $i < $len; $i++) {
	if (sscanf(substr($text, $i * 2, 2), "%02x", $c) != 1)
	    return false;
	$hex .= chr($c);
    }

    return true;
}

function xvp_password_otp($hex)
{
    global $xvp_now, $xvp_otp_ipcheck, $xvp_otp_window;

    xvp_password_crypt_vnc($hex, $key, MCRYPT_DECRYPT);

    for ($i = 0; $i < 8; $i++) {
	/*
	 * Reverse bits in byte, algorithm attributed to Sean Anderson,
	 * July 13, 2001 and Mike Keith, January 3, 2002, referred to at:
	 *
	 *    http://www-graphics.stanford.edu/~seander/bithacks.html
	 *
	 * as being in public domain.  Note not using 64-bit version as in
	 * password.c, as can't assume 64-bit integers available in PHP.
	 */
	$b = ord($key[$i]);
	$b = (($b * 0x0802 & 0x22110) | ($b * 0x8020 & 0x88440))
	    * 0x10101 >> 16;
	$key[$i] = chr($b);
    }


    $now = round($xvp_now / $xvp_otp_window);
    $now *= $xvp_otp_window;

    $nowthere = "00000000";
    $nowthere[0] = chr(($now & 0xff000000) >> 24);
    $nowthere[1] = chr(($now & 0xff0000) >> 16);
    $nowthere[2] = chr(($now & 0xff00) >> 8);
    $nowthere[3] = chr($now & 0xff);

    switch ($xvp_otp_ipcheck) {
    case XVP_IPCHECK_OFF:
	$nowthere[4] = $nowthere[0];
	$nowthere[5] = $nowthere[1];
	$nowthere[6] = $nowthere[2];
	$nowthere[7] = $nowthere[3];
	break;
    case XVP_IPCHECK_ON:
	if (isset($_SERVER['REMOTE_ADDR'])) {
	    $addr = ip2long($_SERVER['REMOTE_ADDR']);
	    $nowthere[4] = chr(($addr & 0xff000000) >> 24);
	    $nowthere[5] = chr(($addr & 0xff0000) >> 16);
	    $nowthere[6] = chr(($addr & 0xff00) >> 8);
	    $nowthere[7] = chr($addr & 0xff);
	} else {
	    // this will never work at the other end!
	    $nowthere[4] = $nowthere[5] = $nowthere[6] = $nowthere[7] = 0;
	}
	break;
    case XVP_IPCHECK_HTTP:
	$nowthere[4] = chr(ord($nowthere[0]) ^ ord('H'));
	$nowthere[5] = chr(ord($nowthere[1]) ^ ord('T'));
	$nowthere[6] = chr(ord($nowthere[2]) ^ ord('T'));
	$nowthere[7] = chr(ord($nowthere[3]) ^ ord('P'));
	break;
    }

    $iv = "\x00\x00\x00\x00\x00\x00\x00\x00";
    $td = mcrypt_module_open(MCRYPT_DES, "", MCRYPT_MODE_ECB, "");

    mcrypt_generic_init($td, $key, $iv);
    $hex = mcrypt_generic($td, $nowthere);
    mcrypt_generic_deinit($td);
    mcrypt_module_close($td);

    xvp_password_hex_to_text($hex, $text, XVP_PASSWORD_VNC);

    return $text;
}
?>
