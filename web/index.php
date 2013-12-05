<?php
/*
 * index.php - Main file for Xen VNC Proxy PHP Pages
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

xvp_global_init();
xvp_config_init();
xvp_db_init();

echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp Pool Status</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp Pool Status" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
<link rel="apple-touch-icon" href="images/touch-icon.png" />
<link rel="stylesheet" type="text/css" href="css/styles.css" />
<link rel="stylesheet" type="text/css" href="css/jquery.contextMenu.css" />
<?php echo ios_css(); ?> 
<script type="text/javascript" src="js/jquery.js"></script>
<script type="text/javascript" src="js/jquery.contextMenu.js"></script>
<script type="text/javascript" src="js/busy.js"></script>
<script type="text/javascript" src="js/click.js"></script>
<script type="text/javascript" src="js/requester.js"></script>
</head>
<body>

<h1 id="banner">Xen Pool Status</h1>

<div id="timestamp">Update pending</div>
<div id="reloads">updates automatically - do not use refresh button</div>

<?php

main();

function pool_display($pool)
{
    if (xvp_db_user_rights(null, $pool) == "none")
	return;

    $hidehosts = (xvp_db_user_host_rights($pool) == "none");

    $displayname = htmlspecialchars($pool->poolname, ENT_QUOTES);
    if ($pool->domainname)
	$displayname .= " in domain " . htmlspecialchars(substr($pool->domainname, 1), ENT_QUOTES);

    foreach ($pool->vms as $vm) {
	if (xvp_db_user_rights($vm, $pool) == "none")
	    continue;
	echo <<<EOF
<iframe class="hidden" id="actions-$vm->fullname" name="actions-$vm->fullname" src="blank.html"></iframe>
<iframe class="hidden" id="console-$vm->fullname" name="console-$vm->fullname" src="blank.html"></iframe>

<ul id="vmmenu-$vm->fullname" class="contextMenu">
  <li class="console">
    <a href="#console">Console</a>
  </li>
  <li class="boot separator">
    <a href="#boot">Boot</a>
  </li>
  <li class="booton">
    <a href="#booton">Boot on ...</a>
  </li>
  <li class="bootrecovery">
    <a href="#bootrecovery">Boot Recovery</a>
  </li>
  <li class="shutdown separator">
    <a href="#shutdown">Shutdown</a>
  </li>
  <li class="poweroff">
    <a href="#poweroff">Force Shutdown</a>
  </li>
  <li class="reboot separator">
    <a href="#reboot">Reboot</a>
  </li>
  <li class="reset">
    <a href="#reset">Force Reboot</a>
  </li>
  <li class="suspend separator">
    <a href="#suspend">Suspend</a>
  </li>
  <li class="resume">
    <a href="#resume">Resume</a>
  </li>
  <li class="resumeon">
    <a href="#resumeon">Resume on ...</a>
  </li>
  <li class="migrate separator">
    <a href="#migrate">Migrate to ...</a>
  </li>
  <li class="snapshot separator">
    <a href="#snapshot">Snapshots</a>
  </li>
  <li class="properties separator">
    <a href="#properties">Properties</a>
  </li>
</ul>


EOF;
    }

    $poolid = xvp_make_fullname($pool->poolname, null);
    $jspoolid = addslashes($poolid);

    echo "\n<div class=\"pool\" id=\"pool-$poolid\">\n\n";

    if ($hidehosts) {

	echo <<<EOF
<h2 class="center"><img class="toggle" id="toggle-$poolid" alt="" title="" src="images/minus.png" onclick="poolToggle('$jspoolid');" />Pool $displayname</h2>

<div class="vmscenter">

EOF;
    } else {

	echo <<<EOF
<h2><img class="toggle" id="toggle-$poolid" alt="" title="" src="images/minus.png" onclick="poolToggle('$jspoolid');" />Pool $displayname</h2>

<div class="hosts">

<h3 class="hosts">Server Hosts</h3>

<table class="hosts">

  <tr>
    <td class="group">
      <img class="toggle" id="toggle-$poolid-hosts" alt="" title="" src="images/minus.png" onclick="hostsToggle('$jspoolid');" />&nbsp;
    </td>
  </tr>

EOF;
    foreach ($pool->hosts as $host)
	host_display($host);
    echo <<<EOF
</table>

</div>

<div class="vmsright">


EOF;
    }

    echo <<<EOF
<h3>Virtual Machines</h3>

<table class="vms">

EOF;
    $groupname = "--";
    foreach ($pool->vms as $vm)
	if (!xvp_config_host_by_name($pool, $vm->vmname))
	    vm_display($vm, $groupname);
    echo <<<EOF
</table>

</div>

</div>


EOF;
}

function host_display($host)
{
    echo <<<EOF
  <tr>
    <td class="host">
      <table>
        <tr class="host">
          <td class="oslogo"><img id="osicon-$host->fullname" alt="" title="" class="oslogo" src="images/xen.png" /></td>
          <th class="host">$host->hostname</th>
          <td class="unknown" id="state-$host->fullname">&nbsp;</td>
          <td class="memfree" id="memfree-$host->fullname">&nbsp;</td>
        </tr>
      </table>
    </td>
  </tr>

EOF;
}

function vm_display($vm, &$groupname)
{
    if (xvp_db_user_rights($vm, null) == "none")
	return;

    $poolname   = $vm->pool->poolname;
    $vmname     = $vm->vmname;
    $label      = xvp_is_uuid($vmname) ? "&nbsp;" : $vmname; 
    $form       = "form-" . $vm->fullname;
    $busy       = "busy-" . $vm->fullname;
    $button     = "button-" . $vm->fullname;
    $osicon     = "osicon-" . $vm->fullname;
    $jsfullname = addslashes($vm->fullname);
    $groupclass = xvp_make_fullname($poolname, $groupname);
    if ($vm->groupname != $groupname) {
	$groupname = $vm->groupname;
	$displayname = htmlspecialchars($groupname, ENT_QUOTES);
	$groupclass = xvp_make_fullname($poolname, $groupname);
	$jsgroupclass = addslashes($groupclass);
	echo <<<EOF
  <tr>
    <td class="group">
      <img class="toggle" id="toggle-$groupclass" alt="" title="" src="images/minus.png" onclick="groupToggle('$jsgroupclass');" />$displayname&nbsp;
    </td>
  </tr>

EOF;
    }

    // Note target below is not used - overriden from click.js on click,
    // as is setting for action

    echo <<<EOF
  <tr class="group-$groupclass">
    <td class="vm">
      <form class="vm" id="$form" method="post" action="blank.html" target="_blank">
        <input type="hidden" name="poolname" value="$poolname" />
        <input type="hidden" name="vmname" value="$vmname" />
        <input type="hidden" name="action" class="action" value="" />
        <input type="hidden" name="busy" id="$busy" class="busy" value="false" />
        <table>
          <tr class="vm">
            <td class="oslogo"><img class="oslogo" id="$osicon" alt="" title="" src="images/blank.png" /></td>
            <th class="vm" id="label-$vm->fullname">$label</th>
	    <td class="button"><img class="button" id="$button" alt="" title="" src="images/blank.png" onclick="vmClick('left', '$jsfullname');" /></td>
	    <td class="unknown" id="state-$vm->fullname">&nbsp;</td>
	    <td class="memtotal" id="memtotal-$vm->fullname">&nbsp;</td>
          </tr>
        </table>
      </form>
    </td>
  </tr>

EOF;
}

function main()
{
    global $xvp_pools;

    foreach ($xvp_pools as $pool) {
	pool_display($pool);
    }
}

?>

<div id="logo">
  <h1>
    <a class="logo" href="http://www.xvpsource.org/">xvp</a><span class="tm">&trade;</span>
  </h1>
</div>

</body></html>
