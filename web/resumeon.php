<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * resumeon.php - VM resumer with host selection for Xen VNC Proxy PHP Pages
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

function setup()
{
    global $poolname, $vmname, $jsvmname;
    global $stage, $hostname, $htvmname, $home, $hosts;
    global $session, $ref, $href;

    xvp_global_init();
    xvp_config_init();
    xvp_db_init();

    $hosts = $ref = $href = false;

    if (!($pool = xvp_config_pool_by_name($poolname)) ||
	!($vm = xvp_config_vm_by_name($pool, $vmname)))
	return;

    if (($session = xenapi_login($pool, $master_ref)) === false)
	return;

    if (xvp_is_uuid($vmname)) {
	if (($ref = xenapi_vm_by_uuid($session, $vmname)) === false ||
	    ($label = xenapi_vm_get_name_label($session, $ref)) === false)
	    return;
	$vm->uuid = $vm->vmname;
	$htvmname = htmlspecialchars($label);
	if ($stage == 2)
	    $jsvmname = addslashes($label);
    } else if (($ref = xenapi_vm_by_name($session, $vmname)) === false) {
	    return;
    } else {
	$htvmname = htmlspecialchars($vmname);
    }

    if (!xvp_db_user_may_perform($vm, null, "resumeon"))
	return;

    switch ($stage) {
    case 1:

	if (($aff = xenapi_vm_get_affinity($session, $ref)) !== false)
	    $home = xenapi_host_get_name_label($session, $aff);
	else
	    $home = "";

	if (($ph = xenapi_vm_get_possible_hosts($session, $ref)) === false)
	    return;

	$hosts = array();
	foreach ($ph as $href) {
	    $name = xenapi_host_get_name_label($session, $href);
	    if ($name !== false)
		$hosts[$href] = $name;
	}

	asort($hosts);
	break;

    case 2:

	$href = xenapi_host_by_name($session, $hostname);
	break;
    }
}

function no_hosts()
{
    global $htvmname;

    echo <<<EOF
    <p class="action">No suitable hosts on which to resume $htvmname</p>
    <div class="action ok">
      <button type="button" onclick="window.close();">OK</button>
    </div>

EOF;
}

function choose_host()
{
    global $poolname, $vmname, $fullname, $home, $hosts;

    $size = count($hosts);

echo <<<EOF

  <form class="action" method="post" target="actions-$fullname">
    <p>Select server:</p>
    <input type="hidden" name="poolname" value="$poolname" />
    <input type="hidden" name="vmname" value="$vmname" />
    <select name="hostname" size="$size">

EOF;

    foreach ($hosts as $name) {
	$comment = ("$name" == "$home") ? " (home server)" : "";

	echo "      <option value=\"$name\">$name$comment</option>\n";
    }

echo <<<EOF
    </select>
    <div class="action">
      <button type="button" onclick="submit_form();">Resume</button>
      <button type="button" onclick="window.close();">Cancel</button>
    </div>
  </form>


EOF;
}

function resume_vm()
{
    global $session, $ref, $href, $resumed;

    if ($ref === false || $href == false)
	return;

    $prio = xenapi_vm_get_restart_priority($session, $ref);
    $always = xenapi_vm_get_ha_always_run($session, $ref);

    if (xenapi_vm_resume_on($session, $ref, $href)) {
	$resumed = "true";
	if (strlen($prio) > 0 && $always === false)
	    // looks like HA disabled for VM on shutdown, so re-enable
	    xenapi_vm_set_ha_always_run($session, $ref, true);
    }
}

function main()
{
    global $stage;

    setup();

    if ($stage == 2)
	resume_vm();
}

/*
 * We get called twice: stage 1 in separate window with GET parameters
 * so the user can choose the host to resume on, and stage 2 (invoked by
 * stage 1) back in the main window's hidden iframe with POST parameters
 * to do the actual resume, so that the latter can pop up a JavaScript
 * success or failure box, like resume.php does.
 */
if (isset($_POST['hostname'])) {
    $stage    = 2;
    $poolname = stripslashes($_POST['poolname']);
    $hostname = stripslashes($_POST['hostname']);
    $jsvmname = $_POST['vmname'];
    $vmname   = stripslashes($jsvmname);
} else {
    $stage    = 1;
    $poolname = stripslashes(urldecode($_GET['poolname']));
    $vmname   = stripslashes(urldecode($_GET['vmname']));
}
$fullname = xvp_make_fullname($poolname, $vmname);
$resumed = "false";
$ioscss = ios_css();
$viewport = ios_viewport();

main();

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp VM Resume - $htvmname</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp VM Resume - $htvmname" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
$viewport
<link rel="stylesheet" type="text/css" href="css/styles.css" />
$ioscss
<script type="text/javascript" src="js/jquery.js"></script>
<script type="text/javascript" src="js/jquery.contextMenu.js"></script>
<script type="text/javascript" src="js/busy.js"></script>
</head>

EOF;

if ($stage == 1) {
    echo <<<EOF
<script type="text/javascript">

function submit_form()
{
    var f = document.forms[0];

    if (f.hostname.selectedIndex < 0) {
	alert("Please select a host");
	return;
    }

    setBusy(f.target.replace("actions-", ""), "resume", true);

    f.submit();
    window.close();
}

</script>
<body class="popup">
<div id="main">

  <h1 class="action">Resume $htvmname</h1>

EOF;
    if ($hosts === false || count($hosts) == 0)
	no_hosts();
    else
	choose_host();

} else {
    echo <<<EOF
<body onload="window.parent.actionCallback('$fullname', '$jsvmname', 'resume', '$resumed');">

EOF;

}

echo <<<EOF
</div>
</body>
</html>

EOF;

?>
