<?php
/*
 * libc.inc - C compatibility functions for Xen VNC Proxy PHP Pages
 *
 * Copyright (C) 2009, Colin Dean
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

function atoi($string)
{
    if (!ctype_digit($string))
	return 0;

    return intval($string);
}
?>
