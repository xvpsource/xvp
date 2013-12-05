<?php echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
/*
 * opener.php - Popup window opener for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2010, Colin Dean
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

$poolname = urlencode(stripslashes($_POST['poolname']));
$vmname   = urlencode(stripslashes($_POST['vmname']));
$action   = $_POST['action'];

// edit in globals.inc if you want to change popup window dimensions
$nw = XVP_POPUP_WIDTH;
$nh = XVP_POPUP_HEIGHT;

echo <<<EOF
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>xvp Popup Window Opener</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<meta http-equiv="Imagetoolbar" content="no" />
<meta http-equiv="pragma" content="no-cache" />
<meta http-equiv="Cache-Control" content="no-cache" />
<meta name="description" content="xvp Popup Window Opener" />
<meta name="copyright" content="(C) Colin Dean" />
<meta name="author" content="Colin Dean" />
<meta name="robots" content="noindex,nofollow,noarchive" />
<link rel="stylesheet" type="text/css" href="css/styles.css" />
</head>
<body>
<script type="text/javascript">

    /*
     * All I want to do here is place the centre of the pop up window
     * over the centre of the main window ...
     */

    var nw = $nw;
    var nh = $nh;

    var x, y, w, h;

    if (!(x = window.top.screenX) && !(x = window.top.screenLeft))
        x = 0;
    if (!(y = window.top.screenY) && !(y = window.top.screenTop))
        y = 0;

    if (!(w = window.top.outerWidth))
        w = window.top.document.documentElement.clientWidth;
    if (!(h = window.top.outerHeight))
        h = window.top.document.documentElement.clientHeight;

    x += (w - nw) / 2;
    y += (h - nh) / 2;

    window.open('$action.php?poolname=$poolname&vmname=$vmname',
                '_blank',
	        'left=' + x + ',top=' + y + ',height=' + nh + ',width=' + nw +
                ',location=no,menubar=no,personalbar=no,dependent=yes' + 
                ',scrollbars=no,status=no,toolbar=no,directories=no');

</script>
</body>
</html>
EOF;

?>
