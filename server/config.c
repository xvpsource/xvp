/*
 * config.c - configuration handling for Xen VNC Proxy
 *
 * Copyright (C) 2009-2010, Colin Dean
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "xvp.h"

#define XVP_CONFIG_MAX_DEPTH 5
#define XVP_CONFIG_MAX_WORDS 10
#define XVP_CONFIG_LINEBUF_SIZE 4096

typedef enum {
    XVP_CONFIG_STATE_DATABASE,
    XVP_CONFIG_STATE_OTP,
    XVP_CONFIG_STATE_MULTIPLEX,
    XVP_CONFIG_STATE_POOL,
    XVP_CONFIG_STATE_DOMAIN,
    XVP_CONFIG_STATE_MANAGER,
    XVP_CONFIG_STATE_HOST,
    XVP_CONFIG_STATE_GROUP,
    XVP_CONFIG_STATE_VM
} xvp_config_state;

char     *xvp_config_filename = XVP_CONFIG_FILENAME;
xvp_pool *xvp_pools = NULL;
xvp_vm   *xvp_multiplex_vm = NULL;

static int   xvp_config_depth = 0;
static char *xvp_config_filenames[XVP_CONFIG_MAX_DEPTH];
static FILE *xvp_config_streams[XVP_CONFIG_MAX_DEPTH];
static int   xvp_config_linenums[XVP_CONFIG_MAX_DEPTH];

static void xvp_config_bad(void)
{
    xvp_log(XVP_LOG_FATAL, "%s: Syntax error at line %d",
	    xvp_config_filenames[xvp_config_depth],
	    xvp_config_linenums[xvp_config_depth]);
}

static int xvp_config_parse_line(char *line, char **wordv, int *linenum)
{
    char *word;
    int wordc = 0;
    bool quoted = false;

    FILE *stream   = xvp_config_streams[xvp_config_depth];
    char *filename = xvp_config_filenames[xvp_config_depth];

    if (!fgets(line, XVP_CONFIG_LINEBUF_SIZE, stream)) {

	if (ferror(stream))
	    xvp_log_errno(XVP_LOG_FATAL, "%s", filename);

	if (feof(stream)) {
	    xvp_log(XVP_LOG_DEBUG, "Closing config file %s", filename);
	    fclose(stream);
	    xvp_free(xvp_config_filenames[xvp_config_depth]);
	    if (xvp_config_depth == 0)
		return -1;
	    xvp_config_depth--;
	    return 0;
	}
    }

    *linenum = ++xvp_config_linenums[xvp_config_depth];

    if ((word = strchr(line, '#')) ||
	(word = strchr(line, '\r')) || (word = strchr(line, '\n')))
	*word = '\0';

    for (wordc = 0; wordc < XVP_CONFIG_MAX_WORDS; wordc++) {
	while (*line == ' ' || *line == '\t')
	    line++;
	if (*line == '"') {
	    line++;
	    quoted = true;
	}
	if (!*line)
	    goto parsed;
	wordv[wordc] = line;
	if (quoted) {
	    while (*line != '\0' && *line != '"')
		line++;
	    if (!*line) {
		wordc++;
		goto parsed;
	    }
	    *line++ = '\0';
	    quoted = false;
	} else {
	    while (*line != '\0' && *line != ' ' && *line != '\t')
		line++;
	    if (!*line) {
		wordc++;
		goto parsed;
	    }
	    *line++ = '\0';
	}
    }

    xvp_config_bad();

 parsed:

    if (wordc != 2 || strcmp(wordv[0], "INCLUDE")) 
	return wordc;

    if (xvp_config_depth + 1 >= XVP_CONFIG_MAX_DEPTH)
	xvp_log(XVP_LOG_FATAL, "%s: Too many levels of INCLUDE at line %d",
		filename, *linenum);

    xvp_log(XVP_LOG_DEBUG, "Including config file %s", wordv[1]);

    if (!(stream = fopen(wordv[1], "r")))
	xvp_log_errno(XVP_LOG_FATAL, "%s", wordv[1]);

    xvp_config_depth++;
    xvp_config_filenames[xvp_config_depth] = xvp_strdup(wordv[1]);
    xvp_config_streams[xvp_config_depth]   = stream;
    xvp_config_linenums[xvp_config_depth]  = 0;

    return 0;
}

void xvp_config_init(void)
{
    static bool scanned = false;
    char line[XVP_CONFIG_LINEBUF_SIZE], *wordv[XVP_CONFIG_MAX_WORDS];
    int linenum = 0, wordc, port, i, len, wordn;
    FILE *stream;
    xvp_config_state state = XVP_CONFIG_STATE_DATABASE;
    xvp_pool *pool, *new_pool;
    xvp_host *host, *new_host;
    char *hostname, *address;
    char *xvp_otp_text = NULL;
    char *xvp_ipcheck_text = NULL;

    xvp_vm *vm, *new_vm;

    xvp_otp_mode    = XVP_OTP_MODE;
    xvp_otp_ipcheck = XVP_OTP_IPCHECK;
    xvp_otp_window  = XVP_OTP_WINDOW;

    if (scanned) {
	xvp_log(XVP_LOG_INFO, "Re-reading config file on signal");

	if (xvp_multiplex_vm) {
	    xvp_free(xvp_multiplex_vm);
	    xvp_multiplex_vm = NULL;
	}

	while (xvp_pools) {
	    pool = xvp_pools;
	    xvp_pools = pool->next;
	    while (pool->hosts) {
		host = pool->hosts;
		pool->hosts = host->next;
		xvp_free(host);
	    }
	    while (pool->vms) {
		vm = pool->vms;
		pool->vms = vm->next;
		xvp_free(vm);
	    }
	    xvp_free(pool);
	}

    } else {
	xvp_log(XVP_LOG_DEBUG, "Reading config file %s", xvp_config_filename);
    }


    if (!(stream = fopen(xvp_config_filename, "r")))
	xvp_log_errno(XVP_LOG_FATAL, "%s", xvp_config_filename);

    xvp_config_depth        = 0;
    xvp_config_filenames[0] = xvp_strdup(xvp_config_filename);
    xvp_config_streams[0]   = stream;
    xvp_config_linenums[0]  = 0;

    new_pool = NULL;
    while (true) {

	if ((wordc = xvp_config_parse_line(line, wordv, &linenum)) == 0)
	    continue;
	else if (wordc < 0)
	    break;

	switch (state) {

	case XVP_CONFIG_STATE_DATABASE: /* DATABASE dsn [username [password]] */
	    if (strcmp(wordv[0], "DATABASE"))
		goto xvp_config_state_otp;
	    if (wordc < 2 || wordc > 4)
		xvp_config_bad();
	    /* used by xvpweb, not relevant to us here, so pass over */
	    state = XVP_CONFIG_STATE_OTP;
	    break;
		
	case XVP_CONFIG_STATE_OTP: /* OTP REQUIRE|ALLOW|DENY [IPCHECK ON|OFF|HTTP] [ window ] */
	xvp_config_state_otp:
	    if (strcmp(wordv[0], "OTP"))
		goto xvp_config_state_multiplex;
	    if (wordc < 2 || wordc > 5)
		xvp_config_bad();
	    if (!strcmp(wordv[1], "DENY"))
		xvp_otp_mode = XVP_OTP_DENY;
	    else if (!strcmp(wordv[1], "ALLOW"))
		xvp_otp_mode = XVP_OTP_ALLOW;
	    else if (!strcmp(wordv[1], "REQUIRE"))
		xvp_otp_mode = XVP_OTP_REQUIRE;
	    else
		xvp_config_bad();
	    if (wordc >= 3 && !strcmp(wordv[2], "IPCHECK")) {
		if (!strcmp(wordv[3], "OFF"))
		    xvp_otp_ipcheck = XVP_IPCHECK_OFF;
		else if (!strcmp(wordv[3], "ON"))
		    xvp_otp_ipcheck = XVP_IPCHECK_ON;
		else if (!strcmp(wordv[3], "HTTP"))
		    xvp_otp_ipcheck = XVP_IPCHECK_HTTP;
		else
		    xvp_config_bad();
		wordn = 4;
	    } else {
		wordn = 2;
	    }
	    if (wordc == wordn + 1) {
		xvp_otp_window = atoi(wordv[wordn]);
		if (xvp_otp_window < 1 || xvp_otp_window > XVP_OTP_MAX_WINDOW)
		    xvp_config_bad();
	    }
	    state = XVP_CONFIG_STATE_MULTIPLEX;
	    break;

	case XVP_CONFIG_STATE_MULTIPLEX: /* MULTIPLEX port */
	xvp_config_state_multiplex:
	    if (strcmp(wordv[0], "MULTIPLEX"))
		goto xvp_config_state_pool;
	    if (wordc != 2)
		xvp_config_bad();
	    if (*wordv[1] == ':') {
		port = atoi(wordv[1] + 1) + XVP_VNC_PORT_MIN;
		if (port < XVP_VNC_PORT_MIN || port > XVP_VNC_PORT_MAX)
		    xvp_config_bad();
	    } else {
		port = atoi(wordv[1]);
		if (port < 1024 || port > 65535)
		    xvp_config_bad();
	    }
	    xvp_multiplex_vm = xvp_alloc(sizeof(xvp_vm));
	    xvp_multiplex_vm->sock = -1;
	    xvp_multiplex_vm->port = port;
	    strcpy(xvp_multiplex_vm->vmname, "[multiplexer]");
	    state = XVP_CONFIG_STATE_POOL;
	    break;

	case XVP_CONFIG_STATE_POOL: /* POOL poolname */
	xvp_config_state_pool:
	    if (strcmp(wordv[0], "POOL") || wordc < 2)
		xvp_config_bad();
	    /* Pool name may contain spaces but not ":" */
	    for (len = 0, i = 1, *line = '\0'; i < wordc; i++) {
		len += strlen(wordv[i]) + 1;
		if (len > XVP_MAX_POOL)
		    xvp_config_bad();
		strcat(line, " ");
		// strcat(line, wordv[i]) unsafe, they may overlap
		memmove(line + strlen(line), wordv[i], strlen(wordv[i]) + 1);
	    }
	    if (strchr(line + 1, ':'))
		xvp_config_bad();
	    if (xvp_config_pool_by_name(line + 1))
		xvp_log(XVP_LOG_FATAL,
			"%s: Duplicate pool name at line %d",
			xvp_config_filenames[xvp_config_depth], linenum);
	    new_pool = xvp_alloc(sizeof(xvp_pool));
	    strcpy(new_pool->poolname, line + 1);
	    if (pool = xvp_config_last_pool())
		pool->next = new_pool;
	    else
		xvp_pools = new_pool;
	    state = XVP_CONFIG_STATE_DOMAIN;
	    break;

	case XVP_CONFIG_STATE_DOMAIN: /* DOMAIN this.that.com */
	    if (strcmp(wordv[0], "DOMAIN") || wordc != 2 ||
		strlen(wordv[1] + 1) > XVP_MAX_HOSTNAME)
		xvp_config_bad();
	    if (*wordv[1]) {
		/* store with leading dot for ease of manipulation */
		*new_pool->domainname = '.';
		strcpy(new_pool->domainname + 1, wordv[1]);
	    }
	    state = XVP_CONFIG_STATE_MANAGER;
	    break;

	case XVP_CONFIG_STATE_MANAGER: /* MANAGER username xen-password */
	    if (strcmp(wordv[0], "MANAGER") || wordc != 3 ||
		strlen(wordv[1]) > XVP_MAX_MANAGER ||
		strlen(wordv[2]) != XVP_MAX_XEN_PW * 2 ||
		!xvp_password_text_to_hex(wordv[2], new_pool->password,
					  XVP_PASSWORD_XEN))
		xvp_config_bad();
	    strcpy(new_pool->manager, wordv[1]);
	    state = XVP_CONFIG_STATE_HOST;
	    break;

	case XVP_CONFIG_STATE_HOST: /* HOST hostname */
	    if (strcmp(wordv[0], "HOST")) {
		if (new_pool->hosts)
		    goto xvp_config_state_group;
		xvp_config_bad();
	    }
	    switch (wordc) {
	    case 2:
		address = "";
		hostname = wordv[1];
		break;
	    case 3:
		if (!xvp_is_ipv4(wordv[1]))
		    xvp_config_bad();
		address = wordv[1];
		hostname = wordv[2];
		break;
	    default:
		xvp_config_bad();
		break;
	    }
	    if (strlen(hostname) > XVP_MAX_HOSTNAME)
		xvp_config_bad();
	    if (xvp_config_host_by_name(new_pool, hostname))
		xvp_log(XVP_LOG_FATAL,
			"%s: Duplicate host name at line %d",
			xvp_config_filenames[xvp_config_depth], linenum);
		
	    new_host = xvp_alloc(sizeof(xvp_host));
	    new_host->pool = new_pool;
	    strcpy(new_host->address, address);
	    strcpy(new_host->hostname, hostname);
	    new_host->hostname_is_ipv4 = xvp_is_ipv4(new_host->hostname);

	    if (host = xvp_config_last_host(new_pool))
		host->next = new_host;
	    else
		new_pool->hosts = new_host;
	    break;

	case XVP_CONFIG_STATE_GROUP: /* GROUP groupname */
	xvp_config_state_group:
	    if (strcmp(wordv[0], "GROUP"))
		goto xvp_config_state_vm;
	    if (wordc < 2)
		xvp_config_bad();
	    /* used by xvpweb, not relevant to us here, so pass over */
	    state = XVP_CONFIG_STATE_VM;
	    break;

	case XVP_CONFIG_STATE_VM: /* VM port vmname vnc-password */
	xvp_config_state_vm:
	    if (!strcmp(wordv[0], "GROUP"))
		goto xvp_config_state_group;
	    if (strcmp(wordv[0], "VM")) {
		if (new_pool->vms)
		    goto xvp_config_state_pool;
		xvp_config_bad();
	    }
	    new_vm = xvp_alloc(sizeof(xvp_vm));
	    if (wordc != 4 ||
		strlen(wordv[2]) > XVP_MAX_HOSTNAME ||
		strlen(wordv[3]) != XVP_MAX_VNC_PW * 2 ||
		!xvp_password_text_to_hex(wordv[3], new_vm->password,
					  XVP_PASSWORD_VNC))
		xvp_config_bad();
	    /*
	     * VNC convention is display 0 = port 5900, and on up, but going
	     * beyond 5999 is probably daft as >= 6000 is for X Window System.
	     * So we enforce :0 to :99 when ":" is used.  However, for those
	     * who need more than 100 VMs, we allow any explicit port number
	     * without the ":", as long as it's not a reserved port < 1024,
	     * and leave it to them to ensure their choices are sensible.
	     * Port "-" means no VM-specific port, using multiplex port only.
	     */
	    if (!strcmp(wordv[1], "-")) {
		if (!xvp_multiplex_vm)
		    xvp_config_bad();
		port = 0;
	    } else if (*wordv[1] == ':') {
		port = atoi(wordv[1] + 1) + XVP_VNC_PORT_MIN;
		if (port < XVP_VNC_PORT_MIN || port > XVP_VNC_PORT_MAX)
		    xvp_config_bad();
	    } else {
		port = atoi(wordv[1]);
		if (port < 1024 || port > 65535)
		    xvp_config_bad();
	    }

	    if (xvp_config_vm_by_name(new_pool, wordv[2]))
		xvp_log(XVP_LOG_FATAL,
			"%s: Duplicate vm name at line %d",
			xvp_config_filenames[xvp_config_depth], linenum);

	    if (xvp_config_vm_by_port(port))
		xvp_log(XVP_LOG_FATAL,
			"%s: Duplicate port number at line %d",
			xvp_config_filenames[xvp_config_depth], linenum);

	    new_vm->pool = new_pool;
	    new_vm->port = port;
	    new_vm->sock = -1;
	    if (xvp_xenapi_is_uuid(wordv[2])) {
		strcpy(new_vm->uuid, wordv[2]);
		strcpy(new_vm->vmname, "uuid=");
		strcat(new_vm->vmname, wordv[2]);
	    } else {
		strcpy(new_vm->vmname, wordv[2]);
	    }
	    if (vm = xvp_config_last_vm(new_pool))
		vm->next = new_vm;
	    else
		new_pool->vms = new_vm;
	    break;
	}
    }

    if (!new_pool || !new_pool->vms)
	xvp_log(XVP_LOG_FATAL, "%s: Unexpected end of file at line %d",
		xvp_config_filename, xvp_config_linenums[0]);

    switch (xvp_otp_mode) {
    case XVP_OTP_DENY:
	xvp_otp_text = "DENY";
	break;
    case XVP_OTP_ALLOW:
	xvp_otp_text = "ALLOW";
	break;
    case XVP_OTP_REQUIRE:
	xvp_otp_text = "REQUIRE";
	break;
    }
    switch (xvp_otp_ipcheck) {
    case XVP_IPCHECK_OFF:
	xvp_ipcheck_text = "OFF";
	break;
    case XVP_IPCHECK_ON:
	xvp_ipcheck_text = "ON";
	break;
    case XVP_IPCHECK_HTTP:
	xvp_ipcheck_text = "HTTP";
	break;
    }
    xvp_log(XVP_LOG_DEBUG, "> OTP %s IPCHECK %s %d",
	    xvp_otp_text, xvp_ipcheck_text, xvp_otp_window);
    if (xvp_multiplex_vm)
	xvp_log(XVP_LOG_DEBUG, "> MULTIPLEX %d", xvp_multiplex_vm->port);
    for (pool = xvp_pools; pool; pool = pool->next) {
	xvp_log(XVP_LOG_DEBUG, "> POOL \"%s\"", pool->poolname);
	xvp_log(XVP_LOG_DEBUG, ">   DOMAIN \"%s\"", *pool->domainname ?
		pool->domainname + 1 : "");
	xvp_log(XVP_LOG_DEBUG, ">   MANAGER \"%s\"", pool->manager);
	for (host = pool->hosts; host; host = host->next) {
	    if (host->address[0])
		xvp_log(XVP_LOG_DEBUG, ">   HOST %s \"%s\"",
			host->address, host->hostname);
	    else
		xvp_log(XVP_LOG_DEBUG, ">   HOST \"%s\"", host->hostname);
	}
	for (vm = pool->vms; vm; vm = vm->next)
	    if (vm->port)
		xvp_log(XVP_LOG_DEBUG, ">   VM %d %s", vm->port, vm->vmname);
	    else
		xvp_log(XVP_LOG_DEBUG, ">   VM - %s", vm->vmname);
    }

    scanned = true;
}

