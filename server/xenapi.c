/*
 * xenapi.c - Xen API handling for Xen VNC Proxy
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
 *
 * The call_func() and write_func() functions here were taken from the file
 * test_event_handling.c, supplied in the XenServer C SDK 5.0.0, and to
 * which the following copyright statement applies:
 *
 * Copyright (c) 2006-2008 Citrix Systems, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * We initially use the XenServer API to connect to one of the servers in
 * the pool: if it's not the master, it will tell us which server is, and
 * we connect to that instead.  If we get no response, we try another server
 * from the pool, until we've tried all the pool servers or have found the
 * master.
 *
 * We then find the VM with the right name, and the location of its console.
 * The latter is returned by the API as something like:
 *
 *    https://192.168.0.1/console?ref=OpaqueRef:a7529ed1....
 *
 * We then connect to the RFB stream by initially making an SSL connection
 * to port 443 on the server address specified, and sending an HTTP header
 * of the form:
 *
 *   CONNECT /console?ref=OpaqueRef:a7529ed1....&session_id=....
 *
 * where we get the Session ID from the initial Xen session login.  If we
 * get a success (HTTP 200) header line back (and subsequent header lines
 * ending with a blank line, all of which we can ignore), then we can
 * switch to using RFB over the SSL connection (commencing with the usual
 * RFB protocol version handshake.
 *
 * In order to reliably discover when a console goes away (e.g. on VM
 * shutdown, reboot, or migrate), we register ourselves with the API to
 * receive console events.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libxml/parser.h>
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <xen/api/xen_all.h>

#include "xvp.h"

#define XVP_XENAPI_BUFLEN 256

typedef struct { /* from XenServer C SDK 5.0.0 */
    xen_result_func func;
    void *handle;
} xen_comms;

static char  xvp_xenapi_host_url[XVP_MAX_HOSTNAME + 9]; /* https://hostname */
static char  xvp_xenapi_console_url[XVP_XENAPI_BUFLEN]; /* see above */
static xen_vm_set      *xvp_xenapi_vmset = NULL;
static xen_console_set *xvp_xenapi_cset = NULL;
static xen_session     *xvp_xenapi_session = NULL;
static xen_console     *xvp_xenapi_console = NULL;

bool xvp_vm_is_host = false;

/* from XenServer C SDK 5.0.0 */
static size_t write_func(void *ptr, size_t size, size_t nmemb, xen_comms *comms)
{
    size_t n = size * nmemb;

    return comms->func(ptr, n, comms->handle) ? n : 0;
}

/* from XenServer C SDK 5.0.0 */
static int call_func(const void *data, size_t len, void *user_handle,
		     void *result_handle, xen_result_func result_func)
{
    (void)user_handle;

    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    xen_comms comms = {
        .func = result_func,
        .handle = result_handle
    };

    curl_easy_setopt(curl, CURLOPT_URL, xvp_xenapi_host_url);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
#ifdef CURLOPT_MUTE
    curl_easy_setopt(curl, CURLOPT_MUTE, 1);
#endif
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_func);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &comms);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

    CURLcode result = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    return result;
}

static void xvp_xenapi_cleanup(xen_session *session)
{
    if (xvp_xenapi_cset) {
	xen_console_set_free(xvp_xenapi_cset);
	xvp_xenapi_cset = NULL;
    }
    if (xvp_xenapi_vmset) {
	xen_vm_set_free(xvp_xenapi_vmset);
	xvp_xenapi_vmset = NULL;
    }

    curl_global_cleanup();
    xmlCleanupParser();
    xen_session_logout(session);
    xen_fini();

    xvp_xenapi_session = NULL;
}

static char *xvp_xenapi_error_code(xen_session *session)
{
    if (session->error_description_count < 1)
	return "NO_ERROR";
    return session->error_description[0];
}

static bool xvp_xenapi_session_failure(xen_session *session)
{
    int i;
    char buf[XVP_XENAPI_BUFLEN];

    strcpy(buf, "Xen API error:");
    for (i = 0; i < session->error_description_count; i++) {
	strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
	strncat(buf, session->error_description[i],
		sizeof(buf) - strlen(buf) - 1);
    }

    xvp_log(XVP_LOG_ERROR, buf);
    xvp_xenapi_cleanup(session);
    return false;
}

