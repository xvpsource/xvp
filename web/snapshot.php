<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * snapshot.php - VM Snapshot manager for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2012, Colin Dean
 *
 * Partly based on code contributed by Julien Dary
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
    global $poolname, $vmname, $htvmname;
    global $vm, $reason, $snapshot, $snapshots, $message;

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    if (!($pool = xvp_config_pool_by_name($poolname)) ||
	!($vm = xvp_config_vm_by_name($pool, $vmname)))
	return;

    if (!xvp_db_user_may_perform($vm, null, "snapshot"))
	exit;

    if (($session = xenapi_login($pool, $master_ref)) === false)
	return;

    if (xvp_is_uuid($vmname)) {
	if (($ref = xenapi_vm_by_uuid($session, $vmname)) === false ||
	    ($label = xenapi_vm_get_name_label($session, $ref)) === false)
	    return;
	$vm->uuid = $vm->vmname;
	$htvmname = htmlspecialchars($label);
    } else if (($ref = xenapi_vm_by_name($session, $vmname)) === false) {
	    return;
    } else {
	$htvmname = htmlspecialchars($vmname);
    }

    switch ($reason) {
    case 'init':
	$message = "";
	break;

    case 'create':
	if (xenapi_vm_snapshot($session, $ref, $snapshot) !== false)
	    $message = "Successfully created snapshot";
	else
	    $message = "Error creating snapshot";
	break;

    case 'revert':
	if (xenapi_vm_revert($session, $snapshot) !== false)
	    $message = "Successfully reverted to snapshot";
	else
	    $message = "Error reverting to snapshot";
	break;

    case 'delete':
	// destroying VM will destroy its VBDs but not the underlying VDIs
	$delete_vdis = array();
	if (($vbds = xenapi_vm_get_vbds($session, $snapshot)) !== false) {
	    foreach ($vbds as $vbd) {
		if (($vbd_rec = xenapi_vbd_get_record($session, $vbd)) === false)
		    continue;
		if ($vbd_rec["type"] != "Disk")
		    continue;
		$vdi = $vbd_rec["VDI"];
		if (($vdi_rec = xenapi_vdi_get_record($session, $vdi)) === false)
		    continue;
		if ($vdi_rec["is_a_snapshot"] && count($vdi_rec["VBDs"]) == 1) {
		    $delete_vdis[] = $vdi;
		}
	    }
	}
	if (($ok = xenapi_vm_destroy($session, $snapshot))) {
	    foreach ($delete_vdis as $vdi) {
		$ok &= xenapi_vdi_destroy($session, $vdi);
	    }
	}
	if ($ok)
	    $message = "Successfully deleted snapshot";
	else
	    $message = "Error deleting snapshot";
	break;
    }

    $snapshots = array();
    $snaprefs = xenapi_vm_get_snapshots($session, $ref);	

    foreach ($snaprefs as $snapref) {
	$snapname = xenapi_vm_get_name_label($session, $snapref);
	if ($snapname !== false)
	    $snapshots[$snapref]= $snapname;
    }
}

function formatted_snap_name($name)
{
    // many browsers ignore width specifications for <select> and <option> 
    return str_replace(" ", "&nbsp;", str_pad(htmlspecialchars($name), 70, " "));
}

function show_snapshots()
{
    global $poolname, $vmname, $snapshots;

echo <<<EOF

  <form class="action" method="post">
    <p>Existing snapshots:</p>
    <input type="hidden" name="poolname" value="$poolname" />
    <input type="hidden" name="vmname" value="$vmname" />
    <input type="hidden" name="reason" value="" />
    <select name="snapref" size="3">

EOF;

    if (count($snapshots) == 0) {
	    $htsnapname = formatted_snap_name("no snapshots found");
	echo "      <option class=\"snapshot\" value=\"0\">$htsnapname</option>\n";
    } else {
	foreach ($snapshots as $snapref => $snapname) {
	    $htsnapname = formatted_snap_name($snapname);
	    echo "      <option class=\"snapshot\" value=\"$snapref\">$htsnapname</option>\n";
	}
    }
echo <<<EOF
    </select>

    <div class="action">
      <button type="button" onclick="submit_form('revert');">Revert</button>
      <button type="button" onclick="submit_form('delete');">Delete</button>
    </div>

    <div class="action">
      New snapshot:
      <input class="snapshot" name="snapname" type="text"></input>
      <button type="button" onclick="submit_form('create');">Create</button>
    </div>

    <div class="action">
      <button type="button" onclick="window.close();">Close</button>
    </div>
  </form>

EOF;
}

if (isset($_POST['reason'])) {
    $reason   = $_POST['reason'];
    $poolname = stripslashes($_POST['poolname']);
    $vmname   = stripslashes($_POST['vmname']);
    if ($reason == 'create') {
	$snapshot = trim(stripslashes($_POST['snapname']));
    } else {
	$snapshot = stripslashes($_POST['snapref']);
    }
} else {
    $reason   = 'init';
    $poolname = stripslashes(urldecode($_GET['poolname']));
    $vmname   = stripslashes(urldecode($_GET['vmname']));
}

$ioscss = ios_css();
$viewport = ios_viewport();

main();

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Snapshots - $htvmname</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Snapshots - $htvmname" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
$viewport
<link rel="stylesheet" type="text/css" href="css/styles.css" />
$ioscss
<script type="text/javascript">

if (!String.prototype.trim) {
    // IE < 9
    String.prototype.trim = function() {
	return this.replace(/^\s+|\s+$/g, '');
    }
}

function submit_form(reason)
{
    var f = document.forms[0];

    if (reason == "create") {
	var name = f.snapname.value.trim();
	if (name.length == 0) {
	    alert("Please enter name for new snapshot");
	    return;
	}
	for (i = 0; i < f.snapref.options.length; i++) {
	    var snapname = f.snapref.options[i].text.replace(/\u00a0/g, ' ').trim();
	    if (snapname === name) {
	    	alert("An existing snapshot has this name");
	    	return;
	    }
	}
    } else if (reason == "revert" || reason == "delete") {
	if (f.snapref.options[0].value == 0) {
	    alert("No existing snapshots found");
	    return;
	} else if (f.snapref.selectedIndex < 0) {
	    alert("Please select an existing snapshot");
	    return;
	}
    }

    f.reason.value = reason;
    f.submit();
}

function show_message(msg)
{
    if (msg.length > 0)
	alert(msg);
}

</script>
</head>
<body class="popup" onload="show_message('$message');">
<div id="main">

  <h1 class="snapshot">VM Snapshots - $htvmname</h1>

EOF;

show_snapshots();

echo <<<EOF

</div>
</body>
</html>
EOF;

?>
