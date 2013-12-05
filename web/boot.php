<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * boot.php - VM booter for Xen VNC Proxy PHP Pages
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
    global $poolname, $vmname, $jsvmname, $booted, $recovery;

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

    if (!xvp_db_user_may_perform($vm, null, "boot"))
	return;

    if ($recovery && !xvp_db_user_may_perform($vm, null, "bootrecovery"))
	return;

    if ($recovery) {
	/*
	 * A recovery boot means using HVM, even if guest is PV, and
	 * setting boot device order to CD/DVD drive and then network.
	 * However, we must restore original settings immediately
	 * after boot completes or fails.
	 */

	$policy = xenapi_vm_get_hvm_boot_policy($session, $ref);
	$params = xenapi_vm_get_hvm_boot_params($session, $ref);

	if ($policy === false || $params === false)
	    return;
	
	if (isset($params["order"]))
	    $order = $params["order"];
	else
	    $order = "";

	$params["order"] = "dn";
	if (!xenapi_vm_set_hvm_boot_params($session, $ref, $params))
	    return;

	if (!xenapi_vm_set_hvm_boot_policy($session, $ref, "BIOS order")) {
	    $params["order"] = $order;
	    xenapi_vm_set_hvm_boot_params($session, $ref, $params);
	    return;
	}
    }

    $prio = xenapi_vm_get_restart_priority($session, $ref);
    $always = xenapi_vm_get_ha_always_run($session, $ref);

    if (xenapi_vm_start($session, $ref)) {
	$booted = "true";
	if (strlen($prio) > 0 && $always === false)
	    // looks like HA disabled for VM on shutdown, so re-enable
	    xenapi_vm_set_ha_always_run($session, $ref, true);
    }

    if ($recovery) {
	/*
	 * Restore original HVM/PV and boot order settings.
	 */
	xenapi_vm_set_hvm_boot_policy($session, $ref, $policy);
	$params["order"] = $order;
	xenapi_vm_set_hvm_boot_params($session, $ref, $params);
    }
}

$poolname = stripslashes($_POST['poolname']);
$jsvmname = $_POST['vmname'];
$vmname   = stripslashes($jsvmname);
$fullname = xvp_make_fullname($poolname, $vmname);
$booted = "false";
if (!isset($recovery)) // set to true in bootrecovery.php wrapper
    $recovery = false;

main();

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Boot</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Boot" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
<link rel="stylesheet" type="text/css" href="css/styles.css" />
</head>
<body onload="window.parent.actionCallback('$fullname', '$jsvmname', 'boot', '$booted');">
</body>
</html>
EOF;

?>
