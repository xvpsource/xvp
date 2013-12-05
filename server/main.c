/*
 * main.c - main program for Xen VNC Proxy
 *
 * Copyright (C) 2009-2012, Colin Dean
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
 * xvp (standing for Xen VNC Proxy) is a proxy server providing
 * password-protected VNC-based access to the consoles of virtual
 * machines hosted on Citrix XenServer or Xen Cloud Platform.
 *
 * Relying on a simple configuration file, it listens on multiple ports,
 * one per virtual machine, and forwards VNC sessions to the appropriate
 * XenServer or XCP host(s).  It uses a separate VNC password for each
 * virtual machine, as specified in encrypted form in the configuration
 * file.
 *
 * Standard VNC clients such as vncviewer can connect to the appropriate
 * port for the virtual machine they wish to access, and for each client
 * a separate xvp process is forked to authenticate the client, connect
 * to the appropriate host, and proxy the data traffic.
 *
 * For configuration details, refer to the associated manual page.
 *
 * One single-threading process handles all incoming client connections,
 * forking a multi-threading child process to handle server connection
 * and data proxying for each individual client.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netinet/in.h>

#include "xvp.h"

static fd_set xvp_read_fds;
static int    xvp_read_max = -1;


static void xvp_mainloop(void);

static void usage(void)
{
    fprintf(stderr,
"Xen VNC Proxy %d.%d.%d, Copyright (C) 2009-2012, Colin Dean\n",
	    XVP_MAJOR, XVP_MINOR, XVP_BUGFIX);
    fprintf(stderr,
"    Usage:\n"
"        xvp [ proxy-options | password-options ]\n"
	    );
    fprintf(stderr,
"    Proxy Options:\n"
"        -c | --configfile filename   ( default %s )\n"
"        -l | --logfile    filename   ( default %s, \"-\" = stdout )\n"
"        -p | --pidfile    filename   ( default %s )\n"
"        -r | --reconnect  seconds    ( reconnect delay, default %d )\n"
"        -n | --nodaemon              ( run in foreground )\n"
"        -v | --verbose               ( increase logging detail )\n"
"        -t | --trace                 ( enable some packet trace logging )\n",
	    XVP_CONFIG_FILENAME, XVP_LOG_FILENAME, XVP_PID_FILENAME,
	    XVP_RECONNECT_DELAY);
    fprintf(stderr,
"    Password Options:\n"
"        -e | --encrypt               (encrypt a vnc-password, prompts)\n"
"        -x | --xencrypt              (encrypt a xen-password, prompts)\n"
#ifdef DEBUG_PASSWORDS
"        -u | --unencrypt  encrypted-password\n"
#endif
	    );
    fprintf(stderr,
"    Config file format:\n"
"        # Comments, blank lines and additional whitespace ignored.\n"
"        # Specify port as either TCP port number between 1024 and 65535,\n"
"        # or VNC display (:0 to :99, :0 = port 5900, :1 = 5901, etc).\n"
"        # \"DATABASE\" and \"GROUP\" lines are used by xvpweb only.\n"
"        # \"OTP\" (one time passwords) line optional, default %s, %s, %d.\n"
"        # \"MULTIPLEX\" required if VM ports \"-\", otherwise optional.\n"
"        DATABASE dsn [ username [ password ] ]\n"
"        OTP REQUIRE|ALLOW|DENY [ IPCHECK ON|OFF|HTTP ] [ time-window-seconds ]\n"
"        MULTIPLEX port\n"
"        POOL poolname\n"
"            DOMAIN domainname\n"
"            MANAGER username encrypted-xen-password\n"
"            HOST hostname\n"
"            HOST ...\n"
"            GROUP groupname\n"
"            VM [port|-] vmname encrypted-vnc-password\n"
"            VM ...\n"
"            GROUP ...\n"
"        POOL ....\n",
	    XVP_OTP_MODE == XVP_OTP_ALLOW ? "ALLOW" :
	    (XVP_OTP_MODE == XVP_OTP_REQUIRE ? "REQUIRE" : "DENY"),
	    XVP_OTP_IPCHECK ? "ON" : "OFF", XVP_OTP_WINDOW);
    fprintf(stderr,
"    Config files may be nested, using INCLUDE \"filename\".\n");
    exit(1);
}

void *xvp_alloc(int size)
{
    void *p;

    if (!(p = calloc(1, size)))
	xvp_log(XVP_LOG_FATAL, "Out of memory");

    return p;
}

void xvp_free(void *p)
{
    free(p);
}

char *xvp_strdup(char *s)
{
    char *p = xvp_alloc(strlen(s) + 1);

    strcpy(p, s);
    return p;
}

bool xvp_is_ipv4(char *address)
{
    unsigned int i, ip[4];
    char dummy;

    if (sscanf(address, "%u.%u.%u.%u%c",
	       ip, ip + 1, ip + 2, ip + 3, &dummy) != 4)
	return false;

    for (i = 0; i < 4; i++)
	if (ip[i] > 255)
	    return false;

    return (strlen(address) <= XVP_MAX_ADDRESS);
}

/*
 * Copy text into supplied buffer, escaping XML special characters,
 * quietly truncating if not enough room.  Escaping is no longer
 * needed if building against libxenserver6, so this now just copies
 * unless build macro ESCAPE_XML_FOR_OLD_LIBXENSERVER is defined.
 */