static char *xvp_xenapi_ip_to_hostname(xen_session *session, char *ip)
{
    xen_host_set *host_set;
    int i;
    bool matched;
    char *address, *hostname = NULL;

    /*
     * To connect to console of a host whose IP address we know, we need
     * to find its hostname as know to XenServer, which may not be the
     * same as gethostbyaddr() would tell us.
     */

    if (!xen_host_get_all(session, &host_set))
	return NULL;

    for (i = 0; i < host_set->size; i++) {
	if (!xen_host_get_address(session, &address, host_set->contents[i]))
	    continue;
	matched = (strcmp(address, ip) == 0);
	xvp_free(address);
	if (matched) {
	    xen_host_get_hostname(session, &hostname, host_set->contents[i]);
	    break;
	}
    }

    xen_host_set_free(host_set);
    return hostname;
}

static xen_session *xvp_xenapi_get_console(xvp_vm *vm)
{
    xvp_pool *pool = vm->pool;
    xvp_host *host;
    char password[XVP_MAX_XEN_PW + 1], escpassword[256];
    xen_session *session = NULL;
    struct xen_string_set *classes;
    int i;
    enum xen_console_protocol protocol;
    char *location, *domainname;

    if (session = xvp_xenapi_session)
	goto have_session;

    curl_global_init(CURL_GLOBAL_ALL);
    xmlInitParser();
    xen_init();
    memset(password, 0, sizeof(password));

    for (host = pool->hosts; host; host = host->next) {

	if (session)
	    xen_session_logout(session);

	if (host->address[0]) {
	    sprintf(xvp_xenapi_host_url, "https://%s", host->address);
	} else {
	    domainname = host->hostname_is_ipv4 ? "" : pool->domainname;
	    sprintf(xvp_xenapi_host_url, "https://%s%s",
		    host->hostname, domainname);
	}
	xvp_log(XVP_LOG_DEBUG, "Trying host %s", xvp_xenapi_host_url + 8);

	xvp_password_decrypt(pool->password, password, XVP_PASSWORD_XEN);
	(void)xvp_xmlescape(password, escpassword, sizeof(escpassword));
	
	session = xen_session_login_with_password(call_func, NULL,
		      pool->manager, escpassword, xen_api_version_1_5);
	if (!session->ok &&
	    session->error_description_count > 0 &&
	    !strcmp(session->error_description[0], "HOST_IS_SLAVE")) {
	    xvp_log(XVP_LOG_DEBUG, "Redirected to %s",
		    session->error_description[1]); 
	    sprintf(xvp_xenapi_host_url, "https://%s",
		    session->error_description[1]);
	    xen_session_logout(session);
	    session = xen_session_login_with_password(call_func, NULL,
			  pool->manager, escpassword, xen_api_version_1_5);
	}

	memset(password, 0, sizeof(password));
	memset(escpassword, 0, sizeof(escpassword));

	if (session->ok || (session->error_description_count > 0 &&
	    !strcmp(session->error_description[0],
		    "SESSION_AUTHENTICATION_FAILED")))
	    break;
    }	

    if (!session->ok) {
	xvp_xenapi_session_failure(session);
	return NULL;
    }

    xvp_log(XVP_LOG_DEBUG, "Xen API session established to %s",
	    xvp_xenapi_host_url);

 have_session:

    for (host = pool->hosts; host; host = host->next) {
	if (!strcmp(host->hostname, vm->vmname) ||
	    !strcmp(host->address, vm->vmname)) {
	    xvp_vm_is_host = true;
	    break;
	}
    }

    if (host) {
	char label[XVP_MAX_HOSTNAME + 32], *hostname;
	if (host->address[0]) {
	    hostname = xvp_xenapi_ip_to_hostname(session, host->address);
	    if (!hostname) {
		xvp_xenapi_session_failure(session);
		return NULL;
	    }
	    sprintf(label, "Control domain on host: %s", hostname);
	    xvp_free(hostname);
	} else {
	    sprintf(label, "Control domain on host: %s%s",
		    host->hostname, pool->domainname);
	}
	xvp_log(XVP_LOG_DEBUG, "%s", label);
	if (!xen_vm_get_by_name_label(session, &xvp_xenapi_vmset, label)) {
	    xvp_xenapi_session_failure(session);
	    return NULL;
	}
    } else if (*vm->uuid) {
	xen_vm xvm;
	char *label;
	if (!xen_vm_get_by_uuid(session, &xvm, vm->uuid) ||
	    !xen_vm_get_name_label(session, &label, xvm)) {
	    xvp_xenapi_session_failure(session);
	    return NULL;
	}
	xvp_xenapi_vmset = xen_vm_set_alloc(1);
	xvp_xenapi_vmset->contents[0] = xvm;
	strncpy(vm->vmname, label, XVP_MAX_HOSTNAME);
	xvp_free(label);
	xvp_log(XVP_LOG_DEBUG, "VM name label: %s", vm->vmname);
    } else {
	char escvmname[256];
	if (!xen_vm_get_by_name_label(session, &xvp_xenapi_vmset,
		xvp_xmlescape(vm->vmname, escvmname, sizeof(escvmname)))) {
	    xvp_xenapi_session_failure(session);
	    return NULL;
	}
    }
    
    if (xvp_xenapi_vmset->size == 0) {
	xvp_log(XVP_LOG_ERROR, "%s: VM not found", vm->vmname);
	xvp_xenapi_cleanup(session);
	return NULL;
    } else if (xvp_xenapi_vmset->size > 1) {
	xvp_log(XVP_LOG_ERROR, "%s: Multiple VMs with same name", vm->vmname);
	return NULL;
    }	

    if (!xen_vm_get_consoles(session, &xvp_xenapi_cset,
			     xvp_xenapi_vmset->contents[0])) {
	xvp_xenapi_session_failure(session);
	return NULL;
    }

    for (i = 0, location = NULL; i < xvp_xenapi_cset->size; i++) {
	if (xen_console_get_protocol(session, &protocol,
				     xvp_xenapi_cset->contents[i]) &&
	    protocol == XEN_CONSOLE_PROTOCOL_RFB) {
	    if (!xen_console_get_location(session, &location,
					  xvp_xenapi_cset->contents[i])) {
		xvp_xenapi_session_failure(session);
		return NULL;
	    }
	    xvp_xenapi_console = xvp_xenapi_cset->contents[i];
	    break;
	}
    }

    if (!location) {
	xvp_log(XVP_LOG_ERROR, "%s: Console not found", vm->vmname);
	xvp_xenapi_cleanup(session);
	return NULL;
    }

    if (strlen(location) >= XVP_XENAPI_BUFLEN) {
	xvp_log(XVP_LOG_ERROR, "%s: Console URL too long\n", vm->vmname);
	xvp_xenapi_cleanup(session);
	return NULL;
    }

    classes = xen_string_set_alloc(1);
    classes->contents[0] = xvp_strdup("console");

    if (!(xen_event_register(session, classes)))
	xvp_xenapi_session_failure(session);
    xen_string_set_free(classes);

    xvp_log(XVP_LOG_DEBUG, "Xen API console location: %s", location);

    strcpy(xvp_xenapi_console_url, location);
    /* free(location); not sure if this is valid */

    return session;
}

