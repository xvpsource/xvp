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
 *
 * Optimisations using get_all_records calls based on contributions
 * by Regis Wilson, Nov-Dec 2011.
 *
 * This is not the default version of update.php, as it doesn't work
 * for some sites (for reasons as yet undetermined).  Use at your
 * own risk!
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

	// We used to make lots of per-host Xen API calls, but grabbing
	// the details for all hosts in 2 calls should be much faster
	$allhosts = xenapi_host_get_all_records($session);
	$allhostmetrics = xenapi_host_metrics_get_all_records($session);
	$hostsbyuuid = array();
	foreach ($allhosts as $ref => $oid) {
	    $hostobj_r   = $oid->scalarval();
	    $hostmref    = $hostobj_r["metrics"]->scalarval();
	    $hostuuid    = $hostobj_r["uuid"]->scalarval();
	    $hostname    = $hostobj_r["hostname"]->scalarval();
	    $hostaddr    = $hostobj_r["address"]->scalarval();
	    $hostenabled = $hostobj_r["enabled"]->scalarval();
	    $swversion   = $hostobj_r["software_version"]->scalarval();
	    $hostversion = $swversion["product_version"]->scalarval();
	    $hostbrand   = $swversion["product_brand"]->scalarval();
	    $hostbuild   = $swversion["build_number"]->scalarval();

	    $hostsbyuuid[$hostuuid]["hostname"]   = $hostname;
	    $hostsbyuuid[$hostuuid]["address"]    = $hostaddr;
	    $hostsbyuuid[$hostuuid]["enabled"]    = $hostenabled;
	    $hostsbyuuid[$hostuuid]["os_version"] = "$hostbrand $hostversion build $hostbuild";
	    $hostsbyuuid[$hostuuid]["is_master"]  = ($ref == $master_ref);

	    if ($hostenabled) {
		$hostm        = $allhostmetrics[$hostmref]->scalarval();
		$hostmemtotal = $hostm["memory_total"]->scalarval();
		$hostmemfree  = $hostm["memory_free"]->scalarval();

		$hostsbyuuid[$hostuuid]["memory_total"] = $hostmemtotal;
		$hostsbyuuid[$hostuuid]["memory_free"]  = $hostmemfree;
	    }
	}

	foreach ($pool->hosts as $host)
	    host_update($pool, $host, $session, $master_ref, $hostsbyuuid);
    }

    // We used to make lots of per-VM Xen API calls, but grabbing
    // the details for all VMs in 3 calls should be much faster
    $allvms = xenapi_vm_get_all_records($session);
    $allvmmetrics = xenapi_vm_metrics_get_all_records($session);
    $allguestmetrics = xenapi_vm_guest_metrics_get_all_records($session);
    $vmsbyuuid = array();
    foreach ($allvms as $oid) {
	$vmobj_r = $oid->scalarval();
	if (($vmobj_r["is_a_template"]->scalarval()) ||
	    ($vmobj_r["is_control_domain"]->scalarval())) {
	    continue;
	}
	$vmmref  = $vmobj_r["metrics"]->scalarval();
	$vmm     = $allvmmetrics[$vmmref]->scalarval();
	$vmgmref = $vmobj_r["guest_metrics"]->scalarval();
	if (array_key_exists($vmgmref, $allguestmetrics)) {
	    $vmgm = $allguestmetrics[$vmgmref]->scalarval();
	} else {
	    unset($vmgm);
	}
	$vmuuid      = $vmobj_r["uuid"]->scalarval();
	$vmname      = $vmobj_r["name_label"]->scalarval();
	$vmpower     = $vmobj_r["power_state"]->scalarval();
	$vmmemory    = $vmm["memory_actual"]->scalarval();
	$vmstarttime = $vmm["start_time"]->scalarval();
	if (isset($vmgm)) {
	    $vmmetricsos = $vmgm["os_version"]->scalarval();
	    $strarr = explode('|', $vmmetricsos["name"]->scalarval());
	    $vmosversion = $strarr[0];
	    // Regis's distro code doesn't work for me, Colin 
	    //$vmdistro = $vmmetricsos["distro"]->scalarval();
	} else {
	    $vmosversion = "";
	}

	$vmsbyuuid[$vmuuid]["name"]          = $vmname;
	$vmsbyuuid[$vmuuid]["power_state"]   = $vmpower;
	$vmsbyuuid[$vmuuid]["memory_actual"] = $vmmemory;
	$vmsbyuuid[$vmuuid]["start_time"]    = $vmstarttime;
	$vmsbyuuid[$vmuuid]["os_version"]    = $vmosversion;
	//$vmsbyuuid[$vmuuid]["distro"]        = $vmdistro;
    }

    foreach ($pool->vms as $vm)
	vm_update($pool, $vm, $session, $vmsbyuuid);
}

