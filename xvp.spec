Summary: A VNC Console Proxy Server and Client for Citrix(R) XenServer
Name: xvp
Version: 1.16.0
Release: 1%{?dist}
License: GPL
Vendor: Colin Dean
Packager: Colin Dean <colin@xvpsource.org>
Group: System Environment/Libraries
URL: http://www.xvpsource.org/
Source: http://www.xvpsource.org/xvp-%{version}.tar.gz

%if 0%{?suse_version}
%define suse 1
%define xvpwebdir /srv/www/htdocs/xvpweb
%else
%define suse 0
%define xvpwebdir /var/www/html/xvpweb
%endif

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: libxml2-devel curl-devel
BuildRequires: libxenserver-devel >= 6.2.0
BuildRequires: java-devel

Requires: libxml2 curl
Requires: libxenserver >= 6.2.0
Provides: xvp = %{version}-%{release}
Obsoletes: xvp < %{version}-%{release}

%description
xvp (standing for Xen VNC Proxy) is a proxy server providing
password-protected VNC-based access to the consoles of virtual machines
hosted on Citrix XenServer.

Relying on a simple configuration file, it listens on multiple ports,
one per virtual machine, and forwards VNC sessions to the appropriate
XenServer host(s).  It uses a separate VNC password for each virtual
machine, as specified in encrypted form in the configuration file.

Standard VNC clients such as vncviewer(1) connect to the appropriate
port for the virtual machine they wish to access, and for each client a
separate xvp process is forked to authenticate the client, connect to
the appropriate XenServer host, and proxy the data traffic.

A Java-based VNC client, xvpviewer(1), is supplied with xvp.  This is
based on the TightVNC viewer, but with xvp-specific extensions to allow
virtual machine shutdown, reboot and reset to be initiated from the
viewer.

Also supplied is a web-based front end, providing convenient access to
all of the features of xvp and xvpviewer, with facilities for
restricting which users can manage particular virtual machines or groups
of virtual machines.

%package -n xvpviewer
Summary: A viewer for use with the xvp Xen VNC Proxy server
Group: User Interface/Desktops
Provides: xvpviewer = %{version}-%{release}
Obsoletes: xvpviewer < %{version}-%{release}

%description -n xvpviewer
xvpviewer is a Java-based VNC client, based on the TightVNC viewer.
It has extensions for use with the xvp(8) Xen VNC Proxy server, to
allow virtual machine shutdown, reboot and reset to be initiated from
the viewer.

%package -n xvpweb
Summary: A web-based front end for the xvp(8) Xen VNC Proxy server
Group: User Interface/Desktops
Provides: xvpweb = %{version}-%{release}
Obsoletes: xvpweb < %{version}-%{release}
%if %suse
Requires: httpd php php-curl php-mcrypt php-pdo
%else
Requires: httpd mod_ssl php php-mcrypt php-pdo
%endif

%description -n xvpweb
xvpweb is a web-based front end for use with the xvp(8) Xen VNC Proxy
server, written in PHP, providing convenient access to all of the
features of xvp(8) and xvpviewer(1), with facilities for restricting
which users can manage particular virtual machines or groups of virtual
machines.

%if %suse
%else
%package -n xvpappliance
Summary: Appliance scripts for the xvp(8) Xen VNC Proxy server
Group: User Interface/Desktops
Provides: xvpappliance = %{version}-%{release}
Obsoletes: xvpappliance < %{version}-%{release}
Requires: xvp = %{version}-%{release}%{?xvpdist}
Requires: xvpweb = %{version}-%{release}
%endif

%if %suse
%else
%description -n xvpappliance
xvpappliance is a program to manage the xvp(8) Xen VNC Proxy server and
xvpweb(7) web-based front end as a self-contained appliance.  It has a
simple menu-driven text-based interface.
%endif

%prep
%setup -q

