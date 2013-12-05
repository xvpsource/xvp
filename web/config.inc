<?php
/*
 * config.inc - configuration handling for Xen VNC Proxy PHP Pages
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

function xvp_config_bad()
{
    global $xvp_config_depth, $xvp_config_filenames, $xvp_config_linenums;

    xvp_log(XVP_LOG_FATAL, "%s: Syntax error at line %d",
	    $xvp_config_filenames[$xvp_config_depth],
	    $xvp_config_linenums[$xvp_config_depth]);
}

function xvp_config_parse_line(&$line, &$wordv, &$linenum)
{
    global $xvp_config_depth;
    global $xvp_config_filenames, $xvp_config_streams, $xvp_config_linenums;

    $stream   = $xvp_config_streams[$xvp_config_depth];
    $filename = $xvp_config_filenames[$xvp_config_depth];

    if (($line = fgets($stream, XVP_CONFIG_LINEBUF_SIZE)) === false) {

	if (feof($stream)) {
	    fclose($stream);
	    if ($xvp_config_depth == 0)
		return -1;
	    $xvp_config_depth--;
	    return 0;
	}

	xvp_log(XVP_LOG_FATAL, "%s: Read error", $filename);
    }

    $linenum = ++$xvp_config_linenums[$xvp_config_depth];

    $wordc = 0;

    if ((($pos = strpos($line, "#")) !== false) ||
	(($pos = strpos($line, "\r")) !== false) ||
	(($pos = strpos($line, "\n")) !== false)) {
	if ($pos === 0)
	    return 0;
	$line = substr($line, 0, $pos);
    }

    $quoted = false;
    $bad = true;

    for ($wordc = 0; $wordc < XVP_CONFIG_MAX_WORDS; $wordc++) {
	$line = ltrim($line);
	if (strpos($line, '"') === 0) {
	    $line = substr($line, 1);
	    $quoted = true;
	}
	if (strlen($line) == 0) {
	    $bad = false;
	    break;
	}
	if ($quoted) {
	    $arr = explode('"', $line, 2);
	    $quoted = false;
	} else {
	    $arr = preg_split('/[ \t]+/', $line, 2);
	}

	$wordv[$wordc] = $arr[0];
	if (count($arr) == 1) {
	    $wordc++;
	    $bad = false;
	    break;
	}
	$line = $arr[1];
    }

    if ($bad)
	xvp_config_bad();

    if ($wordc != 2 || $wordv[0] != "INCLUDE")
	return $wordc;

    if ($xvp_config_depth + 1 >= XVP_CONFIG_MAX_DEPTH)
	xvp_log(XVP_LOG_FATAL, "%s: Too many levels of INCLUDE at line %d",
		$filename, $linenum);

    if (($stream = @fopen($wordv[1], "r")) === false)
	xvp_log(XVP_LOG_FATAL, "Unable to open %s", $wordv[1]);

    $xvp_config_depth++;
    $xvp_config_filenames[$xvp_config_depth] = $wordv[1];
    $xvp_config_streams[$xvp_config_depth]   = $stream;
    $xvp_config_linenums[$xvp_config_depth]  = 0;

    return 0;
}

function xvp_config_pool_by_name($poolname)
{
    global $xvp_pools;

    foreach ($xvp_pools as $pool)
	if ($pool->poolname == $poolname)
	    return $pool;

    return null;
}

function xvp_config_host_by_name($pool, $hostname)
{
    global $xvp_pools;

    if (!$pool) {
	foreach ($xvp_pools as $pool)
	    if ($host = xvp_config_host_by_name($pool, $hostname))
		return $host;
	return null;
    }

    foreach ($pool->hosts as $host)
	if ($host->hostname == $hostname)
	    return $host;

    return null;
}

function xvp_config_vm_by_name($pool, $vmname)
{
    global $xvp_pools;

    if (!$pool) {
	foreach ($xvp_pools as $pool)
	    if ($vm = xvp_config_vm_by_name($pool, $vmname))
		return $vm;
	return null;
    }

    foreach ($pool->vms as $vm)
	if ($vm->vmname == $vmname)
	    return $vm;

    return null;
}


function xvp_config_vm_by_port($port)
{
    global $xvp_pools, $xvp_multiplex_vm;

    if (!$port)
	return null;

    if (isset($xvp_multiplex_vm) && $xvp_multiplex_vm->port == $port)
	return $xvp_multiplex_vm;

    foreach ($xvp_pools as $pool)
	foreach ($pool->vms as $vm)
	    if ($vm->port == $port)
		return $vm;

    return null;
}

function xvp_config_init()
{
    global $xvp_pools, $xvp_multiplex_vm, $xvp_db, $xvp_config_depth;
    global $xvp_otp_mode, $xvp_otp_ipcheck, $xvp_otp_window;
    global $xvp_config_filenames, $xvp_config_streams, $xvp_config_linenums;

    $xvp_otp_mode    = XVP_OTP_ALLOW;
    $xvp_otp_ipcheck = XVP_OTP_IPCHECK;
    $xvp_otp_window  = XVP_OTP_WINDOW;

    $state = XVP_CONFIG_STATE_DATABASE;

    if (($stream = @fopen(XVP_CONFIG_FILENAME, "r")) === false)
	xvp_log(XVP_LOG_FATAL, "Unable to open %s", XVP_CONFIG_FILENAME);

    $xvp_config_depth     = 0;
    $xvp_config_filenames = array(0 => XVP_CONFIG_FILENAME);
    $xvp_config_streams   = array(0 => $stream);
    $xvp_config_linenums  = array(0 => 0);

    while (true) {
	$wordv = array();
	$wordc = xvp_config_parse_line($line, $wordv, $linenum);
	if ($wordc == 0)
	    continue;
	else if ($wordc < 0)
	    break;

	while (true) {

	    switch ($state) {

	    case XVP_CONFIG_STATE_DATABASE: /* DATABASE dsn ... */
		if ($wordv[0] != "DATABASE") {
		    $state = XVP_CONFIG_STATE_OTP;
		    break 1;
		}
		switch ($wordc) {
		case 2: /* DATABASE dsn */
		    $xvp_db = new Xvp_database($wordv[1], null, null);
		    break;
		case 3: /* DATABASE dsn username */
		    $xvp_db = new Xvp_database($wordv[1], $wordv[2], null);
		    break;
		case 4: /* DATABASE dsn username password */
		    if (strlen($wordv[3]) != XVP_MAX_XEN_PW * 2 ||
			!xvp_password_text_to_hex($wordv[3], $password,
						  XVP_PASSWORD_XEN))
			xvp_config_bad();
		    $xvp_db = new Xvp_database($wordv[1], $wordv[2], $password);
		    break;
		default:
		    xvp_config_bad();
		}
		$state = XVP_CONFIG_STATE_OTP;
		break 2;

	    case XVP_CONFIG_STATE_OTP: /* OTP REQUIRE|ALLOW|DENY [IPCHECK ON|OFF] [ window ] */
		if ($wordv[0] != "OTP") {
		    $state = XVP_CONFIG_STATE_MULTIPLEX;
		    break 1;
		}
		if ($wordc < 2 || $wordc > 5)
		    xvp_config_bad();
		if ($wordv[1] == "DENY")
		    $xvp_otp_mode = XVP_OTP_DENY;
		else if ($wordv[1] == "ALLOW")
		    $xvp_otp_mode = XVP_OTP_ALLOW;
		else if ($wordv[1] == "REQUIRE")
		    $xvp_otp_mode = XVP_OTP_REQUIRE;
		else
		    xvp_config_bad();
		if ($wordc >= 3 && $wordv[2] == "IPCHECK") {
		    if ($wordv[3] == "OFF")
			$xvp_otp_ipcheck = XVP_IPCHECK_OFF;
		    else if ($wordv[3] == "ON")
			$xvp_otp_ipcheck = XVP_IPCHECK_ON;
		    else if ($wordv[3] == "HTTP")
			$xvp_otp_ipcheck = XVP_IPCHECK_HTTP;
		    else
			xvp_config_bad();
		    $wordn = 4;
		} else {
		    $wordn = 2;
		}
		if ($wordc == $wordn + 1) {
		    $xvp_otp_window = atoi($wordv[$wordn]);
		    if ($xvp_otp_window < 1 ||
			$xvp_otp_window > XVP_OTP_MAX_WINDOW)
			xvp_config_bad();
		}
		$state = XVP_CONFIG_STATE_MULTIPLEX;
		break 2;

	    case XVP_CONFIG_STATE_MULTIPLEX: /* MULTIPLEX port */
		if ($wordv[0] != "MULTIPLEX") {
		    $state = XVP_CONFIG_STATE_POOL;
		    break 1;
		}
		if ($wordc != 2)
		    xvp_config_bad();
		if (substr($wordv[1], 0, 1) == ':') {
		    $port = atoi(substr($wordv[1], 1)) + XVP_VNC_PORT_MIN;
		    if ($port < XVP_VNC_PORT_MIN || $port > XVP_VNC_PORT_MAX)
			xvp_config_bad();
		} else {
		    $port = atoi($wordv[1]);
		    if ($port < 1024 || $port > 65535)
			xvp_config_bad();
		}
		$xvp_multiplex_vm = new Xvp_vm(null, "VM multiplexer");
		$xvp_multiplex_vm->port = $port;
		$state = XVP_CONFIG_STATE_POOL;
		break 2;

	    case XVP_CONFIG_STATE_POOL: /* POOL poolname */
		if ($wordv[0] != "POOL" || $wordc < 2)
		    xvp_config_bad();
		/* Pool name may contain spaces but not ":" */
		for ($i = 1, $line = ""; $i < $wordc; $i++) {
		    $line .= " " . $wordv[$i];
		}
		$line = substr($line, 1);
		if (strpos($line, ":") !== false)
		    xvp_config_bad();
		if (xvp_config_pool_by_name($line))
		    xvp_log(XVP_LOG_FATAL,
			    "%s: Duplicate pool name at line %d",
			    $xvp_config_filenames[$xvp_config_depth], $linenum);
		$new_pool = new Xvp_pool($line);
		$xvp_pools[] = $new_pool;
		$groupname = "";
		$state = XVP_CONFIG_STATE_DOMAIN;
		break 2;

	    case XVP_CONFIG_STATE_DOMAIN: /* DOMAIN this.that.com */
		if ($wordv[0] != "DOMAIN" || $wordc != 2)
		    xvp_config_bad();
		/* store with leading dot for ease of manipulation */
		$new_pool->domainname = (strlen($wordv[1]) > 0) ?
		    "." . $wordv[1] : "";
		$state = XVP_CONFIG_STATE_MANAGER;
		break 2;

	    case XVP_CONFIG_STATE_MANAGER: /* MANAGER username xen-password */
		if ($wordv[0] != "MANAGER" || $wordc != 3 ||
		    strlen($wordv[2]) != XVP_MAX_XEN_PW * 2 ||
		    !xvp_password_text_to_hex($wordv[2], $new_pool->password,
					      XVP_PASSWORD_XEN))
		    xvp_config_bad();
		$new_pool->manager = $wordv[1];
		$state = XVP_CONFIG_STATE_HOST;
		break 2;

	    case XVP_CONFIG_STATE_HOST: /* HOST hostname */
		if ($wordv[0] != "HOST") {
		    if (count($new_pool->hosts)) {
			$state = XVP_CONFIG_STATE_GROUP;
			break 1;
		    }
		    xvp_config_bad();
		}
		switch ($wordc) {
		case 2:
		    $address = false;
		    $hostname = $wordv[1];
		    break;
		case 3:
		    if (!xvp_is_ipv4($wordv[1]))
			xvp_config_bad();
		    $address = $wordv[1];
		    $hostname = $wordv[2];
		    break;
		default:
		    xvp_config_bad();
		    break;
		}
		if (xvp_config_host_by_name($new_pool, $wordv[1]))
		    xvp_log(XVP_LOG_FATAL,
			    "%s: Duplicate host name at line %d",
			    $xvp_config_filenames[$xvp_config_depth], $linenum);
		
		$new_host = new Xvp_host($new_pool, $hostname, $address);
		$new_pool->hosts[] = $new_host;
		break 2;

	    case XVP_CONFIG_STATE_GROUP: /* GROUP groupname */
		if ($wordv[0] != "GROUP") {
		    $state = XVP_CONFIG_STATE_VM;
		    break 1;
		}
		if ($wordc < 2)
		    xvp_config_bad();
		array_shift($wordv);
		$groupname = implode(" ", $wordv);
		break 2;

	    case XVP_CONFIG_STATE_VM: /* VM port vmname vnc-password */
		if ($wordv[0] == "GROUP") {
		    $state = XVP_CONFIG_STATE_GROUP;
		    break 1;
		} else if ($wordv[0] != "VM") {
		    if (count($new_pool->vms)) {
			$state = XVP_CONFIG_STATE_POOL;
			break 1;
		    }
		    xvp_config_bad();
		}
		if ($wordc != 4 ||
		    strlen($wordv[3]) != XVP_MAX_VNC_PW * 2 ||
		    !xvp_password_text_to_hex($wordv[3], $password,
					      XVP_PASSWORD_VNC))
		    xvp_config_bad();

		// see comments in config.c regarding supported port numbers
		if ($wordv[1] == "-") {
		    if (!isset($xvp_multiplex_vm))
			xvp_config_bad();
		    $port = 0;
		} else if (substr($wordv[1], 0, 1) == ':') {
		    $port = atoi(substr($wordv[1], 1)) + XVP_VNC_PORT_MIN;
		    if ($port < XVP_VNC_PORT_MIN || $port > XVP_VNC_PORT_MAX)
			xvp_config_bad();
		} else {
		    $port = atoi($wordv[1]);
		    if ($port < 1024 || $port > 65535)
			xvp_config_bad();
		}

		if (xvp_config_vm_by_name($new_pool, $wordv[2]))
		    xvp_log(XVP_LOG_FATAL,
			    "%s: Duplicate vm name at line %d",
			    $xvp_config_filenames[$xvp_config_depth], $linenum);

		if (xvp_config_vm_by_port($port))
		    xvp_log(XVP_LOG_FATAL,
			    "%s: Duplicate port number at line %d",
			    $xvp_config_filenames[$xvp_config_depth], $linenum);

		// web interface doesn't support hosts as VMs
		foreach ($new_pool->hosts as $host)
		    if ($host->hostname == $wordv[2] ||
			$host->address == $wordv[2])
			break 3;

		$new_vm = new Xvp_vm($new_pool, $wordv[2]);
		$new_vm->port = $port;
		$new_vm->password = $password;
		$new_vm->groupname = $groupname;
		$new_pool->vms[] = $new_vm;
		break 2;
	    }
	}

    }

}
?>