static char *xvp_xenapi_get_header_line(SSL *ssl)
{
    static char buf[XVP_XENAPI_BUFLEN];
    char *bp;
    int len;

    /*
     * Not optimally efficient to request 1 byte at a time, but there
     * shouldn't be more than a handful of header lines, and it's
     * simpler if we can avoid overshooting into RFB handshaking.
     */
    for (bp = buf; bp < buf + sizeof(buf) - 1; bp++) {
	if ((len = SSL_read(ssl, bp, 1)) != 1)
	    return NULL;
	if (*bp == '\r') {
	    bp--;
	} else if (*bp == '\n') {
	    *bp = '\0';
	    return buf;
	}
    }

    return NULL;
}

static SSL *xvp_xenapi_connect_to_rfb(xen_session *session)
{
    char buf[XVP_XENAPI_BUFLEN], ip[XVP_XENAPI_BUFLEN], uri[XVP_XENAPI_BUFLEN];
    char *line;
    int sock, len;
    unsigned int major, minor;
    struct sockaddr_in connect_addr;
    SSL_METHOD *method;
    SSL_CTX *ctx;
    SSL *ssl;
    BIO *bio;

    if (sscanf(xvp_xenapi_console_url, "https://%[^/]/%s", ip, uri) != 2) {
	xvp_log(XVP_LOG_ERROR, "Failed to parse console location");
	return NULL;
    }

    connect_addr.sin_family = AF_INET;
    connect_addr.sin_addr.s_addr = inet_addr(ip);
    connect_addr.sin_port = htons(443);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
	xvp_log_errno(XVP_LOG_ERROR, "socket");
	return NULL;
    }

    if (connect(sock, (struct sockaddr *)&connect_addr,
		sizeof(connect_addr)) != 0) {
	xvp_log_errno(XVP_LOG_ERROR, "connect");
	return NULL;
    }

    SSL_library_init();
    SSL_load_error_strings();

    method = (SSL_METHOD *)SSLv23_client_method();
    if (!(ctx = SSL_CTX_new(method))) {
	xvp_log(XVP_LOG_ERROR, "SSL_ctx_new: Failed");
	return NULL;
    }
    ssl = SSL_new(ctx);
    bio = BIO_new_socket(sock, BIO_NOCLOSE);
    SSL_set_bio(ssl, bio, bio);

    if (SSL_connect(ssl) <= 0) {
	xvp_log(XVP_LOG_ERROR, "SSL_connect: Failed");
	return NULL;
    }

    sprintf(buf, "CONNECT /%s&session_id=%s HTTP/1.0\r\n\r\n",
	    uri, session->session_id);
    len = strlen(buf);

    if (SSL_write(ssl, buf, len) <= 0) {
	xvp_log(XVP_LOG_ERROR, "SSL_write: Failed");
	return NULL;
    }

    if (!(line = xvp_xenapi_get_header_line(ssl))) {
	xvp_log(XVP_LOG_ERROR, "Failed to read/parse header");
	return NULL;
    }

    if (strcmp(line, "HTTP/1.1 200 OK") != 0) {
	xvp_log(XVP_LOG_ERROR, "Failure code: %s", line);
	return NULL;
    }

    do {
	if (!(line = xvp_xenapi_get_header_line(ssl))) {
	    xvp_log(XVP_LOG_ERROR, "Failed to read/parse header");
	    return NULL;
	}
    } while (*line);

    xvp_log(XVP_LOG_DEBUG, "Connected to console");
    return ssl;
}

