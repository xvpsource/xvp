/*
 * xvpdiscover.c - Program to discover hosts & VMs in a XenServer/XCP pool
 *
 * This is intended to be used in association with xvp(1): it connects
 * to the specified XenServer or Xen Cloud Platform host to discover the
 * pool name, host names and virtual machine name labels and UUIDs, and
 * outputs an xvp.conf(5) file suitable for use by xvp(1).
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <libxml/parser.h>
#include <curl/curl.h>

#include <xen/api/xen_all.h>

#include "xvp.h"

#define TRY(_func_) { _func_; if (!session->ok) fail_session(); }
#define NAME_PAD_LEN 30

typedef struct { /* from XenServer C SDK 5.0.0 */
    xen_result_func func;
    void *handle;
} xen_comms;

typedef struct {
    char *label;
    char *uuid;
    char *address;
} host_info;

typedef struct {
    char *vmname;
    char *uuid;
    char *group;
    char password[XVP_MAX_VNC_PW * 2 + 1];
} vm_info;

static char server_url[XVP_MAX_HOSTNAME + 9]; /* https://hostname */
static xen_session *session;

static void usage(void)
{
    fprintf(stderr,
"xvpdiscover %d.%d.%d, Copyright (C) 2009-2012, Colin Dean\n",
	    XVP_MAJOR, XVP_MINOR, XVP_BUGFIX);
    fprintf(stderr,
"    Usage:\n"
"        xvpdiscover options\n"
	    );
    fprintf(stderr,
"    Options:\n"
"        -s | --server          hostname-or-ip     (prompts)\n"
"        -u | --username        username           (prompts)\n"
"        -x | --xenpassword     encrypted-password (prompts for unencrypted)\n"
"        -v | --vncpassword     encrypted-password (prompts for unencrypted)\n"
"        -r | --randompasswords                    (instead of -v)\n"
"        -m | --multiplex       [ port-number ]    (default off, port %d)\n"
"        -p | --portbase        port-number        (default %d)\n"
"        -a | --addresses                          (output host IP addresses)\n"
"        -c | --hostconsoles                       (output host console support)\n"
"        -n | --names                              (output VM names, not UUIDs)\n"
"        -g | --grouptag        tag-prefix         (default none)\n"
"        -o | --output          filename           (prompts, use '-' for stdout)\n"
"        -f | --forceoverwrite                     (overwrite exisiting file)\n",
	    XVP_VNC_PORT_MIN, XVP_VNC_PORT_MIN, XVP_VNC_PORT_MIN);
    exit(1);
}

/*
 * Print message to stderr and bail out
 */
static void fail(char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);

    exit(1);
}

/*
 * Print message and errno text to stderr and bail out
 */
static void fail_errno(char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    va_end(ap);

    exit(1);
    }

/*
 * Print session error details and bail out
 */
static void fail_session(void)
{
    int i;
    char buf[256];

    strcpy(buf, "Xen API error:");
    for (i = 0; i < session->error_description_count; i++) {
	strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
	strncat(buf, session->error_description[i],
		sizeof(buf) - strlen(buf) - 1);
    }

    fail(buf);
}

/*
 * Memory allocation functions, to match those defined in xvp.h
 */
