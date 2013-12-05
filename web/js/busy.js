/*
 * busy.js - Busy icon progress handling for Xen VNC Proxy PHP Pages
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

function setBusy(fullname, actiontext, inpopup)
{
    var doc    = inpopup ? opener.parent.document : document;
    var help   = actiontext + " in progress";
    // Leave console in context menu if currently enabled, but disable the rest
    var context = "#boot,#booton,#bootrecovery,#shutdown,#poweroff,#reboot,#reset,#suspend,#resume,#resumeon,#migrate,#snapshot,#properties";

    $('#vmmenu-' + fullname, doc).disableContextMenuItems(context);
    $('#busy-'   + fullname, doc).attr('value', "true");
    $('#button-' + fullname, doc).attr({
        src:   "images/busy.png",
        alt:   help,
        title: help
    });
}

function isBusy(fullname)
{
   var form = document.getElementById("form-" + fullname);

   return ($('input.busy', form).attr('value') == "true");
}

function clearBusy(fullname)
{
   var form = document.getElementById("form-" + fullname);

   $('input.busy', form).attr('value', 'false');
}
