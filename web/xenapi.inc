<?php
/*
 * xenapi.inc - interface to Xen API for Xen VNC Proxy PHP Pages
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

function xenapi_login($pool, &$master_ref)
{
    $username = new xmlrpcval($pool->manager);
    xvp_password_decrypt($pool->password, $password, XVP_PASSWORD_XEN);
    $password = new xmlrpcval($password);
    $version = new xmlrpcval("1.5");

    foreach ($pool->hosts as $host) {

	if ($host->address !== false) {
	    $url = "https://" . $host->address;
	} else {
	    $url = "https://" . $host->hostname;
	    if (!$host->hostname_is_ip)
		$url .= $pool->domainname;
	}

	while (true) {

	    $handle = xenapi_connect($url, $version,
				     $username, $password, $master_ref);
	    if ($handle === false)
		return false;
	    else if ($handle == null)
		break;
	    else if ($handle === true)
		continue;

	    return $handle;
	}
    }

    return false;
}

/*
 * Attempt to connect to a host, returning:
 *
 *   null   - connection failed
 *   false  - rejected
 *   true   - redirected, use updated $url
 *   handle - accepted
 */
function xenapi_connect(&$url, $version, $username, $password, &$master_ref)
{
    $msg = new xmlrpcmsg("session.login_with_password",
			 array($username, $password, $version));
    $conn = new xmlrpc_client($url);
    $conn->setSSLVerifyPeer(FALSE);

    $res = $conn->send($msg);
    if ($res->faultcode())
	return null;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    // Looks good, but may not be master host, so ...

    $sessid = $val->structmem("Value");
    $msg = new xmlrpcmsg("session.get_this_host", array($sessid, $sessid));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success") {
	$err = $val->structmem("ErrorDescription");
	if ($err->arraySize() == 2 &&
	    $err->arrayMem(0)->scalarval() == "HOST_IS_SLAVE") {
	    $url = "https://" . $err->arrayMem(1)->scalarval();
	    return true;
	}
	return false;
    }

    $master_ref = $val->structmem("Value")->scalarval();
    return array($conn, $sessid->scalarval());
}

function xenapi_host_by_name($session, $name)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $name = new xmlrpcval($name);

    $msg = new xmlrpcmsg("host.get_by_name_label", array($sess, $name));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $hostset = $value->scalarval();

    if (count($hostset) != 1)
	return false;

    return $hostset[0]->scalarval();
}

