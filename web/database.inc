<?php
/*
 * database.inc - user authorisation database for Xen VNC Proxy PHP Pages
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

function xvp_db_bad_file($filename, $linenum)
{
    xvp_log(XVP_LOG_FATAL, "%s: Syntax error at line %d",
            $filename, $linenum);
}

function xvp_db_open_file($filename)
{
    global $xvp_db;

    if (($stream = @fopen($filename, "r")) === false)
        xvp_log(XVP_LOG_FATAL, "Unable to open %s", $filename);

    try {
	$dbh = new PDO("sqlite::memory:", false, false,
		       array(PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION));
	$create = sprintf("CREATE TEMPORARY TABLE %s " .
			  "(username VARCHAR(%d), poolname VARCHAR(%d), " .
			  "groupname VARCHAR(%d), vmname VARCHAR(%d), " .
			  "rights VARCHAR(%d));",
			  XVP_DB_TABLE_NAME, XVP_DB_COLUMN_SIZE,
			  XVP_DB_COLUMN_SIZE, XVP_DB_COLUMN_SIZE,
			  XVP_DB_COLUMN_SIZE, XVP_DB_COLUMN_SIZE);
	$dbh->exec($create);

	$pattern = "/^([^:]+):([^:]+):([^:]+):([^:]+):([^:]+)$/";
	$linenum = 0;
	while (($line = fgets($stream, XVP_DB_LINEBUF_SIZE)) !== false) {
	    $linenum++;

	    if (($pos = strpos($line, "#")) !== false ||
		($pos = strpos($line, "\r")) !== false ||
		($pos = strpos($line, "\n")) !== false) {
		if ($pos == 0)
		    continue;
		$line = rtrim(substr($line, 0, $pos));
	    } else {
		$line = rtrim($line);
	    }

	    if (strlen($line) == 0)
		continue;

	    if (preg_match($pattern, $line, $matches) != 1)
		xvp_db_bad_file($filename, $linenum);

	    array_shift($matches);

	    foreach ($matches as &$match) {
		if (strlen($match = trim($match)) > XVP_DB_COLUMN_SIZE)
		    xvp_db_bad_file($filename, $linenum);
		$match = $dbh->quote($match);
	    }

	    $insert = sprintf("INSERT INTO %s values(%s,%s,%s,%s,%s);",
			      XVP_DB_TABLE_NAME, $matches[0], $matches[1],
			      $matches[2], $matches[3], $matches[4]);
	    $dbh->exec($insert);
	}
	
    } catch (PDOException $e) {
	xvp_log(XVP_LOG_FATAL, "%s: PDO error: %s",
		$filename, $e->getMessage());
    }

    fclose($stream);
    $xvp_db->dbh = $dbh;
}

function xvp_db_init()
{
    global $xvp_db;

    if (!$xvp_db)
	return;

    if (!isset($_SERVER['REMOTE_USER']))
	xvp_db_forbidden("You need to login to view this page");

    if (strpos($xvp_db->dsn, "xvp:") === 0) {
	xvp_db_open_file(substr($xvp_db->dsn, 4));
    } else {
	xvp_password_decrypt($xvp_db->password, $password, XVP_PASSWORD_XEN);
	
	try {
	    $xvp_db->dbh = new PDO($xvp_db->dsn, $xvp_db->username, $password,
				   array(PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION));
	} catch (PDOException $e) {
	    xvp_log(XVP_LOG_FATAL, "%s: PDO error: %s",
		    $xvp_db->dsn, $e->getMessage());
	}
    }

    if (xvp_db_user_rights(null, null) == "none")
	xvp_db_forbidden("You do not have permission to view this page");
}

function xvp_db_get_rights_map()
{
    global $xvp_rights_map;

    if ($xvp_rights_map !== null)
	return $xvp_rights_map;

    $xvp_rights_map = array();
    
    if (($stream = @fopen(XVP_RIGHTS_CUSTOM_FILENAME, "r")) === false &&
	($stream = @fopen(XVP_RIGHTS_DEFAULT_FILENAME, "r")) === false)
	return $xvp_rights_map;

    while (($line = fgets($stream, XVP_CONFIG_LINEBUF_SIZE)) !== false) {

	if ((($pos = strpos($line, "#")) !== false) ||
	    (($pos = strpos($line, "\r")) !== false) ||
	    (($pos = strpos($line, "\n")) !== false)) {
	    if ($pos === 0)
		continue;
	    $line = substr($line, 0, $pos);
	}

	$words = explode(':', $line);
	if (count($words) != 2)
	    continue;
	if (($operation = strtolower(trim($words[0]))) == "")
	    continue;
	$rights = strtolower(trim($words[1]));

	switch ($rights) {
	case "none":
	case "list":
	case "read":
	case "write":
	case "control":
	case "all":
	    $xvp_rights_map[$operation] = $rights;
	    break;
	}
    }

    fclose($stream);
    return $xvp_rights_map;
}

function xvp_db_user_rights($vm, $pool)
{
    global $xvp_db;

    if (!$xvp_db)
	return "all";

    $dbh      = $xvp_db->dbh;
    $username = $dbh->quote($_SERVER['REMOTE_USER']);
    $wild     = $dbh->quote('*');
    $minus    = $dbh->quote('-');

    if (isset($vm)) {

	$poolname  = $dbh->quote($vm->pool->poolname);
	$groupname = $dbh->quote($vm->groupname);
	$vmname    = $dbh->quote($vm->uuid ? $vm->uuid : $vm->vmname);

	$query = "SELECT rights FROM " . XVP_DB_TABLE_NAME . " WHERE " .
	    "(username = $username OR username = $wild) AND " .
	    "(poolname = $poolname OR poolname = $wild) AND " .
	    "(groupname = $groupname OR groupname = $wild) AND " .
	    "(vmname = $vmname OR vmname = $wild);";
    } else if (isset($pool)) {

	$poolname  = $dbh->quote($pool->poolname);	

	$query = "SELECT rights FROM " . XVP_DB_TABLE_NAME . " WHERE " .
	    "(username = $username OR username = $wild) AND " .
	    "(poolname = $poolname OR poolname = $wild) AND " .
	    "(groupname <> $minus) AND (vmname <> $minus);";

    } else {

	$query = "SELECT rights FROM " . XVP_DB_TABLE_NAME . " WHERE " .
	    "(username = $username OR username = $wild) AND " .
	    "groupname <> $minus AND vmname <> $minus;";
    }

    try {
	$sth = $dbh->query($query);
	$entries = ($sth->fetchAll(PDO::FETCH_COLUMN | PDO::FETCH_UNIQUE, 0));
    } catch (PDOException $e) {
	xvp_log(XVP_LOG_FATAL, "%s: PDO error: %s",
		$xvp_db->dsn, $e->getMessage());
    }

    $rights = "none";
    foreach ($entries as $entry) {
	if ($entry == "all")
	    return $entry;
	else if ($entry == "control")
	    $rights = $entry;
	else if ($entry == "write" && $rights != "all" && $rights != "control")
	    $rights = $entry;
	else if ($entry == "read" && ($rights == "list" || $rights == "none"))
	    $rights = $entry;
	else if ($entry == "list" && $rights == "none")
	    $rights = $entry;
    }

    return $rights;
}

function xvp_db_user_has_rights($vm, $pool, $wanted)
{
    global $xvp_db;

    if ($wanted == "disabled")
	return false;

    if (!$xvp_db || $wanted == "none")
	return true;

    if (($rights = xvp_db_user_rights($vm, $pool)) == "none")
	return false;

    switch ($wanted) {
    case "all":
	return ($rights == "all");
    case "control":
	return ($rights == "all" || $rights == "control");
    case "write":
	return ($rights != "read" && $rights != "list");
    case "read":
	return ($rights != "list");
    case "list":
	return true;
    }

    // not reached
    return false; 
}

function xvp_db_user_may_perform($vm, $pool, $operation)
{

    $map = xvp_db_get_rights_map();
    if (array_key_exists($operation, $map))
	$rights = $map[$operation];
    else
	$rights = "disabled";

    return xvp_db_user_has_rights($vm, $pool, $rights);
}

function xvp_db_user_host_rights($pool)
{
    global $xvp_db;

    if (!$xvp_db)
	return "all";

    $dbh      = $xvp_db->dbh;
    $username = $dbh->quote($_SERVER['REMOTE_USER']);
    $wild     = $dbh->quote('*');
    $minus    = $dbh->quote('-');

    if (isset($pool)) {

	$poolname  = $dbh->quote($pool->poolname);	

	$query = "SELECT rights FROM " . XVP_DB_TABLE_NAME . " WHERE " .
	    "(username = $username OR username = $wild) AND " .
	    "(poolname = $poolname OR poolname = $wild) AND " .
	    "groupname = $minus AND vmname = $minus;";
    } else {

	$query = "SELECT rights FROM " . XVP_DB_TABLE_NAME . " WHERE " .
	    "(username = $username OR username = $wild) AND " .
	    "groupname = $minus AND vmname = $minus;";
    }

    try {
	$sth = $dbh->query($query);
	$entries = ($sth->fetchAll(PDO::FETCH_COLUMN | PDO::FETCH_UNIQUE, 0));
    } catch (PDOException $e) {
	xvp_log(XVP_LOG_FATAL, "%s: PDO error: %s",
		$xvp_db->dsn, $e->getMessage());
    }

    // For compatibility with < 1.5.0, no entries => "list"
    if (count($entries) == 0)
	return "list";

    $rights = "none";
    foreach ($entries as $entry) {
	if ($entry == "all")
	    return $entry;
	else if ($entry == "control")
	    $rights = $entry;
	else if ($entry == "write" && $rights != "all" && $rights != "control")
	    $rights = $entry;
	else if ($entry == "read" && ($rights == "list" || $rights == "none"))
	    $rights = $entry;
	else if ($entry == "list" && $rights == "none")
	    $rights = $entry;
    }

    return $rights;
}

function xvp_db_forbidden($message)
{
    header("HTTP/1.0 403 Forbidden");
    echo <<<EOF
<html>
<head>
<title>Access to page forbidden</title>
</head>
<body>
<p style="margin-top: 100px; text-align: center">$message</p>
</body>
</html>

EOF;
    exit();

}

?>
