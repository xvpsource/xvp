#
# Copyright (C) 2009-2013, Colin Dean
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#

VERSION = 1.16.0

SBINDIR = /usr/sbin
OBJS = $(patsubst %.c, %.o, $(wildcard *.c))
CFLAGS = -g
CPPFLAGS =  -I /usr/include/libxml2
LDFLAGS = -lxenserver -lcurl -lcrypto -lxml2 -lssl -lpthread
INSTALL = install -p

all: xvp xvpdiscover xvptag

xvp: config.o logging.o main.o password.o process.o proxy.o xenapi.o
	$(CC) $(LDFLAGS) -o $@ $^

xvpdiscover: xvpdiscover.o password.o
	$(CC) $(LDFLAGS) -o $@ $^

xvptag: xvptag.o password.o
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJS): xvp.h

clean::
	rm -f $(OBJS) xvp xvpdiscover xvptag

install: install_xvp install_xvpdiscover install_xvptag

install_xvp: xvp
	mkdir -p $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 -s xvp $(DESTDIR)$(SBINDIR)/xvp

install_xvpdiscover: xvpdiscover
	mkdir -p $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 -s xvpdiscover $(DESTDIR)$(SBINDIR)/xvpdiscover

install_xvptag: xvptag
	mkdir -p $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 -s xvptag $(DESTDIR)$(SBINDIR)/xvptag