function xenapi_host_get_name_label($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $host = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("host.get_name_label", array($sess, $host));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_host_get_enabled($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $host = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("host.get_enabled", array($sess, $host));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_host_compute_free_memory($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $host = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("host.compute_free_memory", array($sess, $host));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_host_get_software_version($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $host = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("host.get_software_version", array($sess, $host));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $info = $value->scalarval();

    if (isset($info["product_brand"]) && isset($info["product_version"])) {
	// XenServer and older XCP (newer XCP missing this)
	$product_brand = $info["product_brand"]->scalarval();
	$product_version = $info["product_version"]->scalarval();
    } else {
	// newer XCP (newer XenServer returns XCP values if use this)
	$product_brand = $info["platform_name"]->scalarval();
	$product_version = $info["platform_version"]->scalarval();
    }
    $build_number = $info["build_number"]->scalarval();

    $version = sprintf("%s %s build %s",
		       $product_brand, $product_version, $build_number);

    return $version;
}

function xenapi_host_get_metrics($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $host = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("host.get_metrics", array($sess, $host));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_host_get_memory_total($session, $metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $m = new xmlrpcval($metrics);

    $msg = new xmlrpcmsg("host_metrics.get_memory_total", array($sess, $m));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return 0;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return 0;

    return $val->structmem("Value")->scalarval();
}

function xenapi_host_get_all_records($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("host.get_all_records", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
       return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
       return false;

    return $val->structmem("Value")->scalarval();
}

function xenapi_host_metrics_get_all_records($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("host_metrics.get_all_records", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
       return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
       return false;

    return $val->structmem("Value")->scalarval();
}


function xenapi_vm_by_name($session, $name)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $name = new xmlrpcval($name);

    $msg = new xmlrpcmsg("VM.get_by_name_label", array($sess, $name));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $vmset = $value->scalarval();

    if (count($vmset) != 1)
	return false;

    return $vmset[0]->scalarval();
}

function xenapi_vm_by_uuid($session, $uuid)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $uuid = new xmlrpcval($uuid);

    $msg = new xmlrpcmsg("VM.get_by_uuid", array($sess, $uuid));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_name_label($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_name_label", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_name_description($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_name_description", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_power_state($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_power_state", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return "Unknown";

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_memory_static_max($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_memory_static_max", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_vcpus_max($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_VCPUs_max", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_start($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $start_paused = new xmlrpcval(false, "boolean");
    $force = new xmlrpcval(false, "boolean");

    $msg = new xmlrpcmsg("VM.start", array($sess, $vm, $start_paused, $force));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_start_on($session, $ref, $href)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $host = new xmlrpcval($href);
    $start_paused = new xmlrpcval(false, "boolean");
    $force = new xmlrpcval(false, "boolean");

    $msg = new xmlrpcmsg("VM.start_on", array($sess, $vm, $host, $start_paused, $force));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_restart($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.clean_reboot", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_hard_reboot($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.hard_reboot", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_shutdown($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.clean_shutdown", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_hard_shutdown($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.hard_shutdown", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_suspend($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.suspend", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "Unknown";

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_resume($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $start_paused = new xmlrpcval(false, "boolean");
    $force = new xmlrpcval(false, "boolean");

    $msg = new xmlrpcmsg("VM.resume", array($sess, $vm, $start_paused, $force));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_resume_on($session, $ref, $href)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $host = new xmlrpcval($href);
    $start_paused = new xmlrpcval(false, "boolean");
    $force = new xmlrpcval(false, "boolean");

    $msg = new xmlrpcmsg("VM.resume_on", array($sess, $vm, $host, $start_paused, $force));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_migrate($session, $ref, $href)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $host = new xmlrpcval($href);
    $options = new xmlrpcval(array("live" => new xmlrpcval("true"), "force" => new xmlrpcval("true")), "struct");
    $msg = new xmlrpcmsg("VM.pool_migrate", array($sess, $vm, $host, $options));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_get_restart_priority($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_ha_restart_priority", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return "";

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return "";

    $value = $val->structmem("Value");
    // this will be "1", "best-effort" or blank
    return $value->scalarval();
}

function xenapi_vm_get_possible_hosts($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_possible_hosts", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $hosts = $value->scalarval();

    foreach ($hosts as $k => $v)
	$hosts[$k] = $v->scalarval();
    return $hosts;
}

function xenapi_vm_get_ha_always_run($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_ha_always_run", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_set_ha_always_run($session, $ref, $always)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $flag = new xmlrpcval($always, "boolean");
    $msg = new xmlrpcmsg("VM.set_ha_always_run", array($sess, $vm, $flag));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    return true;
}

function xenapi_vm_get_metrics($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_metrics", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_resident_on($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_resident_on", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_affinity($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_affinity", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_memory_actual($session, $metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $m = new xmlrpcval($metrics);

    $msg = new xmlrpcmsg("VM_metrics.get_memory_actual", array($sess, $m));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return 0;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return 0;

    return $val->structmem("Value")->scalarval();
}

function xenapi_vm_get_start_time($session, $metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $m = new xmlrpcval($metrics);

    $msg = new xmlrpcmsg("VM_metrics.get_start_time", array($sess, $m));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return 0;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return 0;

    return $val->structmem("Value")->scalarval();
}

// Contributed by Regis Wilson, 2 Nov 2011
function xenapi_vm_get_all_records($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("VM.get_all_records", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
       return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
       return false;

    return $val->structmem("Value")->scalarval();
}

// Added by Colin Dean to complement above, 21 Dec 2011
function xenapi_vm_metrics_get_all_records($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("VM_metrics.get_all_records", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
       return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
       return false;

    return $val->structmem("Value")->scalarval();
}

// Added by Colin Dean to complement above, 22 Dec 2011
function xenapi_vm_guest_metrics_get_all_records($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("VM_guest_metrics.get_all_records", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
       return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
       return false;

    return $val->structmem("Value")->scalarval();
}

function xenapi_vm_get_guest_metrics($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_guest_metrics", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    return $val->structmem("Value")->scalarval();
}

function xenapi_vm_get_vbds($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.get_VBDs", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $vbds = $val->structmem("Value")->scalarval();
    foreach ($vbds as $k => $v)
	$vbds[$k] = $v->scalarval();
    return $vbds;
}

function xenapi_vm_get_os_version($session, $guest_metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $gm = new xmlrpcval($guest_metrics);

    $msg = new xmlrpcmsg("VM_guest_metrics.get_os_version", array($sess, $gm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    if (count($value) == 0 || !$value->structmemexists("name"))
	return false;

    $os = explode("|", $value->structmem("name")->scalarval());

    // Some Windows VMs have invalid UTF-8 top-bit set characters in OS string
    return preg_replace('/[\x80-\xff]/', "", $os[0]);
}

function xenapi_vm_get_pv_drivers_version($session, $guest_metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $gm = new xmlrpcval($guest_metrics);

    $msg = new xmlrpcmsg("VM_guest_metrics.get_PV_drivers_version",
			 array($sess, $gm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    if (count($value) == 0 || !$value->structmemexists("major"))
	return false;

    $major = $value->structmem("major")->scalarval();
    $minor = $value->structmem("minor")->scalarval();
    $micro = $value->structmem("micro")->scalarval();
    $build = $value->structmem("build")->scalarval();
    return "$major.$minor.$micro build $build";
}

function xenapi_vm_get_pv_drivers_up_to_date($session, $guest_metrics)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $gm = new xmlrpcval($guest_metrics);

    $msg = new xmlrpcmsg("VM_guest_metrics.get_PV_drivers_up_to_date",
			 array($sess, $gm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_get_hvm_boot_policy($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_HVM_boot_policy", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}


function xenapi_vm_set_hvm_boot_policy($session, $ref, $policy)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $setting = new xmlrpcval($policy);
    $msg = new xmlrpcmsg("VM.set_HVM_boot_policy", array($sess, $vm, $setting));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    return true;
}

function xenapi_vm_get_hvm_boot_params($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    
    $msg = new xmlrpcmsg("VM.get_HVM_boot_params", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $params = array();
    foreach ($value->scalarval() as $key => $value)
	$params[$key] = $value->scalarval();

    return $params;
}

function xenapi_vm_set_hvm_boot_params($session, $ref, $params)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $msgparams = array();
    foreach ($params as $key => $value)
	$msgparams[$key] = new xmlrpcval($value);
    $setting = new xmlrpcval($msgparams, "struct");
    $msg = new xmlrpcmsg("VM.set_HVM_boot_params", array($sess, $vm, $setting));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    return true;
}

function xenapi_vm_get_snapshots($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $msg = new xmlrpcmsg("VM.get_snapshots", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $snap = $val->structmem("Value")->scalarval();
    foreach ($snap as $k => $v)
	$snap[$k] = $v->scalarval();
    return $snap;
}

function xenapi_vm_get_is_a_snapshot($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $msg = new xmlrpcmsg("VM.get_is_a_snapshot", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
        return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
        return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_snapshot($session, $ref, $name)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);
    $name = new xmlrpcval($name);
    $msg = new xmlrpcmsg("VM.snapshot", array($sess, $vm, $name));
    $res = $conn->send($msg);

    if ($res->faultcode())
        return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
        return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vm_revert($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.revert", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vm_destroy($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vm = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VM.destroy", array($sess, $vm));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_sr_get_all($session)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);

    $msg = new xmlrpcmsg("SR.get_all", array($sess));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    $srs = $value->scalarval();

    foreach ($srs as $k => $v)
	$srs[$k] = $v->scalarval();
    return $srs;
}

function xenapi_sr_get_record($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $sr = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("SR.get_record", array($sess, $sr));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $rec = $val->structmem("Value")->scalarval();
    foreach ($rec as $k => $v)
	$rec[$k] = $v->scalarval();
    return $rec;
}

function xenapi_vdi_get_record($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vdi = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VDI.get_record", array($sess, $vdi));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $rec = $val->structmem("Value")->scalarval();
    foreach ($rec as $k => $v)
	if ($v)
	    $rec[$k] = $v->scalarval();
    return $rec;
}

function xenapi_vdi_by_uuid($session, $uuid)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $uuid = new xmlrpcval($uuid);

    $msg = new xmlrpcmsg("VDI.get_by_uuid", array($sess, $uuid));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vdi_get_name_label($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vdi = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VDI.get_name_label", array($sess, $vdi));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $value = $val->structmem("Value");
    return $value->scalarval();
}

function xenapi_vdi_destroy($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vdi = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VDI.destroy", array($sess, $vdi));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vbd_get_record($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vbd = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VBD.get_record", array($sess, $vbd));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $rec = $val->structmem("Value")->scalarval();
    foreach ($rec as $k => $v)
	$rec[$k] = $v->scalarval();
    return $rec;
}

function xenapi_vbd_insert($session, $ref, $iref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vbd = new xmlrpcval($ref);
    $vdi = new xmlrpcval($iref);

    $msg = new xmlrpcmsg("VBD.insert", array($sess, $vbd, $vdi));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_vbd_eject($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $vbd = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("VBD.eject", array($sess, $vbd));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");

    return ($status->scalarval() == "Success");
}

function xenapi_pbd_get_record($session, $ref)
{
    $conn = $session[0];
    $sess = new xmlrpcval($session[1]);
    $pbd = new xmlrpcval($ref);

    $msg = new xmlrpcmsg("PBD.get_record", array($sess, $pbd));
    $res = $conn->send($msg);

    if ($res->faultcode())
	return false;

    $val = $res->value();
    $status = $val->structmem("Status");
    if ($status->scalarval() != "Success")
	return false;

    $rec = $val->structmem("Value")->scalarval();
    foreach ($rec as $k => $v)
	$rec[$k] = $v->scalarval();
    return $rec;
}

?>
