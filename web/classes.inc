<?php
/*
 * classes.inc - class definitions for Xen VNC Proxy PHP Pages
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

class Xvp_database
{
    public $dsn;
    public $username;
    public $password;
    public $dbh;

    function __construct($dsn, $username, $password)
    {
	$this->dsn      = $dsn;
	$this->username = $username;
	$this->password = $password;
    }
}

class Xvp_pool
{
    public $poolname;
    public $domainname;
    public $manager;
    public $password;
    public $session;
    public $hosts;
    public $vms;

    function __construct($poolname)
    {
	$this->poolname = $poolname;
	$this->hosts    = array();
	$this->vms      = array();
    }
}

class Xvp_host
{
    public $hostname;
    public $address;
    public $os_version;
    public $pool;
    public $is_master;
    public $hostname_is_ip;
    public $state;
    public $metrics;
    public $memory_total;
    public $memory_free;
    public $fullname;

    function __construct($pool, $hostname, $address)
    {
	$this->hostname  = $hostname;
	$this->address   = $address;
	$this->pool      = $pool;
	$this->is_master = false;
	$this->hostname_is_ip =
	    (sscanf($hostname, "%u.%u.%u.%u%c", $i1, $i2, $i3, $i4, $c) == 4);
	$this->state     = "Unknown";
	$this->fullname  = xvp_make_fullname($pool->poolname, $hostname);
	$this->memfree   = null;
    }
}

class Xvp_vm
{
    public $vmname;
    public $uuid;
    public $os_version;
    public $tools_info;
    public $pool;
    public $port;
    public $password;
    public $groupname;
    public $state;
    public $metrics;
    public $memory_total;
    public $guest_metrics;
    public $uptime;
    public $fullname;

    function __construct($pool, $vmname)
    {
	$this->vmname   = $vmname;
	$this->uuid     = null;
	$this->pool     = $pool;
	$this->state    = "Unknown";
	if (isset($pool))
	    $this->fullname = xvp_make_fullname($pool->poolname, $vmname);
	else
	    $this->fullname = xvp_make_fullname(null, $vmname);
    }
}
?>
