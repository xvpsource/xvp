<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * poweroff.php - VM force shutdown for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2011, Colin Dean
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

require "./globals.inc";
require "./classes.inc";
require "./libc.inc";
require "./config.inc";
require "./logging.inc";
require "./password.inc";
require "./database.inc";
require "./xmlrpc.inc";
require "./xenapi.inc";

function main()
{
    global $poolname, $vmname, $jsvmname, $poweredoff;

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    if (!($pool = xvp_config_pool_by_name($poolname)) ||
	!($vm = xvp_config_vm_by_name($pool, $vmname)))
	return;

    if (($session = xenapi_login($pool, $master_ref)) === false)
	return;

    if (xvp_is_uuid($vmname)) {
	if (($ref = xenapi_vm_by_uuid($session, $vmname)) === false ||
	    ($vmname = xenapi_vm_get_name_label($session, $ref)) === false)
	    return;
	$vm->uuid = $vm->vmname;
	$vm->vmname = $vmname;
	$jsvmname = addslashes($vmname);
    } else if (($ref = xenapi_vm_by_name($session, $vmname)) === false) {
	    return;
    }

    if (!xvp_db_user_may_perform($vm, null, "poweroff"))
	return;

    $always = xenapi_vm_get_ha_always_run($session, $ref);
    if ($always === true) {
	// disable HA prior to shutdown
	xenapi_vm_set_ha_always_run($session, $ref, false);
    }


    if (xenapi_vm_hard_shutdown($session, $ref)) {
	$poweredoff = "true";
    } else if ($always === true) {
	    xenapi_vm_set_ha_always_run($session, $ref, true);
    }
}

$poolname   = stripslashes($_POST['poolname']);
$jsvmname   = $_POST['vmname'];
$vmname     = stripslashes($jsvmname);
$fullname   = xvp_make_fullname($poolname, $vmname);
$poweredoff = "false";

main();

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Force Shutdown</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Force Shutdown" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
<link rel="stylesheet" type="text/css" href="css/styles.css" />
</head>
<body onload="window.parent.actionCallback('$fullname', '$jsvmname', 'force shutdown', '$poweredoff');">
</body>
</html>
EOF;

?>
