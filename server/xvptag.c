/*
 * xvptag.c - Program to manipulate VM tags in a XenServer/XCP pool
 *
 * Copyright (C) 2010-2012, Colin Dean
 *
 * XenServer and Xen Cloud Platform allow any VM to have a set of zero
 * or more tags associated with it.  Each tag is just a text string.
 * Tags are used for a variety of purposes, for instance xvpweb(7) and
 * xvpappliance(8) use tags to group VMs (for display, and for assigning
 * user rights).  XenServer administrators can also use the XenCenter
 * GUI to manipulate tags, but the versions of the "xe" command-line
 * tool provided with XenServer and XCP don't provide any commands for
 * manipulating tags, so I wrote this program to do so.
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

typedef enum {
    XVP_TAG_UNSET,
    XVP_TAG_LIST,
    XVP_TAG_ADD,
    XVP_TAG_REMOVE
} xvp_tag_command;

static char server_url[XVP_MAX_HOSTNAME + 9]; /* https://hostname */
static xen_session *session;

static void usage(void)
{
    fprintf(stderr,
"xvpdiscover %d.%d.%d, Copyright (C) 2012, Colin Dean\n",
	    XVP_MAJOR, XVP_MINOR, XVP_BUGFIX);
    fprintf(stderr,
"    Usage:\n"
"        xvptag [ options ] vm-name-or-uuid\n"
	    );
    fprintf(stderr,
"    Options:\n"
"        -s | --server          hostname-or-ip     (prompts)\n"
"        -u | --username        username           (prompts)\n"
"        -x | --xenpassword     encrypted-password (prompts for unencrypted)\n"
"        -l | --list                               (omit VM name to list all)\n"
"        -a | --add             tag-text\n"
"        -r | --remove          tag-text\n");
    fprintf(stderr,
"    Precisely one of -l, -a and -r must be specified\n"
"    Enclose tag text or VM name in quotes if it contains spaces\n");
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
 * Exactly the same as in xenapi.c
 */
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
 * List a VM's tags, or for all VMs if vmname is NULL
 */
static void list_tags(xen_session *session, xen_vm vm, char *vmname)
{
    xen_string_set *tags;
    int i, total;

    TRY(xen_vm_get_tags(session, &tags, vm));

    switch (total = tags->size) {
    case 0:
	printf("VM %s has no tags\n", vmname);
	break;
    case 1:
	printf("VM %s has 1 tag:\n  %s\n", vmname, tags->contents[0]);
	break;
    default:
	printf("VM %s has %d tags:\n", vmname, total);
	for (i = 0; i < total; i++)
	    printf("  %s\n", tags->contents[i]);
	break;
    }

    xen_string_set_free(tags);
}

/*
 * Add a tag to a VM - if VM already has this tag, succeeds but does nothing
 */
static void add_tag(xen_session *session, xen_vm vm, char *vmname, char *tag)
{
    char esctag[256];

    (void)xvp_xmlescape(tag, esctag, sizeof(esctag));
    TRY(xen_vm_add_tags(session, vm, esctag));
    printf("Tag successfully added\n");
}


/*
 * Remove a tag from a VM - if VM doesn't have this tag, succeeds but does nothing
 */
static void remove_tag(xen_session *session, xen_vm vm, char *vmname, char *tag)
{
    char esctag[256];

    (void)xvp_xmlescape(tag, esctag, sizeof(esctag));
    TRY(xen_vm_remove_tags(session, vm, esctag));
    printf("Tag successfully removed\n");
}


int main(int argc, char **argv, char **envp)
{
    int optc = argc, i;
    char **optv = argv;
    char *server = NULL, *username = NULL, *xenpw = NULL;
    char *tag = NULL, *vmname = NULL;
    xvp_tag_command command = XVP_TAG_UNSET;
    char xenhex[XVP_MAX_XEN_PW + 1];
    char xentext[XVP_MAX_XEN_PW * 2 + 1];
    xen_vm vm;
    xen_vm_set *vm_set;
    bool template;

    while (optc > 1 && optv[1][0] == '-') {

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

	if (!strcmp(optv[1], "-l") || !strcmp(optv[1], "--list")) {
	    command = XVP_TAG_LIST;
	    optv++;
	    optc--;
	    continue;
	}

	if (!strcmp(optv[1], "-a") || !strcmp(optv[1], "--add")) {
	    if (optc < 3)
		usage();
	    command = XVP_TAG_ADD;
	    tag = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	if (!strcmp(optv[1], "-r") || !strcmp(optv[1], "--remove")) {
	    if (optc < 3)
		usage();
	    command = XVP_TAG_REMOVE;
	    tag = xvp_strdup(optv[2]);
	    optv += 2;
	    optc -= 2;
	    continue;
	}

	usage();
    }

    switch (command) {
    case XVP_TAG_UNSET:
	usage();
	break;
    case XVP_TAG_LIST:
	if (optc == 2)
	    vmname = xvp_strdup(optv[1]);
	else if (optc != 1)
	    usage();
	break;
    default:
	if (optc != 2)
	    usage();
	vmname = xvp_strdup(optv[1]);
	break;
    }

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

    open_session(server, username, xenhex);

    if (!vmname) {

	TRY(xen_vm_get_all(session, &vm_set));

	for (i = 0; i < vm_set->size; i++) {
	    vm = vm_set->contents[i];
	    TRY(xen_vm_get_is_a_template(session, &template, vm));
	    if (template)
		continue;
	    TRY(xen_vm_get_name_label(session, &vmname, vm));
	    if (strncmp(vmname, "Control domain on ", 18) == 0)
		continue;
	    list_tags(session, vm, vmname);
	}

	cleanup();
	return 0;
    }


    if (xvp_xenapi_is_uuid(vmname)) {

	if (!xen_vm_get_by_uuid(session, &vm, vmname))
	    fail("VM %s not found", vmname);

    } else {

	TRY(xen_vm_get_by_name_label(session, &vm_set, vmname));
	switch (vm_set->size) {
	case 0:
	    fail("VM %s not found", vmname);
	    break;
	case 1:
	    vm = vm_set->contents[0];
	    break;
	default:
	    fail("Multiple VMs found with same name");
	    break;
	}
    }

    switch (command) {
    case XVP_TAG_LIST:
	list_tags(session, vm, vmname);
	break;
    case XVP_TAG_ADD:
	add_tag(session, vm, vmname, tag);
	break;
    case XVP_TAG_REMOVE:
	remove_tag(session, vm, vmname, tag);
	break;
    default:
	fail("Internal error: Bad command");
	break;
    }

    cleanup();
    return 0;
}
