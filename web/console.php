<?php
/*
 * console.php - Console launcher for Xen VNC Proxy PHP Pages
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
 *                    IMPORTANT SECURITY WARNING
 *                    --------------------------
 *
 * This page exposes the encrypted one-time VNC password to the user's
 * web browser.
 *
 * It is intended to appear in a zero-sized iframe, from a password-
 * protected parent page.  Other uses could be insecure.
 */

echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Console</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Console" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
<link rel="stylesheet" type="text/css" href="css/styles.css" />
</head>
<body>
<?php

require "./globals.inc";
require "./classes.inc";
require "./libc.inc";
require "./config.inc";
require "./logging.inc";
require "./password.inc";
require "./database.inc";

main();
?>
</body>
</html>

<?php

function main()
{
    global $xvp_multiplex_vm, $xvp_otp_ipcheck;

    // Using REQUEST not POST, for easier integration with custom web pages
    $poolname = stripslashes($_REQUEST['poolname']);
    $vmname = stripslashes($_REQUEST['vmname']);

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    if (!($pool = xvp_config_pool_by_name($poolname)) ||
	!($vm = xvp_config_vm_by_name($pool, $vmname)))
	return;

    if (xvp_is_uuid($vmname))
	$vm->uuid = $vm->vmname;

    if (!xvp_db_user_may_perform($vm, null, "console"))
	return;

    $kbdmouse = xvp_db_user_may_perform($vm, null, "kbdmouse");

    if ($kbdmouse) {
	$readonly = "No";
	$controls = "Yes";
	$shutdown = xvp_db_user_may_perform($vm, null, "shutdown") ? "Yes" : "No";
	$reboot   = xvp_db_user_may_perform($vm, null, "reboot")   ? "Yes" : "No";
	$reset    = xvp_db_user_may_perform($vm, null, "reset")    ? "Yes" : "No";
    } else {
	$readonly = "Yes";
	$controls = $shutdown = $reboot = $reset = "No";
    }

    if (isset($xvp_multiplex_vm))
	$port = $xvp_multiplex_vm->port;
    else if ($vm->port != 0)
	$port = $vm->port;
    else
	return;

    $user = (isset($_SERVER['REMOTE_USER'])) ?
	xvp_xmlescape($_SERVER['REMOTE_USER']) : "";
    $password = xvp_password_otp($vm->password);

    if (false) {
	/*
	 * Target is a sequence of UTF-8 bytes, but Java applet could be
	 * expecting any encoding, we just don't know, so we could pass
	 * to it as printable hex: "UTF-8:xx:xx:xx:xx....xx", which
	 * would also mean we don't want to xvp_xmlescape() it.
	 *
	 * Although xvpviewer >= 1.8.0 supports this encoding, I've not
	 * found any browser/Java combinations that actually need it, so
	 * I'm leaving this code disabled for now.
	 * 
	 */
	$target = $poolname . ":" . $vmname;
	$xvptarget = "UTF-8";
	for ($i = 0; $i < strlen($target); $i++)
	    $xvptarget .= sprintf(":%02x", ord($target[$i]));
    } else {
	$xvptarget = xvp_xmlescape($poolname . ":" . $vmname);
    }

    echo <<<EOF1
<applet code="VncViewer.class" archive="VncViewer.jar">
  <param name="port" value="$port" />
  <param name="open new window" value="yes" />
  <param name="offer relogin" value="no" />
  <param name="user" value="$user" />
  <param name="vm" value="$xvptarget" />
  <param name="xvppassword" value="$password" />
  <param name="xvpshutdown" value="$shutdown" />
  <param name="xvpreboot" value="$reboot" />
  <param name="xvpreset" value="$reset" />
  <param name="read only" value="$readonly" />
  <param name="show controls" value="$controls" />
EOF1;

    if ($xvp_otp_ipcheck == XVP_IPCHECK_HTTP) {
	$proxyhost = $_SERVER['SERVER_NAME'];
	$proxyport = $_SERVER['SERVER_PORT'];
	if (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] == "on") {
	    $factory = "HTTPSConnectSocketFactory";
	} else {
	    $factory = "HTTPConnectSocketFactory";
	}
  
	echo <<<EOF2

  <param name="proxyhost1" value="$proxyhost" />
  <param name="proxyport1" value="$proxyport" />
  <param name="socketfactory" value="$factory" />
EOF2;
    }

    echo <<<EOF3

</applet>

EOF3;
}
?>