char *xvp_xmlescape(char *text, char *buf, int buflen)
{
    char *src, *dst, *esc, *end = buf + buflen - 1;
    int len;

    for (src = text, dst = buf; *src && (dst < end); src++) {

	switch (*src) {
#ifdef ESCAPE_XML_FOR_OLD_LIBXENSERVER
	case '&':
	    esc = "&amp;";
	    len = 5;
	    break;
	case '<':
	    esc = "&lt;";
	    len = 4;
	    break;
	case '>':
	    esc = "&gt;";
	    len = 4;
	    break;
	case '"':
	    esc = "&quot;";
	    len = 6;
	    break;
	case '\'':
	    esc = "&apos;";
	    len = 6;
	    break;
#endif
	default:
	    *dst++ = *src;
	    continue;
	}

	if (dst + len > end)
	    break;

	memcpy(dst, esc, len);
	dst += len;
    }

    *dst = '\0';
    return buf;
}

int main(int argc, char **argv, char **envp)
{
    int optc = argc, type, maxpw;
    bool encrypting = false;
    char **optv = argv;

    if (argc == 2) {
	if (!strcmp(argv[1], "-e") || !strcmp(argv[1], "--encrypt")) {
	    encrypting = true;
	    maxpw = XVP_MAX_VNC_PW;
	    type = XVP_PASSWORD_VNC;
	} else if (!strcmp(argv[1], "-x") || !strcmp(argv[1], "--xencrypt")) {
	    encrypting = true;
	    maxpw = XVP_MAX_XEN_PW;
	    type = XVP_PASSWORD_XEN;
	}
    }

    if (encrypting) {

	char text[XVP_MAX_XEN_PW * 2 + 1]; /* intentionally longer than max */
	char hex[maxpw + 1];
	char *eol;
	struct termios term;

	if (isatty(0)) {
	    fputs("Password: ", stdout);
	    fflush(stdout);
	    tcgetattr(1, &term);
	    term.c_lflag &= ~ECHO;
	    tcsetattr(1, TCSADRAIN, &term);
	}

	fgets(text, sizeof(text), stdin);
	if (isatty(0)) {
	    term.c_lflag |= ECHO;
	    tcsetattr(1, TCSADRAIN, &term);
	    putchar('\n');
	}

	if ((eol = strchr(text, '\r')) || (eol = strchr(text, '\n')))
	    *eol = '\0';

	if (!*text) {
	    fprintf(stderr, "xvp: Empty passwords not supported\n");
	    exit(1);
	} else if (strlen(text) > maxpw) {
	    fprintf(stderr, "xvp: Password too long: maximum %d characters\n",
		    maxpw);
	    exit(1);
	}

	xvp_password_encrypt(text, hex, type);
	(void)xvp_password_hex_to_text(hex, text, type);
	puts(text);

	return 0;
    }
	
    while (optc > 1) {
	if (!strcmp(optv[1], "-c") || !strcmp(optv[1], "--configfile")) {
	    if (optc < 3)
		usage();
	    xvp_config_filename = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-l") || !strcmp(optv[1], "--logfile")) {
	    if (optc < 3)
		usage();
	    xvp_log_filename = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-p") || !strcmp(optv[1], "--pidfile")) {
	    if (optc < 3)
		usage();
	    xvp_pid_filename = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-n") || !strcmp(optv[1], "--nodaemon")) {
	    xvp_daemon = false;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-r") || !strcmp(optv[1], "--reconnect")) {
	    if (optc < 3)
		usage();
	    xvp_reconnect_delay = atoi(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-v") || !strcmp(optv[1], "--verbose")) {
	    xvp_verbose = true;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-t") || !strcmp(optv[1], "--trace")) {
	    xvp_tracing = true;
	    optv++;
	    optc--;
	    continue;
	}

	usage();
    }

    umask(077);
    xvp_log_init();
    xvp_process_init(argc, argv, envp);
    xvp_log(XVP_LOG_INFO, "Starting as master");
    xvp_config_init();
    xvp_listen_init();

    xvp_mainloop();

    xvp_process_cleanup();
    xvp_log_close();
    return 0;
}

static void xvp_listen_for_vm(xvp_vm *vm)
{
    int sock, val = 1, len = sizeof(val), flags;
    struct sockaddr_in listen_addr;

    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(vm->port);


    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&val, len) != 0 ||
	(flags = fcntl(sock, F_GETFL, 0)) == -1 ||
	fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0 ||
	bind(sock, (struct sockaddr *)&listen_addr,
	     sizeof(listen_addr)) != 0 ||
	listen(sock, XVP_VNC_LISTEN_BACKLOG) != 0)
	xvp_log(XVP_LOG_FATAL, "Unable to set up listening socket");

    FD_SET(sock, &xvp_read_fds);
    if (sock > xvp_read_max)
	xvp_read_max = sock;

    vm->sock = sock;

    if (vm->port < XVP_VNC_PORT_MIN || vm->port > XVP_VNC_PORT_MAX)
	xvp_log(XVP_LOG_INFO, "Listening on port %d for %s",
		vm->port, vm->vmname);
    else
	xvp_log(XVP_LOG_INFO, "Listening on port %d (VNC :%d) for %s",
		vm->port, vm->port - XVP_VNC_PORT_MIN, vm->vmname);
}