void *xvp_alloc(int size)
{
    void *p;

    if (!(p = calloc(1, size)))
	fail("Out of memory");

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

/*
 * Return string with leading and trailing whitespace removed,
 * note this walks on the original string
 */
static char *trim(char *text)
{
    char *end;

    while isspace(*text)
	text++;

    for (end = text + strlen(text) - 1; end >= text; end--) {
	if (!isspace(*end))
	    break;
	*end = '\0';
    }

    return text;
}

/*
 * Return whether string could possibly be a valid domainname
 */
static char is_domain(char *text)
{
    char *cp;

    if (!text || !*text || !strchr(text, '.'))
	return false;

    if (!isalnum(*text) || !isalnum(text[strlen(text) - 1]))
	return false;

    for (cp = text; *cp; cp++)
	if (!isalnum(*cp) && *cp != '.' && *cp != '-')
	    return false;

    return true;
}

/*
 * Get a line from stdin, with prompt and echo handling as appropriate
 */
static char *get_line(bool hide, char *prompt)
{
    char buf[256], *eol;
    struct termios term;

    if (isatty(0)) {
	fputs(prompt, stdout);
	fflush(stdout);
	if (hide) {
	    tcgetattr(1, &term);
	    term.c_lflag &= ~ECHO;
	    tcsetattr(1, TCSADRAIN, &term);
	}
    }

    fgets(buf, sizeof(buf), stdin);

    if (isatty(0) && hide) {
	term.c_lflag |= ECHO;
	tcsetattr(1, TCSADRAIN, &term);
	putchar('\n');
    }

    if ((eol = strchr(buf, '\r')) || (eol = strchr(buf, '\n')))
	*eol = '\0';

    return xvp_strdup(buf);
}

/*
 * From XenServer C SDK 5.0.0
 */
static size_t write_func(void *ptr, size_t size, size_t nmemb, xen_comms *comms)
{
    size_t n = size * nmemb;

    return comms->func(ptr, n, comms->handle) ? n : 0;
}

/*
 * From XenServer C SDK 5.0.0
 */
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

    curl_easy_setopt(curl, CURLOPT_URL, server_url);
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

/*
 * Open HTTPS session to a host, handling redirection as necessary
 */
static void open_session(char *server, char *username, char *xenhex)
{
    char password[XVP_MAX_XEN_PW + 1], escpassword[256];

    curl_global_init(CURL_GLOBAL_ALL);
    xmlInitParser();
    xen_init();
    memset(password, 0, sizeof(password));

    sprintf(server_url, "https://%s", server);

    xvp_password_decrypt(xenhex, password, XVP_PASSWORD_XEN);
    (void)xvp_xmlescape(password, escpassword, sizeof(escpassword));

    session = xen_session_login_with_password(call_func, NULL,
		  username, escpassword, xen_api_version_1_5);

    if (!session->ok &&
	session->error_description_count > 0 &&
	!strcmp(session->error_description[0], "HOST_IS_SLAVE")) {
	sprintf(server_url, "https://%s", session->error_description[1]);
	xen_session_logout(session);
	session = xen_session_login_with_password(call_func, NULL,
		      username, escpassword, xen_api_version_1_5);
    }

    memset(password, 0, sizeof(password));
    memset(escpassword, 0, sizeof(escpassword));

    if (!session->ok)
	fail_session();
}

/*
 * Clean up session connection etc
 */
static void cleanup(void)
{
    curl_global_cleanup();
    xmlCleanupParser();
    xen_session_logout(session);
    xen_fini();
}

/*
 * qsort() comparator for VMs, sorting by group first (if defined)
 * and then by name label
 */
static int vm_cmp(const void *p1, const void *p2)
{
    vm_info *vm1 = (vm_info *)p1;
    vm_info *vm2 = (vm_info *)p2;
    int gcmp = 0;

    if (vm1->group && vm2->group)
	gcmp = strcasecmp(vm1->group, vm2->group);

    return gcmp ? gcmp : strcasecmp(vm1->vmname, vm2->vmname);
}

/*
 * Generate pseudo-random VNC password, using ASCII printable characters,
 * but reproducible for a given UUID.
 */
static char *vm_randpw(char *uuid)
{
    int i, seed;
    char pw[XVP_MAX_VNC_PW + 1];
    char hex[XVP_MAX_VNC_PW + 1];
    static char text[XVP_MAX_VNC_PW * 2 + 1];

    sscanf(uuid + 28, "%x", &seed);
    srandom(seed);

    for (i = 0; i < XVP_MAX_VNC_PW ; i++) {
	/* random ASCII printable non-space characters */
	pw[i] = (int)((random() * 1.0 * (126 - 32) / RAND_MAX) + 33);
	pw[XVP_MAX_VNC_PW] = '\0';
    }

    xvp_password_encrypt(pw, hex, XVP_PASSWORD_VNC);
    xvp_password_hex_to_text(hex, text, XVP_PASSWORD_VNC);

    return text;
}

int main(int argc, char **argv, char **envp)
{
    int optc = argc, type, i, j, k;
    char **optv = argv;
    char *server = NULL, *username = NULL, *hostname, *address, *uuid, *group;
    char *label, *domain, *tail;
    char *xenpw = NULL, *vncpw = NULL, *grouptag = NULL;
    char *output = NULL, *eol, *pool_label, *pool_description;
    FILE *stream = NULL;

    char xenhex[XVP_MAX_XEN_PW + 1];
    char vnchex[XVP_MAX_VNC_PW + 1];
    char xentext[XVP_MAX_XEN_PW * 2 + 1];
    char vnctext[XVP_MAX_VNC_PW * 2 + 1];

    bool randpws = false, byname = false, overwrite = false;
    bool addresses = false, hostconsoles = false;
    int portbase = 0;
    int multiplex = 0;
    int portnum;

    xen_pool_set *pool_set;
    xen_host master_host;
    xen_host_set *host_set;
    xen_vm_set *vm_set;

    host_info *hosts_array;
    vm_info *vm_array;

    time_t now;

    while (optc > 1) {

	if (!strcmp(optv[1], "-s") || !strcmp(optv[1], "--server")) {
	    if (optc < 3)
		usage();
	    server = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-u") || !strcmp(optv[1], "--username")) {
	    if (optc < 3)
		usage();
	    username = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-x") || !strcmp(optv[1], "--xenpassword")) {
	    if (optc < 3)
		usage();
	    xenpw = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-v") || !strcmp(optv[1], "--vncpassword")) {
	    if (optc < 3)
		usage();
	    vncpw = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-o") || !strcmp(optv[1], "--output")) {
	    if (optc < 3)
		usage();
	    output = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-r") || !strcmp(optv[1], "--randompasswords")) {
	    randpws = true;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-m") || !strcmp(optv[1], "--multiplex")) {
	    if (optc < 3 || optv[2][0] == '-') {
		multiplex = XVP_VNC_PORT_MIN;
		optv++;
		optc--;
	    } else {
		multiplex = atoi(optv[2]);
		if (multiplex < 1024 || multiplex > 65535)
		    usage();
		optv += 2;
		optc -= 2;
	    }
	    continue;
	}

	if (!strcmp(optv[1], "-p") || !strcmp(optv[1], "--portbase")) {
	    if (optc < 3)
		usage();
	    portbase = atoi(optv[2]);
	    if (portbase < 1024 || portbase > 65535)
		usage();
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-n") || !strcmp(optv[1], "--names")) {
	    byname = true;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-a") || !strcmp(optv[1], "--addresses")) {
	    addresses = true;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-c") || !strcmp(optv[1], "--hostconsoles")) {
	    hostconsoles = true;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-g") || !strcmp(optv[1], "--grouptag")) {
	    if (optc < 3)
		usage();
	    grouptag = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-f") || !strcmp(optv[1], "--forceoverwrite")) {
	    overwrite = true;
	    optv++;
	    optc--;
	    continue;
	}

	usage();
    }

    if (vncpw && randpws)
	usage();

    if (!portbase && !multiplex)
	portbase = XVP_VNC_PORT_MIN;
    else if (portbase && multiplex && portbase == multiplex)
	fail("Base port %d clashes with MULTIPLEX port %d",
	     portbase, multiplex);


    if (!server)
	server = get_line(false, "Server hostname or IP address: ");
    if (!*server)
	fail("Invalid server name or address");

    if (!username)
	username = get_line(false, "Server username: ");
    if (!*username)
	fail("Invalid username");

    if (xenpw) {
	if (!xvp_password_text_to_hex(xenpw, xenhex, XVP_PASSWORD_XEN))
	    fail("Invalid Server password");
    } else {
	char *pw = get_line(true, "Server password: ");
	if (!*pw)
	    fail("Empty passwords not supported");
	else if (strlen(pw) > XVP_MAX_XEN_PW)
	    fail("Password too long: maximum %d characters", XVP_MAX_XEN_PW);
	xvp_password_encrypt(pw, xenhex, XVP_PASSWORD_XEN);
	xvp_password_hex_to_text(xenhex, xentext, XVP_PASSWORD_XEN);
	xenpw = xentext;
    }

    if (vncpw) {
	if (!xvp_password_text_to_hex(vncpw, vnchex, XVP_PASSWORD_VNC))
	    fail("Invalid VNC password");
    } else if (!randpws) {
	char *pw = get_line(true, "VNC password (return for random): ");
	if (!*pw) {
	    randpws = true;
	} else if (strlen(pw) > XVP_MAX_VNC_PW) {
	    fail("Password too long: maximum %d characters", XVP_MAX_VNC_PW);
	} else {
	    xvp_password_encrypt(pw, vnchex, XVP_PASSWORD_VNC);
	    xvp_password_hex_to_text(vnchex, vnctext, XVP_PASSWORD_VNC);
	    vncpw = vnctext;
	}
    }

    /*
     * Don't open the output file until we know we've discovered everything
     * about the pool successfully, but we can do some checks now
     */

    if (!output)
	output = get_line(false, "File for output (return for stdout): ");

    if (!strcmp(output, "-") || !*output)
	stream = stdout;
    else if (!overwrite && access(output, F_OK) == 0)
	fail("%s exists: use \"-f\" to force overwrite", output); 

    /*
     * Find everything out and stash it away for output later
     */

    open_session(server, username, xenhex);

    TRY(xen_pool_get_all(session, &pool_set));
    if (pool_set->size != 1)
	fail("Expected one pool, found %d", pool_set->size);

    /*
     * For a standalone host, by default, poolname is empty:
     * we return as such, it's up to the user to change if desired.
     */
    TRY(xen_pool_get_name_label(session, &pool_label, pool_set->contents[0]));

    TRY(xen_pool_get_name_description(session, &pool_description, pool_set->contents[0]));

    TRY(xen_host_get_all(session, &host_set));
    hosts_array = xvp_alloc(host_set->size * sizeof(host_info));

    TRY(xen_pool_get_master(session, &master_host, pool_set->contents[0]));
    TRY(xen_host_get_hostname(session, &hostname, master_host));
    if ((tail = strchr(hostname, '.')) && is_domain(tail + 1)) {
	*tail++ = '\0';
	domain = tail;
    } else {
	domain = "";
    }

    TRY(xen_host_get_name_label(session, &label, master_host));
    if ((tail = strstr(label, domain)) &&
	tail[strlen(domain)] == '\0' && tail > label && tail[-1] == '.')
	*--tail = '\0';
    if (addresses)
	TRY(xen_host_get_address(session, &address, master_host));
    if (hostconsoles && randpws)
	TRY(xen_host_get_uuid(session, &uuid, master_host));
    
    /*
     * Sort hosts master first, then slaves in whatever order they come
     */

    if (addresses)
	hosts_array[0].address = address;
    if (hostconsoles && randpws)
	hosts_array[0].uuid = uuid;

    hosts_array[0].label = label;

    for (i = 0, j = 1; i < host_set->size; i++) {
	TRY(xen_host_get_name_label(session, &label, host_set->contents[i]));
	if ((tail = strstr(label, domain)) &&
	    tail[strlen(domain)] == '\0' && tail > label && tail[-1] == '.')
	    *--tail = '\0';
	if (strcmp(label, hosts_array[0].label) == 0)
	    continue;
	if (addresses) {
	    TRY(xen_host_get_address(session, &address, host_set->contents[i]));
	    hosts_array[j].address = address;
	}
	if (hostconsoles && randpws) {
	    TRY(xen_host_get_uuid(session, &uuid, host_set->contents[i]));
	    hosts_array[j].uuid = uuid;
	}
	hosts_array[j++].label = label;
    }

    /*
     * If we have a group tag, sort VMs alphabetically by group and then
     * alphabetically by name label, and skip VMs with no group tag.
     *
     * Otherwise, just sort all alphabetically by name label.
     */

    TRY(xen_vm_get_all(session, &vm_set));
    vm_array = xvp_alloc(vm_set->size * sizeof(vm_info));

    for (i = 0, j = 0; i < vm_set->size; i++) {
	bool template;
	xen_string_set *tags;
	TRY(xen_vm_get_is_a_template(session, &template, vm_set->contents[i]));
	if (template)
	    continue;
	TRY(xen_vm_get_name_label(session, &vm_array[j].vmname,
				  vm_set->contents[i]));
	if (strncmp(vm_array[j].vmname, "Control domain on ", 18) == 0)
	    continue;
	TRY(xen_vm_get_uuid(session, &vm_array[j].uuid, vm_set->contents[i]));

	if (grouptag) {
	    int taglen = strlen(grouptag);
	    TRY(xen_vm_get_tags(session, &tags, vm_set->contents[i]));
	    vm_array[j].group = "";
	    for (k = 0; k < tags->size; k++) {
		if (strncasecmp(grouptag, tags->contents[k], taglen) == 0) {
		    vm_array[j].group =
			xvp_strdup(trim(tags->contents[k] + taglen));
		    break;
		}
	    }
	    xen_string_set_free(tags);
	}

	j++;
    }

    qsort(vm_array, j, sizeof(vm_info), vm_cmp);

    /*
     * We have all the data, time to open and write the output file
     */

    umask(077);
    if (!stream && !(stream = fopen(output, "w")))
	fail_errno(output);

    now = time(NULL);
    fprintf(stream,
	    "#\n# Configuration file for xvp, written by xvpdiscover, %s#\n\n",
	    asctime(localtime(&now)));

    if (multiplex)
	fprintf(stream, "MULTIPLEX %d\n\n", multiplex);

    fprintf(stream, "POOL \"%s\"", pool_label);
    if (*pool_description)
	fprintf(stream, " # %s\n", pool_description);
    else
	fputc('\n', stream);

    fprintf(stream, "DOMAIN \"%s\"\nMANAGER \"%s\" %s\n", domain,
	    username, xenpw);
    fprintf(stream, "\n#\n# Hosts\n#\n\n");

    for (i = 0; i < host_set->size; i++) {
	if (addresses)
	    fprintf(stream, "HOST %s \"%s\"\n",
		    hosts_array[i].address, hosts_array[i].label);
	else
	    fprintf(stream, "HOST \"%s\"\n", hosts_array[i].label);
    }

    if (hostconsoles) {

	char portbuf[10];

	fprintf(stream, "\n#\n# Host Console Support\n#\n\n");

	portnum = portbase;

	for (i = 0; i < host_set->size; i++) {

	    if (!portbase)
		strcpy(portbuf, "-");
	    else if (portnum == multiplex)
		fail("Port %d for host \"%s\" clashes with MULTIPLEX port",
		     portnum, hosts_array[i].label);
	    else if (portnum > 65535)
		fail("Port %d for host \"%s\" is greater that 65535",
		     portnum, hosts_array[i].label);
	    else
		sprintf(portbuf, "%d", portnum++);


	    if (addresses)
		fprintf(stream, "VM %s %s %s # %s\n", portbuf,
			hosts_array[i].address,
			randpws ? vm_randpw(hosts_array[i].uuid) : vncpw,
			hosts_array[i].label);
	    else
		fprintf(stream, "VM %s \"%s\" %s\n", portbuf,
			hosts_array[i].label,
			randpws ? vm_randpw(hosts_array[i].uuid) : vncpw);
	}

	if (portbase)
	    portbase += host_set->size;
    }

    fprintf(stream, "\n#\n# Virtual Machines\n#\n\n");

    group = "";
    portnum = portbase;

    for (i = 0; i < j; i++) {

	char portbuf[10];
	vm_info *vm = vm_array + i;

	if (grouptag && !vm->group[0])
	    continue;
	if (grouptag && strcmp(group, vm->group) != 0) {
	    group = vm->group;
	    fprintf(stream, "\nGROUP \"%s\"\n\n", group);
	}

	if (!portbase)
	    strcpy(portbuf, "-");
	else if (portnum == multiplex)
	    fail("Port %d for VM \"%s\" clashes with MULTIPLEX port",
		 portnum, vm->vmname);
	else if (portnum > 65535)
	    fail("Port %d for VM \"%s\" is greater that 65535",
		 portnum, vm->vmname);
	else
	    sprintf(portbuf, "%d", portnum++);

	if (byname) {
	    char padding[NAME_PAD_LEN + 1];
	    int padlen = NAME_PAD_LEN - strlen(vm->vmname);

	    if (padlen <= 0)
		padlen = 1;
	    memset(padding, ' ', padlen);
	    padding[padlen] = '\0';

	    fprintf(stream, "VM %s \"%s\"%s%s\n", portbuf, vm->vmname,
		    padding, randpws ? vm_randpw(vm->uuid) : vncpw);
	} else {
	    fprintf(stream, "VM %s %s %s # %s\n", portbuf, vm->uuid,
		    randpws ? vm_randpw(vm->uuid) : vncpw, vm->vmname);
	}
    }

    if (stream != stdout)
	fclose(stream);

    cleanup();
    return 0;
}
