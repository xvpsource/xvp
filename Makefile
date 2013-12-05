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
GSRCS = README README.tightvnc LICENCE.TXT Makefile xvp.8 xvpdiscover.8 xvptag.8 xvpappliance.8 xvp.conf.5 xvpviewer.1 xvpweb.7 xvpusers.conf.5 xvprights.conf.5 xvp.spec xvp.rc xvp.logrotate
CSRCS = $(wildcard server/*.c) $(wildcard server/*.h) server/Makefile
JSRCS = $(wildcard viewer/*.java) $(wildcard viewer/*.tightvnc) \
	 $(patsubst %,viewer/%, MANIFEST.MF Makefile \
	index.html index.vnc xvpviewer xvpviewer.bat)
WSRCS = $(wildcard web/*.html) $(wildcard web/*.inc) web/VncViewer.jar $(wildcard web/*.php) $(wildcard web/css/*.css) $(wildcard web/images/*.png) $(wildcard web/js/*.js) web/xvprights.default
ASRCS = appliance/xvpappliance $(wildcard appliance/*.menu)
ZIPFILES = README README.tightvnc LICENCE.TXT viewer/xvpviewer viewer/xvpviewer.bat viewer/VncViewer.jar
TAROPTS = --owner root --group root
SBINDIR = /usr/sbin
DATADIR = /usr/share/xvp
MANDIR = /usr/share/man
INSTALL = install -p

all: man
	$(MAKE) -C server all
	$(MAKE) -C viewer all
	$(MAKE) web/VncViewer.jar

clean:
	$(MAKE) -C server clean
	$(MAKE) -C viewer clean
	$(RM) *.gz *.zip
	$(RM) web/*.jar

install: installman
	$(MAKE) -C server install
	$(MAKE) -C viewer install
	test -f /etc/redhat-release && $(INSTALL) -m 0755 appliance/xvpappliance $(DESTDIR)$(SBINDIR)/xvpappliance || true
	test -f /etc/redhat-release && $(INSTALL) -m 0644 appliance/*.menu $(DESTDIR)$(DATADIR)/ || true
	$(INSTALL) -m 0644 web/xvprights.default $(DESTDIR)$(DATADIR)/xvprights.default

man: xvp.8.gz xvpdiscover.8.gz xvptag.8.gz xvpappliance.8.gz xvp.conf.5.gz xvpviewer.1.gz xvpweb.7.gz xvpusers.conf.5.gz xvprights.conf.5.gz

installman: man
	mkdir -p $(DESTDIR)$(MANDIR)/man1 $(DESTDIR)$(MANDIR)/man5 $(DESTDIR)$(MANDIR)/man7 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) -m 0644 xvp.8.gz $(DESTDIR)$(MANDIR)/man8/xvp.8.gz
	$(INSTALL) -m 0644 xvpdiscover.8.gz $(DESTDIR)$(MANDIR)/man8/xvpdiscover.8.gz
	$(INSTALL) -m 0644 xvptag.8.gz $(DESTDIR)$(MANDIR)/man8/xvptag.8.gz
	test -f /etc/redhat-release && $(INSTALL) -m 0644 xvpappliance.8.gz $(DESTDIR)$(MANDIR)/man8/xvpappliance.8.gz || true
	$(INSTALL) -m 0644 xvp.conf.5.gz $(DESTDIR)$(MANDIR)/man5/xvp.conf.5.gz
	$(INSTALL) -m 0644 xvpviewer.1.gz $(DESTDIR)$(MANDIR)/man1/xvpviewer.1.gz
	$(INSTALL) -m 0644 xvpusers.conf.5.gz $(DESTDIR)$(MANDIR)/man5/xvpusers.conf.5.gz
	$(INSTALL) -m 0644 xvprights.conf.5.gz $(DESTDIR)$(MANDIR)/man5/xvprights.conf.5.gz
	$(INSTALL) -m 0644 xvpweb.7.gz $(DESTDIR)$(MANDIR)/man7/xvpweb.7.gz

xvp.8.gz: xvp.8
	gzip -c $< >$@

xvpdiscover.8.gz: xvpdiscover.8
	gzip -c $< >$@

xvptag.8.gz: xvptag.8
	gzip -c $< >$@

xvpappliance.8.gz: xvpappliance.8
	gzip -c $< >$@

xvp.conf.5.gz: xvp.conf.5
	gzip -c $< >$@

xvpviewer.1.gz: xvpviewer.1
	gzip -c $< >$@

xvpweb.7.gz: xvpweb.7
	gzip -c $< >$@

xvpusers.conf.5.gz: xvpusers.conf.5
	gzip -c $< >$@

xvprights.conf.5.gz: xvprights.conf.5
	gzip -c $< >$@

web/VncViewer.jar: viewer/VncViewer.jar
	cp -p $^ $@

tarball: xvp-$(VERSION).tar.gz

xvp-$(VERSION).tar.gz: $(GSRCS) $(CSRCS) $(JSRCS) $(WSRCS)
	rm -rf xvp-$(VERSION)
	mkdir -p xvp-$(VERSION) xvp-$(VERSION)/server xvp-$(VERSION)/viewer
	cp -p $(GSRCS) xvp-$(VERSION)
	cp -p $(CSRCS) xvp-$(VERSION)/server
	cp -p $(JSRCS) xvp-$(VERSION)/viewer
	tar cf - $(WSRCS) $(ASRCS) | (cd xvp-$(VERSION); tar xf -) 
	find xvp-$(VERSION) -type d | xargs chmod 755
	find xvp-$(VERSION) -type d | xargs chmod -s
	find xvp-$(VERSION) -type f | xargs chmod 644
	chmod 755 xvp-$(VERSION)/appliance/xvpappliance
	tar -c -z $(TAROPTS) -f $@ xvp-$(VERSION)
	rm -rf xvp-$(VERSION)

zipfile: xvpviewer-$(VERSION).zip

xvpviewer-$(VERSION).zip: $(ZIPFILES)
	zip -rj $@ $^
