<?php
/*
 * globals.inc - globals and constants for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2009-2011, Colin Dean
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
  * This code needs to be kept consistent with xvp's xvp.h.
  */

if (defined("E_DEPRECATED"))
    ini_set("error_reporting", "E_ALL & ~E_NOTICE & ~E_DEPRECATED");
else
    ini_set("error_reporting", "E_ALL & ~E_NOTICE");

define("XVP_LOG_DEBUG", 1);
define("XVP_LOG_INFO",  2);
define("XVP_LOG_ERROR", 3);
define("XVP_LOG_FATAL", 4);

define("XVP_PASSWORD_XEN", 1);
define("XVP_PASSWORD_VNC", 2);

define("XVP_MAX_XEN_PW", 16);
define("XVP_MAX_VNC_PW", 8);

define("XVP_VNC_PORT_MIN", 5900);
define("XVP_VNC_PORT_MAX", 5999);

define("XVP_MAX_ADDRESS", 15);

define("XVP_CONFIG_FILENAME", "/etc/xvp.conf");
define("XVP_CONFIG_MAX_DEPTH", 5);
define("XVP_CONFIG_MAX_WORDS", 10);
define("XVP_CONFIG_LINEBUF_SIZE", 256);

define("XVP_CONFIG_STATE_DATABASE",  1);
define("XVP_CONFIG_STATE_OTP",       2);
define("XVP_CONFIG_STATE_MULTIPLEX", 3);
define("XVP_CONFIG_STATE_POOL",      4);
define("XVP_CONFIG_STATE_DOMAIN",    5);
define("XVP_CONFIG_STATE_MANAGER",   6);
define("XVP_CONFIG_STATE_HOST",      7);
define("XVP_CONFIG_STATE_GROUP",     8);
define("XVP_CONFIG_STATE_VM",        9);

define("XVP_IPCHECK_OFF",  1);
define("XVP_IPCHECK_ON",   2);
define("XVP_IPCHECK_HTTP", 3);

define("XVP_OTP_DENY",    1);
define("XVP_OTP_ALLOW",   2);
define("XVP_OTP_REQUIRE", 3);
define("XVP_OTP_IPCHECK", XVP_IPCHECK_OFF);
define("XVP_OTP_WINDOW", 60);
define("XVP_OTP_MAX_WINDOW", 3600);

define("XVP_DB_LINEBUF_SIZE", 256);
define("XVP_DB_COLUMN_SIZE", 64);
define("XVP_DB_TABLE_NAME", "xvp_users");

define("XVP_RIGHTS_DEFAULT_FILENAME", "/usr/share/xvp/xvprights.default");
define("XVP_RIGHTS_CUSTOM_FILENAME",  "/etc/xvprights.conf");

define("XVP_POPUP_WIDTH",  700);
define("XVP_POPUP_HEIGHT", 315);

define("XVP_MAX_DVD_NAME", 60);

define("XVP_XENAPI_VERSION", "1.3");

function xvp_global_init()
{
    global $xvp_pools, $xvp_multiplex_vm, $xvp_db, $xvp_rights_map, $xvp_now;
    global $xvp_otp_mode, $xvp_otp_ipcheck, $xvp_otp_window;

    $xvp_pools        = array();
    $xvp_multiplex_vm = null;
    $xvp_db           = null;
    $xvp_rights_map   = null;
    $xvp_now          = time();
    $xvp_otp_mode     = null;
    $xvp_otp_ipcheck  = null;
    $xvp_otp_window   = 0;
}

function xvp_xmlescape($text)
{
    return htmlspecialchars($text, ENT_QUOTES);
}

function xvp_is_ipv4($address)
{
    if (sscanf($address, "%u.%u.%u.%u%c",
	       $i1, $i2, $i3, $i4, $dummy) != 4)
	return false;

    if ($i1 > 255 || $i2 > 255 || $i3 > 255 || $i4 > 255)
	return false;

    return (strlen($address) <= XVP_MAX_ADDRESS);
}

function xvp_is_uuid($text)
{
    if (strlen($text) != 36 || $text[8] != '-' || $text[13] != '-' ||
	$text[18] != '-' || $text[23] != '-')
	return false;

    $text = str_replace('-', '', $text);
    return (strlen($text) == 32 && 
	    strlen(trim($text, "0123456789abcdef")) == 0);
}

function xvp_make_fullname($poolname, $thingname)
{
    if (!isset($poolname))
	$fullname = $thingname;
    else if (!isset($thingname))
	$fullname = $poolname;
    else
	$fullname = $poolname . '-' . $thingname;
    return preg_replace('/["\'\(),. ]/', '-', $fullname);
}

function ios_browser()
{
    $ios = false;
    // order of devices matters, or may mistake pod/pad for phone
    foreach (array("iPod", "iPad", "iPhone") as $platform) {
        if (strstr($_SERVER['HTTP_USER_AGENT'], $platform) !== false) {
            $ios = $platform;
            break;
        }
    }

    return $ios;
}

function ios_viewport()
{
    if (ios_browser())
	return '<meta name="viewport" content="width=device-width" />';

    return '';
}

function ios_css()
{
    if (ios_browser())
	return '<link rel="stylesheet" type="text/css" href="css/ios.css" />';

    return '';
}

?>
