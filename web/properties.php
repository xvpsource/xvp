<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * properties.php - VM Properties viewer for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2009-2012, Colin Dean
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

// FIX ME - duplicated from update.php
function vm_os_windows($os_version)
{
    if ($os_version === false)
	return false;
    return stripos($os_version, "microsoft") !== false;
}

function vm_os_linux($os_version)
{
    if ($os_version === false)
	return false;
    if (stripos($os_version, "redhat") !== false)
	return true;
    if (stripos($os_version, "centos") !== false)
	return true;
    if (stripos($os_version, "fedora") !== false)
	return true;
    if (stripos($os_version, "suse") !== false)
	return true;
    if (stripos($os_version, "debian") !== false)
	return true;
    if (stripos($os_version, "ubuntu") !== false)
	return true;
    if (stripos($os_version, "gentoo") !== false)
	return true;
    return false;
}

function vm_uptime($start_time)
{
    global $xvp_now;
    if (($then = strptime($start_time, "%Y%m%dT%H:%M:%SZ")) === false)
	return false;

    $then = gmmktime($then['tm_hour'], $then['tm_min'], $then['tm_sec'],
		     $then['tm_mon'] + 1, $then['tm_mday'],
		     $then['tm_year'] + 1900, 0);

    $days = floor(($xvp_now - $then) / 86400);
    $secs = ($xvp_now - $then) % 86400;
    $hours = floor($secs / 3600);
    $secs = $secs % 3600;
    $mins = floor($secs /60);

    return sprintf("%s days, %d:%02d", $days, $hours, $mins);
}


function sr_get_host_name($session, $sr_rec)
{
    /*
     * Although CLI "xe" happily returns host for any non-shared SR,
     * there doesn't seem to be a host-related field in SR records.
     *
     * This is a workaround, using the fact that PBDs do have hosts.
     */

    if ($sr_rec["shared"] || count($pbds = $sr_rec["PBDs"]) == 0)
	return false;
	
    $pbd = $sr_rec["PBDs"][0]->scalarval();
    if (($pbd_rec = xenapi_pbd_get_record($session, $pbd)) == false)
	return false;

    if (!isset($pbd_rec["host"]))
	return false;

    return xenapi_host_get_name_label($session, $pbd_rec["host"]);
}