function host_info_by_uuid($pool, $host, $hostsbyuuid)
{
    if ($host->address !== false) {
	// Host specified in config as HOST n.n.n.n "name" (preferred)
	foreach ($hostsbyuuid as $uuid => $info) {
	    if ($info["address"] == $host->address)
		return $info;
	}
    } else if ($host->hostname_is_ip) {
	// Host specified in config as HOST n.n.n.n
	foreach ($hostsbyuuid as $uuid => $info) {
	    if ($info["address"] == $host->hostname)
		return $info;
	}
    } else {
	// Host specified in config as HOST "name" (hopefully)
	foreach ($hostsbyuuid as $uuid => $info) {
	    if ($info["hostname"] == $host->hostname . $pool->domainname)
		return $info;
	}
    }

    return false;
}

function host_update($pool, $host, $session, $master_ref, $hostsbyuuid)
{
    if (($host_info = host_info_by_uuid($pool, $host, $hostsbyuuid)) === false)
	return;

    $host->os_version = $host_info["os_version"];
    $host->is_master = $host_info["is_master"];
    if ($host_info["enabled"]) {
	$host->memory_total = sprintf("%.1f", $host_info["memory_total"] / (1024 * 1024 * 1024));
	$host->memory_free = sprintf("%.1f", $host_info["memory_free"] / (1024 * 1024 * 1024));
	$host->state = "Running";
    }
}

function vm_name_to_uuid($vmname, $vmsbyuuid)
{
    foreach ($vmsbyuuid as $uuid => $info) {
	if ($info["name"] == $vmname)
	    return $uuid;
    }

    return false;
}

function vm_update($pool, $vm, $session, $vmsbyuuid)
{
    if (xvp_db_user_rights($vm, null) == "none") {
	$vm->vmname = false;
	return;
    }

    if (xvp_is_uuid($vm->vmname)) {
	$vm->uuid = $vm->vmname;
	$vm->vmname = $vmsbyuuid[$vm->uuid]["name"];
    } else {
	// This is potentially wrong if multiple VMs have same name label,
	// but we've documented that use of names in config file isn't
	// supported if multiple VMs in pool have the same name
	if (($uuid = vm_name_to_uuid($vm->vmname, $vmsbyuuid)) === false)
	    return;
	$vm->uuid = $uuid;
    }

    $vm->state = $vmsbyuuid[$vm->uuid]["power_state"];
    $vm->os_version = $vmsbyuuid[$vm->uuid]["os_version"];
    //$vm->distro = $vmsbyuuid[$vm->uuid]["distro"];
    if ($vm->state != "Running")
	return;

    $mem_mb = $vmsbyuuid[$vm->uuid]["memory_actual"] / (1024 * 1024);
    if ($mem_mb >= 1024)
	$vm->memory_total = sprintf("%.1f GB", $mem_mb / 1024);
    else
	$vm->memory_total = sprintf("%d MB", $mem_mb);

    $vm->uptime = vm_uptime($vmsbyuuid[$vm->uuid]["start_time"]);
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
	$vmname = xvp_xmlescape($vm->vmname);

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
