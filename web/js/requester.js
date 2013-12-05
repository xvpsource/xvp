/*
 * requester.js - Update requester for Xen VNC Proxy PHP Pages
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

var http = createRequestObject();
var pools = new Array();
var tid = null;

function createRequestObject()
{
    var http;

    try {
	// Non-IE and IE >= 7
	http = new XMLHttpRequest();
    } catch(e) {
	try {
	    // IE <= 6
	    http = new ActiveXObject("Microsoft.XMLHttp");
	} catch(e) {
	    // empty
	}
    }

    if (http)
	return http;

    alert("Unable to create XMLHttpRequest object");
}

function updateRequest()
{
    /*
     * Although we want this run at regular intervals, we do not use
     * setInterval(), because we also get run as callback after actions
     * like VM boot, and there want to restart the timer from then.
     */
    if (tid != null)
	clearTimeout(tid);
    tid = setTimeout("updateRequest()", 60000);

    if (!http)
	return;

    epoch = "Thu, 01 Jan 1970 08:25:01 GMT"; // to prevent IE caching it!

    try {
	http.open("GET", "update.php", true);
	http.setRequestHeader("If-Modified-Since", epoch);
	//http.setRequestHeader("Connection", "close");
	http.onreadystatechange = updateReply;
	http.send(null);
    } catch(e) {
	alert("Unable to connect: " + e.message);
    }
}

window.actionCallback = function(fullname, vmname, action, result) {

    clearBusy(fullname);

    if (result == "true") {
	action = action.substr(0,1).toUpperCase() + action.substr(1);
	alert(action + " of virtual machine " + vmname + " succeeded");
    } else {
	alert("Unable to " + action + " virtual machine " + vmname);
    }

    updateRequest();
};

function setState(fullname, state, info)
{
    var state_element = document.getElementById("state-" + fullname);
    if (!state_element)
	return false;

    var color;
    var printstate = state + " " + info;

    state_element.lastChild.nodeValue = printstate;

    // If you change these background colors, edit styles.css to match
    switch (state) {
    case "Running":
	color = "#20b020";
	break;
    case "Halted":
	color = "#d02020";
	break;
    case "Suspended":
	color = "#4060f0";
	break;
    default:
	color = "#c060c0";
	break;
    }

    // Some browsers support this ...
    state_element.setAttribute("class", state.toLowerCase());

    // ... and some support this
    state_element.style.cssText = "background-color: " + color;
    return true;
}

function hostSetDetails(fullname, osversion)
{
   var osicon = document.getElementById("osicon-" + fullname);

   osicon.setAttribute("alt", osversion);
   osicon.setAttribute("title", osversion);
}

function rightsOkForOperation(operations, name, rights)
{
    var needed = "disabled";
    var ok;

    for (var i = 0; i < operations.length; i++) {
	if (operations[i].getAttribute("name") == name) {
	    needed = operations[i].getAttribute("rights");
	    break;
	}
    }

    switch (needed) {
    case "all":
	ok = (rights == "all");
	break;
    case "control":
	ok = (rights == "all" || rights == "control");
	break;
    case "write":
	ok = (rights == "all" || rights == "control" || rights == "write");
	break;
    case "read":
	ok = (rights != "none" && rights != "list");
	break;
    case "list":
	ok = (rights != "none");
	break;
    case "none":
	ok = true;
	break;
    case "disabled":
    default:
	ok = false;
	break;
    }

    return ok;
}

function contextForRights(operations, possible, rights)
{
    var names = possible.split(",");
    var allowed = new Array(0);
    var mobile = tapOpensContextMenu();

    for (var i = 0; i < names.length; i++) {
	if (mobile && names[i] == "console")
	    continue;
	if (rightsOkForOperation(operations, names[i], rights))
	    allowed.push("#" + names[i]); // "#" for jQuery Context Menu plugin
    }

    return allowed.toString();
}