function main()
{
    global $poolname, $vmname, $htvmname, $description;
    global $ha, $vm, $state, $platform, $home, $where, $cpus;
    global $current_dvd_uuid, $current_dvd_name, $possible_dvds;
    global $dvd_change, $dvd_iso, $dvd_host, $dvd_uuid, $dvd_error;

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    $platform = "blank";

    if (!($pool = xvp_config_pool_by_name($poolname)) ||
	!($vm = xvp_config_vm_by_name($pool, $vmname)))
	return;

    if (!xvp_db_user_may_perform($vm, null, "properties"))
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

    $description = xenapi_vm_get_name_description($session, $ref);
    if ($description === false)
	$description = "";
    else
	$description = htmlspecialchars($description);

    $dvd_iso    = xvp_db_user_may_perform($vm, null, "dvdiso");
    $dvd_host   = xvp_db_user_may_perform($vm, null, "dvdhost");
    $dvd_change = ($dvd_iso || $dvd_host);

    $prio = xenapi_vm_get_restart_priority($session, $ref);
    // 1 in older versions of XenServer seems to have become "restart" in
    // newer versions.  Based on Christian Scheele's patch of 23/07/2012.
    switch ($prio) {
    case 1:
    case "restart":
	$ha = "protected";
	break;
    case "best-effort":
	$ha = "restart if possible";
	break;
    default:
	$ha = "do not restart";
	break;
    }

    if (($aff = xenapi_vm_get_affinity($session, $ref)) !== false)
	$home = xenapi_host_get_name_label($session, $aff);
    if (($where = xenapi_vm_get_resident_on($session, $ref)) !== false)
	$where = xenapi_host_get_name_label($session, $where);

    $cpus = xenapi_vm_get_vcpus_max($session, $ref);

    // only handle 1st usable DVD drive found
    $current_dvd_name = null;
    $current_dvd_uuid = null;
    if (($vbds = xenapi_vm_get_vbds($session, $ref)) !== false) {
	foreach ($vbds as $vbd) {
	    if (($vbd_rec = xenapi_vbd_get_record($session, $vbd)) === false ||
		$vbd_rec["type"] != "CD")
		continue;
	    if ($vbd_rec["empty"]) {
		$current_dvd_name = "<empty>";
		$current_dvd_uuid = "empty";
	    } else {
		$vdi = $vbd_rec["VDI"];
		if (($vdi_rec = xenapi_vdi_get_record($session, $vdi)) === false)
		    continue;
		$current_dvd_name = $vdi_rec["name_label"];
		$current_dvd_uuid = $vdi_rec["uuid"];
	    }
	    break;
	}
    }

    $dvd_error = "";
    if ($dvd_change && isset($dvd_uuid) && $dvd_uuid != $current_dvd_uuid) {
	if ($dvd_uuid == "empty") {
	    $changed = xenapi_vbd_eject($session, $vbd);
	} else if ($dvd_uuid != $current_dvd_uuid) {
	    if ($current_dvd_uuid != "empty")
	    xenapi_vbd_eject($session, $vbd);
	    if ($vdi = xenapi_vdi_by_uuid($session, $dvd_uuid))
		$changed = xenapi_vbd_insert($session, $vbd, $vdi);
	    else
		$changed = false;
	}
	if ($changed) {
	    if (($current_dvd_uuid = $dvd_uuid) == "empty")
		$current_dvd_name = "<empty>";
	    else
		$current_dvd_name = xenapi_vdi_get_name_label($session, $vdi);
	} else {
	    $dvd_error = " !";
	}
    }


    $possible_dvds = array();
    $possible_dvds["empty"] = "<empty>";

    if (($srs = xenapi_sr_get_all($session)) !== false) {

	foreach ($srs as $sr) {

	    // SR needs to be an ISO Library or a physical DVD drive
	    if (($sr_rec = xenapi_sr_get_record($session, $sr)) === false ||
		!isset($sr_rec["content_type"]) ||
		$sr_rec["content_type"] != "iso")
		continue;

	    if (($sr_host = sr_get_host_name($session, $sr_rec)) !== false) {
		// If SR on one host and VM running on another, we can't use SR
		if ($where !== false && $where != $sr_host)
		    continue;
		$sr_rec["name_label"] = $sr_host;
	    }

	    $vdicount = 0;
	    $sr_dvds = array();
	    foreach ($sr_rec["VDIs"] as $vdi) {
		$vdi = $vdi->scalarval();
		if (($vdi_rec = xenapi_vdi_get_record($session, $vdi)) === false)
		    continue;

		// nasty hack for XenServer Tools, which may have unwanted VDIs
		if ($sr_rec["name_label"] == "XenServer Tools" &&
		    $vdi_rec["name_label"] != "xs-tools.iso")
		    continue;

		// Bodge physical drive naming
		$vdi_display = ($sr_host === false ) ?
		    $vdi_rec["name_label"] : "DVD drive $vdicount";

		$sr_dvds[$vdi_rec["uuid"]] = $sr_rec["name_label"] . ": $vdi_display";
		$vdicount++;
	    }
	    if ($vdicount > 0) {
		natcasesort($sr_dvds);
		$possible_dvds = array_merge($possible_dvds, $sr_dvds);
	    }
	}
    }

    $state = strtolower(xenapi_vm_get_power_state($session, $ref));

    $vm->metrics = xenapi_vm_get_metrics($session, $ref);
    if ($vm->metrics !== false) {
	if (($mem = xenapi_vm_get_memory_actual($session, $vm->metrics)) > 0) {
	    $mem = $mem / (1024 * 1024);
	    if ($mem >= 1024)
		$vm->memory_total = sprintf("%.1f GB", $mem / 1024);
	    else
		$vm->memory_total = sprintf("%d MB", $mem);
	}
	if ($state == "running")
	    $vm->uptime = vm_uptime(xenapi_vm_get_start_time($session, $vm->metrics));
    }


    if ($state != "running")
	return;

    $vm->guest_metrics = xenapi_vm_get_guest_metrics($session, $ref);
    if ($vm->guest_metrics !== false) {
	$osversion = xenapi_vm_get_os_version($session, $vm->guest_metrics);
	if ($osversion !== false)
	    $vm->os_version = $osversion;
	$toolsversion = xenapi_vm_get_pv_drivers_version($session, $vm->guest_metrics);
	if ($toolsversion == false) {
	    $vm->tools_info = "not installed";
	} else {
	    $toolsuptodate = xenapi_vm_get_pv_drivers_up_to_date($session, $vm->guest_metrics);
	    $vm->tools_info = sprintf("%s (%sup to date)", $toolsversion,
				      $toolsuptodate ? "" : "not ");
	}
    }

    if (vm_os_windows($vm->os_version))
	$platform = "windows";
    else if (vm_os_linux($vm->os_version))
	$platform = "linux";
}

