/*
 * click.js - Button click handling for Xen VNC Proxy PHP Pages
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

$(document).ready( function() {

    $('tr.vm > td.button > img.button').each(function(i) {
        var menuid = $(this).attr('id').replace("button-", "vmmenu-");
        $(this).contextMenu({
            menu: menuid
        },
	    function(action, el, pos) {
	        var fullname = $(el).attr('id').replace("button-", "");
	        vmClick(action, fullname);
	    });
	    $('#' + menuid).disableContextMenuItems();
    });

    updateRequest();
});

function vmClick(action, fullname)
{
    var form   = document.forms['form-' + fullname];
    var ask    = false;
    var mobile = tapOpensContextMenu();

    if (mobile && action == "left")
	return false;

    if (action == "left") {
	var button = document.getElementById('button-' + fullname);

	// .../images/action.png -> action
	action = button.getAttribute("src").replace(/^.*\/(\w+).png$/, "$1");
    }

    var target = "actions-" + fullname;
    var page = action;
    var say = action;
    var progress = true;
    var conftext;

    switch (action) {
    case 'blank':
    case 'busy':
    case 'info':
	return;
    case 'console':
	if (mobile)
	    return false;
	progress = false;
	target = "console-" + fullname;
	break;
    case 'boot':
    case 'resume':
	ask = mobile;
	break;
    case 'booton':
	say = "boot";
	progress = false;
	page = 'opener';
	break;
    case 'bootrecovery':
	ask = true;
	say = "recovery mode boot";
	break;
    case 'resumeon':
	say = "resume";
	progress = false;
	page = 'opener';
	break;
    case 'migrate':
    case 'snapshot':
    case 'properties':
	progress = false;
	page = 'opener';
	break;
    case 'shutdown':
    case 'reboot':
    case 'suspend':
	ask = true;
	break;
    case 'poweroff':
	ask = true;
	say = "force shutdown";
	break;
    case 'reset':
	ask = true;
	say = "force reboot";
	break;
    default:
	alert('Unexpected action: ' + action);
	return;
    }

    if (ask) {
	var label = document.getElementById("label-" + fullname);
	var labelname = label.lastChild.nodeValue;

	if (action == "bootrecovery")
	    // Change matching text in booton.php if change here
	    conftext = "To boot " + labelname + " in recovery mode, you must have either a bootable CD/DVD attached to the VM, or a suitable PXE network boot environment set up.  Do you want to continue?";
	else
	    conftext = "Are you sure you want to " + say + " " + labelname + " ?";
	if (!confirm(conftext))
	    return;
    }

    if (progress)
	setBusy(fullname, say, false);

    form.setAttribute("action", page + ".php");
    form.setAttribute("target", target);
    $('input', form).filter('.action').attr('value', action);
    form.submit();

    return false;
}

function poolToggle(poolid)
{
    var image = document.images['toggle-' + poolid];
    var src = image.src;
    var group;
    var oldname, newname, visible;

    if (src.indexOf("plus") == -1) {
	oldname = "minus";
	newname = "plus";
	visible = false;
    } else {
	oldname = "plus";
	newname = "minus";
	visible = true;
    }

    image.src = src.replace(oldname, newname);

    // collapse/expand hosts
    $('#pool-' + poolid + ' table.hosts td.host').toggle(visible);

    // collapse/expand VM groups ...
    $('#pool-' + poolid + ' table img.toggle').each(function(i){
	    var toggleid = $(this).attr('id');
	    var image = document.images[toggleid];
	    var src = image.src;
	    image.src = src.replace(oldname, newname);
	    $('tr.' + toggleid.replace('toggle','group')).toggle(visible);
	});
 
    // ... or collapse/expand ungrouped VMs
    $('tr.group-' + poolid + '-').toggle(visible);
}

function hostsToggle(poolid)
{
    var image = document.images['toggle-' + poolid + '-hosts'];
    var src = image.src;
    var group;
    var oldname, newname, visible;

    if (src.indexOf("plus") == -1) {
	oldname = "minus";
	newname = "plus";
	visible = false;
    } else {
	oldname = "plus";
	newname = "minus";
	visible = true;
    }

    image.src = src.replace(oldname, newname);

    $('#pool-' + poolid + ' table.hosts td.host').toggle(visible);
}

function groupToggle(groupclass)
{
    var image = document.images['toggle-' + groupclass];
    var src = image.src;
    var oldname, newname, visible;

    if (src.indexOf("plus") == -1) {
	oldname = "minus";
	newname = "plus";
	visible = false;
    } else {
	oldname = "plus";
	newname = "minus";
	visible = true;
    }

    image.src = src.replace(oldname, newname);
    $('tr.group-' + groupclass).toggle(visible);
}