%build
make

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
mkdir -p $RPM_BUILD_ROOT/etc/init.d
install -m 0755 xvp.rc $RPM_BUILD_ROOT/etc/init.d/xvp
mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
install -m 0644 xvp.logrotate $RPM_BUILD_ROOT/etc/logrotate.d/xvp
mkdir -p $RPM_BUILD_ROOT%{xvpwebdir}
cp -R web/* $RPM_BUILD_ROOT%{xvpwebdir}/
rm $RPM_BUILD_ROOT%{xvpwebdir}/xvprights.default

%files -n xvp
%defattr(-,root,root)
%doc README LICENCE.TXT
%{_sbindir}/xvp
%{_sbindir}/xvpdiscover
%{_sbindir}/xvptag
%{_mandir}/man8/xvp.8.gz
%{_mandir}/man8/xvpdiscover.8.gz
%{_mandir}/man8/xvptag.8.gz
%{_mandir}/man5/xvp.conf.5.gz
%{_sysconfdir}/init.d/xvp
%{_sysconfdir}/logrotate.d/xvp

%files -n xvpviewer
%defattr(-,root,root)
%doc README README.tightvnc LICENCE.TXT
%{_bindir}/xvpviewer
%{_mandir}/man1/xvpviewer.1.gz
%{_datadir}/xvp/VncViewer.jar
%{_datadir}/xvp/xvpviewer.bat

%files -n xvpweb
%defattr(-,root,root)
%doc README README.tightvnc LICENCE.TXT
%{_mandir}/man5/xvpusers.conf.5.gz
%{_mandir}/man5/xvprights.conf.5.gz
%{_mandir}/man7/xvpweb.7.gz
%{xvpwebdir}/*
%{_datadir}/xvp/xvprights.default

%if %suse
%else
%files -n xvpappliance
%defattr(-,root,root)
%doc README LICENCE.TXT
%{_sbindir}/xvpappliance
%{_mandir}/man8/xvpappliance.8.gz
%{_datadir}/xvp/*.menu
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ "$1" = 1 ] ; then
    /sbin/chkconfig --add xvp >/dev/null 2>&1
fi

%preun
if [ "$1" = 0 ] ; then
    /sbin/service xvp stop >/dev/null 2>&1
    /sbin/chkconfig --del xvp >/dev/null 2>&1
fi

%postun
if [ "$1" -ge 1 ]; then
    /sbin/service xvp condrestart >/dev/null 2>&1
fi

%if %suse
%else
%post -n xvpappliance
/usr/sbin/xvpappliance --install "%{version}-%{release}"
%endif

%if %suse
%else
%preun -n xvpappliance
if [ "$1" = 0 ] ; then
    /usr/sbin/xvpappliance --uninstall
fi
%endif

%changelog
* Sun Nov 10 2013 Colin Dean <colin@xvpsource.org> 1.16.0-1
- Single sign-on fix for clients requesting "VncViewer.jar.pack.gz"
- Appliance now includes logrotate package (missing in 1.15.0-1)
- Brackets and commas in pool name now don't disable context menus
- The xvp daemon and related utilities are now built against version
  6.2.0 of Citrix's libxenserver, instead of version 6.1.0.

* Sun Nov  4 2012 Colin Dean <colin@xvpsource.org> 1.15.0-1
- Added VM snapshot management facilities to xvpweb.
- Fixed problem resulting from incompatible change in XCP 1.6 which
  prevented xvpweb from ever updating its status display.
- The web console.php script now uses REQUEST instead of POST, for
  easier integration with scripts other than xvpweb. 
- The xvp daemon and related utilities are now built against version
  6.1.0 of Citrix's libxenserver, instead of version 6.0.0.

* Wed Aug 15 2012 Colin Dean <colin@xvpsource.org> 1.14.0-1
- Incorporated European keyboard fixes (as discussed on mailing list
  in March) into console viewer.
- Fixed bug where properties page in xvpweb would display HA
  Priority as "do not restart" instead of "protected" with recent
  versions of XenServer.
- Fixed timezone-related bug in xvpweb that could display negative
  uptimes for VMs.  Also the "Last updated" time shown by xvpweb is now
  shown in the client's timezone, not the appliance's.
- The xvp daemon and related utilities are now built against version
  6.0.0 of Citrix's libxenserver, instead of version 5.6.100.

* Sat Mar 10 2012 Colin Dean <colin@xvpsource.org> 1.13.1-1
- Fixed bug in 1.13.0 that Ctrl-Alt-Del no longer worked.

* Fri Mar  9 2012 Colin Dean <colin@xvpsource.org> 1.13.0-1
- Appliance now provides single sign-on: starting the Java console
  viewer no longer requires username and password to be re-entered.
- The Java viewer's clipboard window now has a Send button, to send
  its contents to the VM, and the xvp proxy now converts client cut
  text messages into a sequence of key event messages.
- Fixed bug causing viewer disconnection if key or mouse events were
  input rapidly.
- Alt-Gr keys on European keyboards now work, at least for Linux VMs.
- Appliance built on CentOS 5.8 release instead of 5.7.

* Wed Feb 22 2012 Colin Dean <colin@xvpsource.org> 1.12.2-1
- Updated xvp proxy to handle fragmented data sent by Java 7 over SSL.

* Tue Jan 31 2012 Colin Dean <colin@xvpsource.org> 1.12.1-1
- Migrating a VM between hosts that have different CPU stepping is now
- forced where possible.
- The VM Properties window in xvpweb now displays the memory used by
- small VMs in MB instead of GB, to match the main page.
- The performance improvements introduced in 1.12.0 have been withdrawn,
- as they caused unresolved problems on some sites.  The improvements
- are still available as an experimental unsupported option, available
- by replacing the update.php script by update-bulk.php.

* Thu Dec 29 2011 Colin Dean <colin@xvpsource.org> 1.12.0-1
- Performance scalability improvements to xvpweb.
- Images in ISO libraries are now sorted by name in xvpweb.

* Tue Oct  4 2011 Colin Dean <colin@xvpsource.org> 1.11.0-1
- Confirmation alerts in xvpweb now display name of VM.
- Icon for creating Home Screen links on iOS now included in xvpweb.
- Appliance built on current CentOS 5.7 release instead of 5.6.

* Thu Jul 28 2011 Colin Dean <colin@xvpsource.org> 1.10.0-1
- Added support (minus consoles) for Apple iOS Mobile Safari browser. 

* Wed Jul  6 2011 Colin Dean <colin@xvpsource.org> 1.9.3-1
- Bug fix: xvp always denies console access to VM if VM's VNC password
- in config file begins with "00".
 
* Sun Apr 24 2011 Colin Dean <colin@xvpsource.org> 1.9.2-1
- Bug fix contributed by Dmitry Ketov: previously, in console viewer,
- random scrolling could occur on pointer move after wheel scrolling.
- Bundled jQuery library upgraded from 1.4.3 to 1.5.2, which officially
- supports Internet Explorer 9.
- Appliance built on current CentOS 5.6 release instead of 5.5.

* Thu Mar 31 2011 Colin Dean <colin@xvpsource.org> 1.9.1-1
- Bug fix: Previously, granting rights at individual VM level in some cases
- didn't allow all expected operations to be performed in xvpweb.
- Added recognition of Gentoo Linux to xvpweb.
- Properties window in xvpweb now displays Xen Tools information for running
- virtual machines.
- Added Fn key support to xvpviewer, contributed by Dmitry Ketov.

* Tue Jan 11 2011 Colin Dean <colin@xvpsource.org> 1.9.0-1
- The relationship in xvpweb between rights (read, control, etc) and specific
- operations (boot, shutdown, open console, etc) can now be customised by
- creating a file /etc/xvprights.conf.
- In xvpweb, for any VM where some context menu items are enabled but not
- boot or console operations, a blue icon with an "i" is displayed, with a
- "right click for menu" balloon when hovered over, instead of no icon.
- In xvpweb, if the name of a VM cannot be determined, it is now displayed
- as "Unknown" instead of displaying a 36-character UUID.
- A "Boot Recovery" option has been added to the context menu in xvpweb, to
- boot the VM in recovery mode (similar to the recovery mode facility in the
- "Boot on ..." option, but without the ability to select the server host).
- By default, "Boot Recovery" requires "all" rights.
- Bug fix: Previously, saying no to tunelling during initial configuration
- of xvpappliance caused console connections to fail with "Network Error"
- message until "Configure web server" option was run again.
- Bug fix: Previously, pools with empty name labels broke rediscovery in
- xvpappliance, now name of master host is used as pool name in this case.
- When selecting access rights for a user in the xvpappliance configuration
- program, there is now a "Cancel" option.
- Appliance configuration program modified so as to work on systems with
- Bash 4 installed, and on systems where "hostname" command without "-s"
- or "-f" displays short name.
- Makefile for xvp executable modified to explicitly link curl and crypto
- libraries, so "make" doesn't fail on recent Linux distributions.

* Wed Dec 29 2010 Colin Dean <colin@xvpsource.org> 1.8.1-1
- Bug fix: OS version strings containing top bit set characters for some
- Windows VMs could cause browsers to fail to display xvpweb, with messages
- including "Error: Problem handing response: timestamp is undefined".

* Tue Dec 28 2010 Colin Dean <colin@xvpsource.org> 1.8.0-1
- In xvpweb, the "Boot on ..." dialog now has the option of booting the
- virtual machine in recovery mode (CD/DVD or network PXE boot).
- The Properties dialog in xvpweb now displays VM description.
- New yellow "in progress" icon displays in xvpweb while VM is being
- boooted, shutdown, migrated, etc.
- Pool, host, group and VM names, and VM descriptions may now contain any
- Latin, Greek or Cyrillic characters - using UTF-8 encoding. 
- Added support for non ISO Latin-1 Unicode characters to xvpviewer (but
- note that XenServer does not as yet support these).
- Built using latest libxenserver library from XenServer 5.6 FP1.
- Appliance disk partitions now aligned by kickstart on 64KB boundaries,
- to avoid misaligned I/O on some NAS/SAN storage systems.

* Fri Nov  5 2010 Colin Dean <colin@xvpsource.org> 1.7.1-1
- Fixed "Unexpected action: xvpappliance" button click error in xvpweb.
- Upgraded bundled jQuery in xvpweb from 1.4.2 to 1.4.3.
- Renamed popup.php in xvpweb to opener.php, to bypass some over-keen
- proxy URL filters (e.g. privoxy).
- Added keysym event tracing to xvp for keyboard layout testing.

* Sat Oct 23 2010 Colin Dean <colin@xvpsource.org> 1.7.0-1
- Added menu support for LDAP authentication in xvpappliance.
- Fixed loopback hostname setting in xvpappliance /etc/hosts.

* Thu Aug  5 2010 Colin Dean <colin@xvpsource.org> 1.6.2-1
- Added "xvptag" command-line utility for managing VM tags.
- Set error reporting in xvpweb to suppress deprecated warnings with PHP 5.3.

* Sun Jun 13 2010 Colin Dean <colin@xvpsource.org> 1.6.1-1
- Bug fix: in xvpweb, long group names are no longer split over multiple
- lines when collapsing all groups in a pool.
- Bug fix: in xvpappliance, adding a user for a specific pool and all groups
- no longer always shows list of VMs from most recently added pool instead.
- "Configure xvp users" menu in xvpappliance now lists users in alphabetical
- order.
- "Configure user" sub-menu in xvpappliance now allows user to have more
- than one rights entry.

* Sat Jun  5 2010 Colin Dean <colin@xvpsource.org> 1.6.0-1
- Added expand/collapse buttons to xvpweb.
- Staggered update/rediscover cron jobs in xvpappliance by 5 minutes.
- xvp reload output displayed correctly now in xvpappliance console.
- Fixed some host console issues in xvp itself, and added "-c" flag to
- xvpdiscover to output support for host consoles.

* Thu Apr 15 2010 Colin Dean <colin@xvpsource.org> 1.5.1-1
- Added "-a" option to xvpdiscover to output host IP addresses, and adapted
- xvp, xvpweb and xvpappliance to make use of these where present, so that
- hosts with name labels different from their hostnames can be supported.
- Fixed bug in xvpappliance rejecting email addresses containing "t".
- Fixed bug in xvpappliance rediscovery not using FQDN for hostnames.
- Fixed bug in xvweb: errors with force shutdown of protected VMs.
- xvpappliance now generates new SSH keys during initial bootstrap.
- xvpappliance can now tunnel console VNC connections over HTTP/HTTPS.
- xvpweb and xvpappliance now allow server hosts to be hidden from users.
- Maximum supported pool name length increased from 31 to 79 characters.
- Increased width for host and VM names in xvpweb to accommodate longer names.
- Speed improvements to xvpweb for users with limited rights.
- Increased default console reconnect delay for xvp from 10 to 20 seconds.

* Mon Apr  5 2010 Colin Dean <colin@xvpsource.org> 1.5.0-1
- Added xvpappliance package (for Red Hat only).
- xvp.conf may now INCLUDE other files, up to a max depth of 5.
- Some rebadging to remove/replace word "XenServer" where things would
- equally apply to Xen Cloud Platform.
- Support added to xvpweb to treat xvpappliance VM as distinct OS type.

* Mon Mar 15 2010 Colin Dean <colin@xvpsource.org> 1.4.1-1
- In xvpweb, context menu items "Poweroff" and "Reset" are now called
- "Force Shutdown" and "Force Reboot", to match wording XenCenter uses.
- Bug fix: xvpweb context menu was missing if VM had dots in name.
- Bug fix: if VM was listed in xvp.conf by UUID, then a number of
- context menu operations in xvpweb didn't work.

* Sat Mar 13 2010 Colin Dean <colin@xvpsource.org> 1.4.0-1
- xvpweb now has a right-button context menu, supporting boot, shutdown,
- reboot, suspend, resume and migrate operations and a properties pop-up.
- From the properties pop-up, it is possible to insert and eject
- virtual DVD drives.
- New xvpweb operations that refer to specific server hosts (boot on,
- resume on and migrate) or DVD drives require new "all" rights.
- xvp no longer appends domain name to host name when connecting if the
- host name looks like an IPv4 address.
- xvpweb now displays domain name from xvp.conf for each pool.
- Bug fix: server Makefile "make install" dependency fixes.
- Now using Xen API version 1.5 instead of 1.3.

* Tue Jan 19 2010 Colin Dean <colin@xvpsource.org> 1.3.4-1
- Bug fix: xvpdiscover now outputs DOMAIN "" instead of
- DOMAIN "unknown" if there doesn't seem to be a domain name.

* Tue Jan 19 2010 Colin Dean <colin@xvpsource.org> 1.3.3-1
- Bug fix: OTP REQUIRE could fail although OTP ALLOW worked.
- Bug fix: xvp and xvpweb failed to append domainname to hostname
- when connecting to XenServer hosts.
- Updated xvp.conf man page to clarify how domainname is used.
- Tidied up /etc/init.d/xvp for Red Hat based distributions.

* Fri Dec 11 2009 Colin Dean <colin@xvpsource.org> 1.3.2-1
- Bug fix: xvpweb no longer hangs if a host was recently shutdown.
- Added RPM and init.d support for openSUSE.
- Viewer updated to not run out of memory with openjdk.
- README now mentions the need for cURL support in PHP.
- VMs with duplicate names are now allowed, provided in different pools.

* Sat Dec  5 2009 Colin Dean <colin@xvpsource.org> 1.3.1-1
- Bug fix: if xvpvewer specified wrong vm name label or UUID when
- connecting to a non-multiplex port, this was not rejected.
- Bug fix: signals sent to xvp master process failed to signal child
- processes if master was running in background (so couldn't disconnect
- clients on exit or dump current connection information).
- Incompatible change: new rights option in xvpweb, "control", needed
- in order to have shutdown/reboot/reset buttons enabled: "write" now
- allows keyboard/mouse input but not these buttons.
- It is no longer necessary to set short_open_tag = Off in the php.ini
- file for xvpweb.

* Tue Dec  1 2009 Colin Dean <colin@xvpsource.org> 1.3.0-1
- xvpweb now uses encrypted one time VNC passwords: previous releases
- exposed VNC passwords in plain text to the browser.
- Added "Security Considerations" section to the README file.
- Added support for new XVP security type in RFB protocol, allowing
- client programs to identify username and to select specific target
- console (the latter being a prerequisite for multiplex support).
- Introduced ability for xvp to multiplex client connections using a
- single port, and updated xvpviewer, xvpweb and xvpdiscover to match.
- Console connections from xvpweb to xvp can now be tunnelled over the
- existing HTTP or HTTPS connection.
- xvpweb can now give users different rights: "list", "read" or "write",
- on a per user/pool/group/vm basis, with "list" prohibiting boot and
- console access, and "read" giving read-only console access (i.e. no
- buttons or keyboard/mouse input.
- Added new manual pages: xvpweb(7) and xvpusers.conf(5).
- Removed references to non-existent css/print.css in xvpweb.
- Replaced XenServer host icon in xvpweb with a prettier one.

* Thu Nov 19 2009 Colin Dean <colin@xvpsource.org> 1.2.5-1
- Fixed handling of XML special characters in passwords, etc.
- Re-badged to refer to xvpsource.org.

* Tue Nov 17 2009 Colin Dean <colin@xvpsource.org> 1.2.4-2
- Rebuild xvpweb - previous build had no mouse-scrolling support

* Thu Nov  5 2009 Colin Dean <colin@xvpsource.org> 1.2.4-1
- Bug fix: 16-character XenServer passwords no longer give
- SESSION_AUTHENTICATION_FAILED errors.
- Bug fix: XenServer passwords whose encrypted form contain zeroes
- no longer give SESSION_AUTHENTICATION_FAILED errors.
- Passwords longer than 8 characters passed to "xvp -e" or longer than
- 16 to "xvp -x" now raise an error instead of being quietly truncated,
- with similar checks also added to xvpdiscover.

* Thu Oct 22 2009 Colin Dean <colin@xvpsource.org> 1.2.0-1
- When using explicit port numbers in xvp.conf (without leading ":"),
- the range of permitted ports is now 1024 to 65535.
- Virtual machine names in xvp.conf may now contain spaces (but in that
- case, must be enclosed in double quotes).
- Virtual machines may now be specified by UUID instead of display name,
- and in this case xvpweb adjusts to display name changes automatically. 
- New tool "xvpdiscover" included to probe a pool and output an xvp.conf.
- Host and VM name columns in web display are now wider to allow for
  longer names without breaking the layout.
- The xvpweb package now knows it depends on the php-mcrypt package.
- Fixed errors generated by xvpweb about undefined variables, invalid
- DATETIME values, and calls to scalarval() on non-objects.
- RPM installation, removal and upgrade now do the right things with
- chkconfig and service condrestart.

* Thu May 21 2009 Colin Dean <colin@xvpsource.org> 1.1.2-1
- Added mouse-wheel scrolling support to xvpviewer.
- Web front end now recognises Ubuntu and uses penguin icon for it.

* Wed May 13 2009 Colin Dean <colin@xvpsource.org> 1.1.1-1
- Reconnect timeout for xvp (-r option) now defaults to 10 seconds.
- xvp now reports shutdown/reboot/reset failure back to xvpviewer, logs
- details of reason, and xvpviewer displays a failure box.
- xvp now ensures ha_always_run is false prior to VM shutdown, to prevent
- shutdown failing for HA-protected VMs, and web interface now sets
- ha_always_run back to true to true after booting if restart priority is
- "Protected" or "Restart if possible".
- Web front end now handles (R) correctly in operating system bubbles,
- and quotes and ampersands in pool and group names.
- Some documentation corrections, including clarifying the difference
- between per-user web passwords and per-VM VNC passwords.

* Mon Apr 27 2009 Colin Dean <colin@xvpsource.org> 1.1.0-1
- Added optional DATABASE keyword to config format.
- Added xvpweb package.

* Tue Apr 21 2009 Colin Dean <colin@xvpsource.org> 1.0.1-1
- Added RFB extensions to request VM shutdown, reboot and reset.
- Added xvpviewer package.

* Thu Apr  9 2009 Colin Dean <colin@xvpsource.org> 1.0.0-2
- Fixed bug in logrotate script.

* Fri Apr  3 2009 Colin Dean <colin@xvpsource.org> 1.0.0-1
- Initial RPM build.