void *xvp_xenapi_open_stream(xvp_vm *vm)
{
    xen_session *session;
    SSL *ssl;

    if (!(session = xvp_xenapi_get_console(vm)))
	return NULL;

    ssl = xvp_xenapi_connect_to_rfb(session);
    
#if 0
    /* keep session for event handling */
    xvp_xenapi_cleanup(session);
#endif

    xvp_xenapi_session = session;
    return (void *)ssl;
}

bool xvp_xenapi_event_wait(xvp_vm *vm)
{
    xen_session *session = xvp_xenapi_session;
    struct xen_event_record_set *events;
    xen_event_record *event;
    char *uuid;
    int i;

    while (true) {
	
        if (!xen_event_next(session, &events)) {
	    xvp_xenapi_session_failure(session);
	    break;
	}

        for (i = 0; i < events->size; i++) {
            event = events->contents[i];

            if (event->operation == XEN_EVENT_OPERATION_DEL) {
		/*
		 * Is it our console that's been deleted?
		 *
		 * XenServer C API 5.0.0-2 doesn't set event->obj_uuid, but
		 * does set event->ref to "OpaqueRef:UUID" which will be the
		 * same as our console handle pointer coerced to a string!
		 */
		if (!strcmp(event->ref, (char *)xvp_xenapi_console)) {
		    xvp_log(XVP_LOG_DEBUG, "Console deleted by server");
		    xen_event_record_set_free(events);
		    return true;
		}
	    }
        }

        xen_event_record_set_free(events);
    }

    return false;
}

bool xvp_xenapi_handle_message_code(int code)
{
    char *text = xvp_message_code_to_text(code);
    xen_session *session;
    xen_vm xvm;
    bool ok = false, ha;

    xvp_log(XVP_LOG_INFO, "Client %s request received", text); 

    if ((session = xvp_xenapi_session) && xvp_xenapi_vmset &&
	xvp_xenapi_vmset->size == 1) {

	xvm = xvp_xenapi_vmset->contents[0];

	switch (code) {
	case XVP_MESSAGE_CODE_SHUTDOWN:
	    if (xen_vm_get_ha_always_run(session, &ha, xvm) && ha) {
		xvp_log(XVP_LOG_DEBUG, "Disabling HA prior to shutdown");
		xen_vm_set_ha_always_run(session, xvm, false);
	    }
	    ok = xen_vm_clean_shutdown(session, xvm);
	    break;
	case XVP_MESSAGE_CODE_REBOOT:
	    ok = xen_vm_clean_reboot(session, xvm);
	    break;
	case XVP_MESSAGE_CODE_RESET:
	    ok = xen_vm_hard_reboot(session, xvm);
	    break;
	default:
	    ok = false;
	    break;
	}	
    }

    if (!ok) {
	xvp_log(XVP_LOG_ERROR, "Client %s request failed: %s",
		text, xvp_xenapi_error_code(session));
	xen_session_clear_error(session);
	return false;
    }

    xvp_log(XVP_LOG_INFO, "Client %s request succeeded", text);
    return true;
}

bool xvp_xenapi_is_uuid(char *text)
{
    int dashes[] = XVP_UUID_DASHES;
    int i, j;
    bool mustdash;

    if (strlen(text) != XVP_UUID_LEN)
	return false;

    for (i = 0; i < XVP_UUID_LEN; i++) {
	mustdash = false;
	for (j = 0; j < XVP_UUID_NDASHES; j++) {
	    if (i == dashes[j]) {
		mustdash = true;
		break;
	    }
	}
	if (mustdash) {
	    if (text[i] != '-')
		return false;
	} else if (isupper(text[i]) || !isxdigit(text[i])) {
	    return false;
	}
    }

    return true;
}