xvp_pool *xvp_config_last_pool(void)
{
    xvp_pool *pool;

    for (pool = xvp_pools; pool && pool->next; pool = pool->next)
	/* empty */;

    return pool;
}

xvp_pool *xvp_config_pool_by_name(char *poolname)
{
    xvp_pool *pool;

    for (pool = xvp_pools; pool; pool = pool->next)
	if (!strcmp(pool->poolname, poolname))
	    return pool;

    return NULL;
}

xvp_host *xvp_config_last_host(xvp_pool *pool)
{
    xvp_host *host;

    for (host = pool->hosts; host && host->next; host = host->next)
	/* empty */;

    return host;
}

xvp_host *xvp_config_host_by_name(xvp_pool *pool, char *hostname)
{
    xvp_host *host;

    if (!pool) {
	for (pool = xvp_pools; pool; pool = pool->next)
	    if (host = xvp_config_host_by_name(pool, hostname))
		return host;
	return NULL;
    }

    for (host = pool->hosts; host; host = host->next)
	if (!strcmp(host->hostname, hostname))
	    return host;

    return NULL;
}

xvp_vm *xvp_config_last_vm(xvp_pool *pool)
{
    xvp_vm *vm;

    for (vm = pool->vms; vm && vm->next; vm = vm->next)
	/* empty */;

    return vm;
}