void xvp_listen_init(void)
{
    int fd, sigpipe;
    xvp_pool *pool;
    xvp_vm *vm;

    if (!xvp_child_pid)
	return;

    sigpipe = xvp_master_sigpipe[0];

    for (fd = 0; fd <= xvp_read_max; fd++) {
	if (FD_ISSET(fd, &xvp_read_fds) && fd != sigpipe) {
	    FD_CLR(fd, &xvp_read_fds);
	    close(fd);
	}
    }

    FD_ZERO(&xvp_read_fds);
    FD_SET(sigpipe, &xvp_read_fds);
    xvp_read_max = sigpipe;

    if (xvp_multiplex_vm)
	xvp_listen_for_vm(xvp_multiplex_vm);

    for (pool = xvp_pools; pool; pool = pool->next)
	for (vm = pool->vms; vm; vm = vm->next)
	    if (vm->port)
		xvp_listen_for_vm(vm);
}

static void xvp_mainloop(void)
{
    int sigpipe = xvp_master_sigpipe[0];
    fd_set read_fds;
    xvp_vm *vm;

    while (true) {

	read_fds = xvp_read_fds;
	int nfds = xvp_read_max + 1;
	int fd;
	int nready = select(nfds, &read_fds, NULL, NULL, NULL);

	if (nready < 0) {
	    if (errno == EINTR)
		continue;
	    xvp_log_errno(XVP_LOG_FATAL, "select");
	}

	for (fd = 0; fd < nfds; fd++) {
	    if (FD_ISSET(fd, &read_fds)) {
		if (fd == sigpipe) {
		    if (!xvp_process_signal_handler())
			return;
		} else if (vm = xvp_config_vm_by_sock(fd)) {
		    xvp_process_spawn(vm);
		} else {
		    xvp_log(XVP_LOG_FATAL, "Unexpected fd %d in mainloop", fd);
		}
	    }
	}
    }
}

char *xvp_message_code_to_text(int code)
{
    switch (code) {
    case XVP_MESSAGE_CODE_FAIL:
	return "fail";
    case XVP_MESSAGE_CODE_INIT:
	return "init";
    case XVP_MESSAGE_CODE_SHUTDOWN:
	return "shutdown";
    case XVP_MESSAGE_CODE_REBOOT:
	return "reboot";
    case XVP_MESSAGE_CODE_RESET:
	return "reset";
    }

    return "unknown";
}

/*
 * Standard connect(2) has too long a timeout for our purposes,
 * especially if we try to establish a session to a host that's down, so
 * slide in a modified implementation that allows us to set our own
 * timeout.
 */
int connect(int sock, const struct sockaddr *addr, socklen_t addrlen)
{
#define SYS_CONNECT 3 /* avoid needing kernel header <linux/net.h> */

    int flags, optval, optlen;
    fd_set mask;
    struct timeval timeout = {XVP_CONNECT_TIMEOUT, 0};

#ifdef SYS_socketcall
    void *args[3];

    args[0] = (void *)(long)sock;
    args[1] = (void *)addr;
    args[2] = (void *)(long)addrlen;
#endif

    if ((flags = fcntl(sock, F_GETFL, 0)) == -1 ||
	fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0)
	return -1;

#ifdef SYS_socketcall
    if (syscall(SYS_socketcall, SYS_CONNECT, args) == 0)
#else
    if (syscall(SYS_connect, sock, addr, addrlen) == 0)
#endif
	return fcntl(sock, F_SETFL, flags);
    else if (errno != EINPROGRESS)
	return -1;

    FD_ZERO(&mask);
    FD_SET(sock, &mask);

    switch (select(sock + 1, NULL, &mask, NULL, &timeout)) {
    case 0:
	errno = ETIMEDOUT;
	/* drop thru */
    case -1:
	return -1;
    default:
	optlen = sizeof(optval);
	(void)getsockopt(sock, SOL_SOCKET, SO_ERROR, &optval, &optlen);
	if (optval != 0) {
	    errno = optval;
	    return -1;
	}
	break;
    }

    return fcntl(sock, F_SETFL, flags);
}
