<?php
/*
 * update.php - Update provider for Xen VNC Proxy PHP Pages
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

header("Content-type: text/xml");

/*
 * The code in some of these is fairly directly translated from the
 * the C source code of xvp, and needs to be maintained in step.
 */

require "./globals.inc";
require "./classes.inc";
require "./libc.inc";
require "./config.inc";
require "./logging.inc";
require "./password.inc";
require "./database.inc";

/*
 * Edd Dumbill's XML-RPC for PHP
 */
require "./xmlrpc.inc";

/*
 * Interface to the Xen API
 */
require "./xenapi.inc";

main();

function pool_update($pool)
{
    if (xvp_db_user_rights(null, $pool) == "none") {
	$pool->poolname = false;
	return;
    }

    if (($session = xenapi_login($pool, $master_ref)) === false)
	return;
    $pool->session = $session;

    if (xvp_db_user_host_rights($pool) == "none") {
	foreach ($pool->hosts as $host)
	    $host->hostname = false;
    } else {
	foreach ($pool->hosts as $host)
	    host_update($pool, $host, $session, $master_ref);
    }

    foreach ($pool->vms as $vm)
	vm_update($pool, $vm, $session);
}

function host_update($pool, $host, $session, $master_ref)
{
    if ($host->address !== false) {
	$shortname = $host->hostname;
	$longname = $shortname . $pool->domainname;
    } else if ($host->hostname_is_ip) {
	if (($longname = gethostbyaddr($host->hostname)) == $host->hostname) {
	    $shortname = $longname;
	} else if (strstr($longname, $pool->domainname)) {
	    $shortname = str_replace($pool->domainname, "", $longname);
	} else if (strstr($longname, ".")) {
	    $shortname = $longname;
	} else {
	    $shortname = $longname;
	    $longname .= $pool->domainname;
	}
    } else {
	$shortname = $host->hostname;
	$longname = $shortname . $pool->domainname;
    }

    // try both FQDN and non-FQDN host names
    if (($ref = xenapi_host_by_name($session, $longname)) === false &&
	($shortname == $longname ||
	 ($ref = xenapi_host_by_name($session, $shortname)) === false))
	return;

    if (!xenapi_host_get_enabled($session, $ref))
	return;

    if (($mem = xenapi_host_compute_free_memory($session, $ref)) === false)
	return;
    $host->memory_free = sprintf("%.1f", $mem / (1024 * 1024 * 1024));

    if (($host->metrics = xenapi_host_get_metrics($session, $ref)) === false)
	return;

    if (($mem = xenapi_host_get_memory_total($session, $host->metrics)) > 0)
	$host->memory_total = sprintf("%.1f", $mem / (1024 * 1024 * 1024));

    if ($ref == $master_ref)
	$host->is_master = true;

    $osversion = xenapi_host_get_software_version($session, $ref);
    if ($osversion !== false)
	$host->os_version = $osversion;

    $host->state = "Running";
}

function vm_update($pool, $vm, $session)
{
    if (xvp_db_user_rights($vm, null) == "none") {
	$vm->vmname = false;
	return;
    }

    if (xvp_is_uuid($vm->vmname)) {
	if (($ref = xenapi_vm_by_uuid($session, $vm->vmname)) === false ||
	    ($vmname = xenapi_vm_get_name_label($session, $ref)) === false)
	    return;
	$vm->uuid = $vm->vmname;
	$vm->vmname = $vmname;
    } else if (($ref = xenapi_vm_by_name($session, $vm->vmname)) === false) {
	    return;
    }

    if (($vm->state = xenapi_vm_get_power_state($session, $ref)) != "Running")
	return;

    $vm->metrics = xenapi_vm_get_metrics($session, $ref);
    $vm->guest_metrics = xenapi_vm_get_guest_metrics($session, $ref);

    if ($vm->metrics !== false) {
	if (($mem = xenapi_vm_get_memory_actual($session, $vm->metrics)) > 0) {
	    $mem = $mem / (1024 * 1024);
	    if ($mem >= 1024)
		$vm->memory_total = sprintf("%.1f GB", $mem / 1024);
	    else
		$vm->memory_total = sprintf("%d MB", $mem);
	}
	if ($vm->state == "Running")
	    $vm->uptime = vm_uptime(xenapi_vm_get_start_time($session, $vm->metrics));
    }

    if ($vm->guest_metrics !== false) {
	$osversion = xenapi_vm_get_os_version($session, $vm->guest_metrics);
	if ($osversion !== false)
	    $vm->os_version = $osversion; 
    }
}

function vm_xvp_appliance($os_version)
{
    if ($os_version === false)
	return false;
    return stripos($os_version, "xvp") !== false;
}

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

function pool_xml($pool)
{
    if ($pool->poolname == false)
	return;

    $poolname = xvp_xmlescape($pool->poolname);

    echo "  <pool name=\"$poolname\">\n";
    foreach ($pool->hosts as $host)
	host_xml($host);
    foreach ($pool->vms as $vm)
	vm_xml($vm);
    echo "  </pool>\n";

}

function host_xml($host)
{
    if ($host->hostname == false)
	return;

    if ($host->memory_total != 0)
	$memfree = $host->memory_free . "/" . $host->memory_total . " GB free";
    else
	$memfree = "";

    if ($host->is_master)
	$role = "master";
    else if ($host->state == "Running")
	$role = "slave";
    else
	$role = "";

    $fullname = xvp_xmlescape($host->fullname);

    echo "    <host fullname=\"$fullname\" role=\"$role\" state=\"$host->state\" osversion=\"$host->os_version\" memfree=\"$memfree\" />\n";
}

function vm_xml($vm)
{
    if ($vm->vmname == false)
	return;

    if (!($memtotal = $vm->memory_total))
	$memtotal = "";
    if (vm_os_windows($vm->os_version))
	$platform = "windows";
    else if (vm_xvp_appliance($vm->os_version))
	$platform = "xvp";
    else if (vm_os_linux($vm->os_version))
	$platform = "linux";
    else
	$platform = "blank";

    $rights   = xvp_db_user_rights($vm, null);
    $fullname = xvp_xmlescape($vm->fullname);
    if (xvp_is_uuid($vm->vmname))
	$vmname = "Unknown";
    else
	$vmname   = xvp_xmlescape($vm->vmname);

    echo "    <vm fullname=\"$fullname\" label=\"$vmname\" rights=\"$rights\" state=\"$vm->state\" platform=\"$platform\" osversion=\"$vm->os_version\" uptime=\"$vm->uptime\" memtotal=\"$memtotal\" />\n";
}

function main()
{
    global $xvp_pools, $xvp_now;

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    $map = xvp_db_get_rights_map();
    
    echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    echo "<response>\n  <timestamp>$xvp_now</timestamp>\n";

    echo "  <map name=\"operations\">\n";
    foreach ($map as $operation => $rights) {
	$name = xvp_xmlescape($operation);
	echo "    <operation name=\"$name\" rights=\"$rights\" />\n"; 
    }
    echo "  </map>\n";

    foreach ($xvp_pools as $pool) {
	pool_update($pool);
	pool_xml($pool);
    }

    echo "</response>\n";
}

?>