function vmSetDetails(fullname, labelname, state, rights, operations, platform, osversion)
{
   var label  = document.getElementById("label-" + fullname);
   var button = document.getElementById("button-" + fullname);
   var osicon = document.getElementById("osicon-" + fullname);
   var action = "blank";
   var help   = "";
   var bubble = "";
   var context = "";
   var realstate = state;
   var mobile = tapOpensContextMenu();

   $('#vmmenu-' + fullname).disableContextMenuItems();

   if (isBusy(fullname))
       state = "Busy";

   /*
    * Security note: the operations a user is permitted to perform are
    * actually enforced server-side.  What we're doing here client-side
    * is additional to that, so we can show the user what's allowed.
    */

   switch (state) {
   case "Running":
       context = contextForRights(operations, "console,shutdown,poweroff,reboot,reset,suspend,migrate,snapshot,properties", rights);
       if (rightsOkForOperation(operations, "console", rights)) {
	   action = "console";
	   help   = "left click to open console or right click for menu";
       }
       break;
   case "Halted":
       context = contextForRights(operations, "boot,booton,bootrecovery,snapshot,properties", rights);
       if (rightsOkForOperation(operations, "boot", rights)) {
	   action = "boot";
	   help   = "left click to boot or right click for menu";
       }
       break;
   case "Suspended":
       context = contextForRights(operations, "resume,resumeon,properties", rights);
       if (rightsOkForOperation(operations, "resume", rights)) {
	   action = "resume";
	   help   = "left click to resume or right click for menu";
       }
       break;
   case "Busy":
       // console not usable in all cases, but useful to watch shutdown, etc
       context = contextForRights(operations, "console", rights);
       action = "busy";
       break;
   }

   if (mobile) {
       action = "info";
       help   = "tap for menu";
   } else if (action == "blank" && context != "") {
       action = "info";
       help   = "right click for menu";
   }

   label.lastChild.nodeValue = labelname;

   osicon.src = "images/" + platform + ".png";
   osicon.setAttribute("alt", osversion);
   osicon.setAttribute("title", osversion);

   button.src = "images/" + action + ".png";

   if (state != "Busy") {
       button.setAttribute("alt", help);
       button.setAttribute("title", help);
   }

   if (context != "") {
       $('#vmmenu-' + fullname).enableContextMenuItems(context);
   }

   if (mobile) {
       $('#vmmenu-' + fullname + ' > li.console').html('<p class="contextLabel">^</p>');
   }
}

function updateReply()
{
    if (http.readyState != 4) { // not complete

	var msg;

	switch (http.readyState) {
	case 0:
	    //msg = "Uninitialised";
	    break;
	case 1:
	case 3:
	    //msg = "Busy ...";
	    break;
	case 2:
	    //msg = "Loaded";
	    break;
	default:
	    //msg = "unexpected state " + http.readyState;
	    break;
	}

	return;
    }

    if (http.status == 0) {
	// e.g. if server down, note statusText may not be defined 
	return;
    } else if (http.status != 200) {
	alert("Error retrieving data: " + http.status + ": " + http.statusText);
	return;
    }

    try {

	var xmldoc = http.responseXML;
	var timestamp = xmldoc.getElementsByTagName("timestamp")[0];
	var operations = xmldoc.getElementsByTagName("operation");
	var hosts = xmldoc.getElementsByTagName("host");
	var vms = xmldoc.getElementsByTagName("vm");

	// Starting with 1.14.0, timestamp is UTC seconds, not formatted text,
	// to display in client local time, as suggested by Christian Scheele
	var updated = Date(timestamp.lastChild.nodeValue * 1000);
	document.getElementById("timestamp").lastChild.nodeValue = "Last updated " + updated;

	for (var i = 0; i < hosts.length; i++) {
	    var fullname  = hosts[i].getAttribute("fullname");
	    var osversion = hosts[i].getAttribute("osversion");
	    var state     = hosts[i].getAttribute("state");
	    var role      = hosts[i].getAttribute("role");
	    var memfree   = hosts[i].getAttribute("memfree");
	    if (role != "")
		role = "as " + role;
	    if (setState(fullname, state, role)) {
		hostSetDetails(fullname, osversion);
		document.getElementById("memfree-" + fullname).lastChild.nodeValue = memfree;
	    }
	}
	
	for (var i = 0; i < vms.length; i++) {
	    var fullname  = vms[i].getAttribute("fullname");
	    var labelname = vms[i].getAttribute("label");
	    var osversion = vms[i].getAttribute("osversion");
	    var state     = vms[i].getAttribute("state");
	    var rights    = vms[i].getAttribute("rights");
	    var platform  = vms[i].getAttribute("platform");
	    var uptime    = vms[i].getAttribute("uptime");
	    var memtotal  = vms[i].getAttribute("memtotal");
	    var printstate = state + " " + uptime;

	    if (setState(fullname, state, uptime)) {
		vmSetDetails(fullname, labelname, state, rights, operations, platform, osversion);
		document.getElementById("memtotal-" + fullname).lastChild.nodeValue = memtotal;
	    }
	}
	
    } catch(e) {

	message = "Error: Problem handing response: " + e.message;
	alert(message);
	return;
    }
}