xvp_vm *xvp_config_vm_by_name(xvp_pool *pool, char *vmname)
{
    xvp_vm *vm;

    if (!pool) {
	for (pool = xvp_pools; pool; pool = pool->next)
	    if (vm = xvp_config_vm_by_name(pool, vmname))
		return vm;
	return NULL;
    }

    for (vm = pool->vms; vm; vm = vm->next)
	if (!strcmp(vm->vmname, vmname))
	    return vm;

    return NULL;
}

xvp_vm *xvp_config_vm_by_uuid(xvp_pool *pool, char *uuid)
{
    xvp_vm *vm;

    if (!pool) {
	for (pool = xvp_pools; pool; pool = pool->next)
	    if (vm = xvp_config_vm_by_uuid(pool, uuid))
		return vm;
	return NULL;
    }

    for (vm = pool->vms; vm; vm = vm->next)
	if (!strncmp(vm->vmname, "uuid=", 5) && !strcmp(vm->vmname + 5, uuid))
	    return vm;

    return NULL;
}

xvp_vm *xvp_config_vm_by_port(int port)
{
    xvp_pool *pool;
    xvp_vm *vm;

    if (!port)
	return NULL;

    if (xvp_multiplex_vm && xvp_multiplex_vm->port == port)
	return xvp_multiplex_vm;

    for (pool = xvp_pools; pool; pool = pool->next)
	for (vm = pool->vms; vm; vm = vm->next)
	    if (vm->port == port)
		return vm;

    return NULL;
}

xvp_vm *xvp_config_vm_by_sock(int sock)
{
    xvp_pool *pool;
    xvp_vm *vm;

    if (xvp_multiplex_vm && xvp_multiplex_vm->sock == sock)
	return xvp_multiplex_vm;

    for (pool = xvp_pools; pool; pool = pool->next)
	for (vm = pool->vms; vm; vm = vm->next)
	    if (vm->sock == sock)
		return vm;

    return NULL;
}