function truncated_dvd_name($dvd_name)
{
    if (($len = strlen($dvd_name)) < XVP_MAX_DVD_NAME)
	return $dvd_name;

    $half = floor(XVP_MAX_DVD_NAME / 2) - 2;
    $offset = strlen($dvd_name) - $half;

    return substr($dvd_name, 0, $half) . "..." . substr($dvd_name, $offset); 
    
}

function show_dvd_drive()
{
    global $poolname, $vmname, $dvd_uuid, $dvd_error;
    global $current_dvd_uuid, $current_dvd_name, $possible_dvds;
    global $dvd_change, $dvd_iso, $dvd_host;

    if (!isset($current_dvd_name)) {
	echo "not found";
	return;
    }

    if (!$dvd_change) {
	foreach($possible_dvds as $uuid=> $name) {
	    if ($uuid == $current_dvd_uuid) {
		echo htmlspecialchars(truncated_dvd_name($name));
		return;
	    }
	}
	echo "not found";
	return;
    }

    $self = $_SERVER["PHP_SELF"];

echo <<<EOF

<form class="dvd" action="$self" method="post">

  <input type="hidden" name="poolname" value="$poolname" />
  <input type="hidden" name="vmname" value="$vmname" />

  <select name="dvduuid" onchange="document.forms[0].submit();">

EOF;

    $found = false;
    foreach($possible_dvds as $uuid => $name) {
	$htname = htmlspecialchars(truncated_dvd_name($name));
	if ($uuid == $current_dvd_uuid) {
	    $selected = " selected=\"selected\"";
	    $found = true;
	} else {
	    $selected = "";
	}
	/*
	 * Hide drives/ISOs where don't have rights to connect, unless it's
	 * currently connected, in which case show but don't allow click 
	 */
	if ($name == "<empty>")
	    $ok = true;
	else if (strpos($name, ": DVD drive ") === false)
	    $ok = $dvd_iso;
	else
	    $ok = $dvd_host;
	if ($ok)
	    $onclick = " onclick=\"document.forms[0].submit();\"";
	else if (!$selected)
	    continue;
	else
	    $onclick = "";
	echo "    <option value=\"$uuid\"$selected$onclick>$htname</option>\n";
    }

    if (!$found) // unlikely, but just in case ...
	echo "    <option value=\"unknown\" selected=\"selected\">&lt;unknown&gt;</option>\n";
    echo "  </select>$dvd_error\n\n</form>\n";
}

if (isset($_POST['dvduuid'])) {
    $poolname = stripslashes($_POST['poolname']);
    $vmname   = stripslashes($_POST['vmname']);
    $dvd_uuid = stripslashes($_POST['dvduuid']);
} else {
    $poolname = stripslashes(urldecode($_GET['poolname']));
    $vmname   = stripslashes(urldecode($_GET['vmname']));
}

$page = sprintf("%s?poolname=%s&vmname=%s", $_SERVER["PHP_SELF"],
		urlencode($poolname), urlencode($vmname));

$ioscss = ios_css();
$viewport = ios_viewport();

main();

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Properties - $htvmname</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Properties - $htvmname" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
$viewport
<link rel="stylesheet" type="text/css" href="css/styles.css" />
$ioscss
</head>
<body class="popup">
<div id="main">

<h1 class="properties">VM Properties - $htvmname</h1>

<img class="properties" src="images/$platform.png" />

<table class="properties">
  <tr><th>DVD drive:</th><td>
EOF;

show_dvd_drive();

echo <<<EOF
</td>
  <tr><th>&nbsp</th></tr>
  <tr><th>Description:</th><td>$description</td></tr>
  <tr><th>Power state:</th><td>$state</td></tr>
  <tr><th>Operating system:</th><td>$vm->os_version</td></tr>
  <tr><th>Xen tools:</th><td>$vm->tools_info</td></tr>
  <tr><th>Home server:</th><td>$home</td></tr>
  <tr><th>Running on:</th><td>$where</td></tr>
  <tr><th>Uptime:</th><td>$vm->uptime</td></tr>
  <tr><th>Virtual CPUs:</th><td>$cpus</td></tr>
  <tr><th>Memory:</th><td>$vm->memory_total</td></tr>
  <tr><th>HA priority:</th><td>$ha</td></tr>
</table>

<div class="properties">
  <button type="button" onclick="window.location = '$page';">Refresh</button>
  <button type="button" onclick="window.close();">Close</button>
</div>

</div>
</body>
</html>
EOF;

?>
