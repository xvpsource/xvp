/*
 * proxy.c - proxy handling for Xen VNC Proxy
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
 *                 Note regarding RFB Protocol handling
 *                 ------------------------------------
 *
 * On the client side we support 3.3, 3.7 and 3.8.  XenServer and XCP
 * currently support 3.3, and we use that on the server side.
 *
 * Note that we don't initiate a connection to the server until we've
 * validated the client.
 *
 * Note also that we use XVP extensions to the RFB protocol, when talking
 * to compatible clients (such as xvpviewer).  These extensions use
 * numbers that have been officially allocated to us by RealVNC Ltd.
 *
 * Starting at version 1.12.2, we handle the fact that Oracle Java SE 1.7
 * appears to fragment data it sends over an SSL tunnel, sending each
 * user-level byte in 1 packet.  This means we can't necessarily retrieve
 * the whole of a single RFB message from the client in a single read(2)
 * system call, although we do so where we can for efficiency reasons.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>

#include "xvp.h"

/* Use modest buffer size - could be many instances running */
#define XVP_PROXY_BUF_SIZE 4096

/* RFB data types - transferred big-endian */
typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned int   U32;
typedef signed char    S8;
typedef short          S16;
typedef int            S32;

typedef enum {
    XVP_STATE_SERVER_VERSION,
    XVP_STATE_CLIENT_VERSION,
    XVP_STATE_REQUIRE_AUTH,
    XVP_STATE_SELECT_AUTH,
    XVP_STATE_USER_TARGET,
    XVP_STATE_CHALLENGE_AUTH,
    XVP_STATE_RESPONSE_AUTH,
    XVP_STATE_CONFIRM_AUTH,
    XVP_STATE_CLIENT_INIT,
    XVP_STATE_SERVER_CONNECT,
    XVP_STATE_SERVER_INIT,
    XVP_STATE_IDLING,
    XVP_STATE_CONSOLE_DELETED,
    XVP_STATE_SERVER_REINIT,
    XVP_STATE_BROKEN
} xvp_proxy_state_enum;

typedef struct { /* to pass to server-connecting thread */
    xvp_vm *vm;
    bool shared;
    bool reinit;
    SSL **sslp; /* for return */
} xvp_server_info;

typedef struct { /* to pass to RFB proxying threads */
    int client_sock;
    SSL *server_handle;
} xvp_proxy_handles;

typedef struct { /* to pass to extension message code thread */
    int client_sock;
    int message_code;
} xvp_proxy_code;

static char client_hostname[XVP_MAX_HOSTNAME + 1];
static char proxy_name[XVP_MAX_HOSTNAME * 2 + 16];
static xvp_proxy_state_enum xvp_proxy_state;
static unsigned int xvp_proxy_minor_version;
static unsigned int xvp_proxy_security_type;
static bool xvp_proxy_writing;
static bool xvp_proxy_extensions;
static pthread_t xvp_proxy_writer_thread;
static pthread_t xvp_proxy_reader_thread;

static struct {
    U16 fb_width;
    U16 fb_height;
    U8  pixel_format[16];
    U32 name_length;
    /* U8 name[name_length]; */
} xvp_proxy_server_details;

static struct {
    U8  message_type; /* XVP_RFB_MESSAGE_TYPE_SET_PIXEL_FORMAT */
    U8  padding1[3];
    U8  pixel_format[16];
} xvp_proxy_pixel_format;

static struct {
    U8  message_type; /* XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS */
    U8  padding;
    U16 number; /* of encodings */
#define XVP_PROXY_MAX_ENCODINGS 32
    S32 encodings[XVP_PROXY_MAX_ENCODINGS];
} xvp_proxy_encodings;

static struct {
    U8  message_type; /* XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT */
    U8  padding1[3];
    U32 text_length;
    /* U8 text[text_length]; */
} xvp_proxy_cut_text;

static struct {
    U8 message_type; /* 4 */
    U8 down_flag; /* non-zero => down, zero => up */
    U8 padding1[2];
    U32 key;
} xvp_proxy_key_event;

static struct {
    U8  message_type; /* 3 */
    U8  incremental;
    U16 x_position;
    U16 y_position;
    U16 width;
    U16 height;
} xvp_proxy_fb_request;

bool xvp_reconnect_delay = XVP_RECONNECT_DELAY;

/*
 * Standard RFB client->server message types we recognise
 */
#define XVP_RFB_MESSAGE_TYPE_SET_PIXEL_FORMAT  0
#define XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS     2
#define XVP_RFB_MESSAGE_TYPE_FB_UPDATE_REQUEST 3
#define XVP_RFB_MESSAGE_TYPE_KEY_EVENT         4
#define XVP_RFB_MESSAGE_TYPE_POINTER_EVENT     5
#define XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT   6

/*
 * Standard RFB security types we know about
 */
#define XVP_RFB_SECURITY_NONE 1
#define XVP_RFB_SECURITY_VNC  2

/*
 * For XVP extensions to RFB protocol, officially allocated
 * by Tristan Richardson of RealVNC Ltd on April 21, 2009
 * and November 24, 2009
 */
#define XVP_RFB_SECURITY_XVP     22
#define XVP_RFB_ENCODING_XVP     0xfffffecb
#define XVP_RFB_MESSAGE_TYPE_XVP 250
#define XVP_RFB_MESSAGE_VERSION  1

typedef struct {
    U8 message_type; /* XVP_RFB_MESSAGE_TYPE_XVP */
    U8 padding;
    U8 version;
    U8 code;
} xvp_proxy_code_message;

void xvp_proxy_dump(void)
{
    xvp_log(XVP_LOG_INFO, "Active %s", proxy_name + 5);
}

static void xvp_proxy_set_name(xvp_vm *vm)
{
    sprintf(proxy_name, "xvp: proxy: %s to %s", client_hostname, vm->vmname);
    xvp_process_set_name(proxy_name);
}

static char *xvp_proxy_get_name(void)
{
    char *name = xvp_process_get_name();
    return strncmp(name, "xvp: ", 5) ? name : name + 5;
}

static bool xvp_read_all(int fd, void *buf, int len) {
    int total, got;
    char *cbuf = buf;

    for (total = 0; total < len; total += got) {
	if ((got = read(fd, cbuf + total, len - total)) <= 0)
	    return false;
    }

    return true;
}

static int xvp_read_line(int fd, char *buf, int len) {
    int total, got;

    for (total = 0; total < len; total++) {
	if ((got = read(fd, buf + total, 1)) <= 0)
	    return got;
	else if (buf[total] == '\n')
	    return total + 1;
    }

    return len;
}

static bool xvp_write_all(int fd, void *buf, int len)
{
    int total, sent;
    char *cbuf = buf;

    for (total = 0; total < len; total += sent) {
	if ((sent = write(fd, cbuf + total, len - total)) <= 0)
	    return false;
    }
    return true;
}

static void xvp_proxy_trace_client(void *buf, int len, bool proxy)
{
    unsigned char *u8 = (unsigned char *)buf;
    unsigned int *u32 = (unsigned int *)buf;
    unsigned short *u16 = (unsigned short *)buf;
    int *s32 = (int *)buf;
    char *type;

    if (!xvp_verbose || !xvp_tracing)
	return;

    switch (u8[0]) {
    case XVP_RFB_MESSAGE_TYPE_SET_PIXEL_FORMAT:
	type = "SetPixelFormat";
	break;
    case XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS:
	type = "SetEncodings";
	break;
    case XVP_RFB_MESSAGE_TYPE_FB_UPDATE_REQUEST:
	type = "FrameBufferUpdateRequest";
	break;
    case XVP_RFB_MESSAGE_TYPE_KEY_EVENT:
	type = "KeyEvent";
	break;
    case XVP_RFB_MESSAGE_TYPE_POINTER_EVENT:
	type = "PointerEvent";
	break;
    case XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT:
	type = "ClientCutText";
	break;
    case XVP_RFB_MESSAGE_TYPE_XVP:
	type = "XVP";
	break;
    default:
	type = "unrecognised message";
	break;
    }

    xvp_log(XVP_LOG_DEBUG, "%s %s", (proxy ? "Proxy" : "Client"), type);
    if (u8[0] == XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS) {
	int i, e;
	for (i = 0; i < ntohs(u16[1]); i++) {
	    e = ntohl(s32[i+1]);
	    xvp_log(XVP_LOG_DEBUG, "  %08x %d", e, e);
	}
    } else if (u8[0] == XVP_RFB_MESSAGE_TYPE_FB_UPDATE_REQUEST) {
	xvp_log(XVP_LOG_DEBUG, "  incr %u, x %u, y %u, w %u, h %u", u8[1],
		ntohs(u16[1]), ntohs(u16[2]), ntohs(u16[3]), ntohs(u16[4]));
    } else if (u8[0] == XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT) {
	U32 textlen = ntohl(u32[1]);
	char textbuf[XVP_PROXY_BUF_SIZE];
	memcpy(textbuf, u8 + 8, textlen);
	textbuf[textlen] = '\0';
	xvp_log(XVP_LOG_DEBUG, "  %s", textbuf);
    } else {
	for (; len >= 8 && u8[0] == XVP_RFB_MESSAGE_TYPE_KEY_EVENT;
	     u8 += 8, u32 += 2, len -= 8) {
	    U32 key = ntohl(u32[1]);
	    U8 c = (key > 32 && key < 127) ? (U8)key : ' ';
	    char *travel = u8[1] ? "down" : "up";
	    xvp_log(XVP_LOG_DEBUG, "  key 0x%08x %c %s", key, c, travel);
	}
	for (; len >= 6 && u8[0] == XVP_RFB_MESSAGE_TYPE_POINTER_EVENT;
	     u8 += 6, u16 += 3, len -= 6) {
	    U8 mask = u8[1];
	    U16 x = ntohs(u16[1]);
	    U16 y = ntohs(u16[2]);
	    xvp_log(XVP_LOG_DEBUG, "  pointer 0x%1x, %u %u", mask, x, y);
	}
    }
}

static void xvp_proxy_trace_server(char *u8, int len)
{
    unsigned int *u32 = (unsigned int *)u8;
    unsigned short *u16 = (unsigned short *)u8;
    char *type;

    if (!xvp_verbose || !xvp_tracing)
	return;

    switch (u8[0]) {
    case 0: type = "FrameBufferUpdate"; break;
    case 1: type = "SetColourMapEntries"; break;
    case 2: type = "Bell"; break;
    case 3: type = "ServerCutText"; break;
    default: type = "unrecognised message"; break;
    }

    xvp_log(XVP_LOG_DEBUG, "Server %s %d", type, len);
}

static bool xvp_proxy_client_update(int sock, xvp_message_code code)
{
    xvp_proxy_code_message message;

    message.message_type = XVP_RFB_MESSAGE_TYPE_XVP;
    message.padding      = 0;
    message.version      = XVP_RFB_MESSAGE_VERSION;
    message.code         = code;

    return xvp_write_all(sock, &message, sizeof(message));
}

static bool xvp_proxy_extensions_init(int sock)
{
    int i, n = ntohs(xvp_proxy_encodings.number);
    int e = htonl(XVP_RFB_ENCODING_XVP);

    for (i = 0; i < n; i++) {
	if (xvp_proxy_encodings.encodings[i] == e) {
	    xvp_proxy_extensions = true;
	    xvp_log(XVP_LOG_DEBUG, "Client supports XVP extensions to RFB");
	    break;
	}
    }

    if (!xvp_proxy_extensions || xvp_vm_is_host)
	return true;

    return xvp_proxy_client_update(sock, XVP_MESSAGE_CODE_INIT);
}

static void *xvp_proxy_message_code_handler(void *arg)
{
    xvp_proxy_code *pc = (xvp_proxy_code *)arg;

    if (!xvp_xenapi_handle_message_code(pc->message_code))
	(void)xvp_proxy_client_update(pc->client_sock, XVP_MESSAGE_CODE_FAIL);
}

static bool xvp_proxy_handle_extensions(int sock, int version, int code)
{
    pthread_t pt;
    static xvp_proxy_code pc;
    bool ha;

    pc.client_sock = sock;
    pc.message_code = code;

    if (version != XVP_RFB_MESSAGE_VERSION) {
	xvp_log(XVP_LOG_ERROR, "Unrecognised client XVP extension version %d",
		version);
	return false;
    }

    if (pthread_create(&pt, NULL, xvp_proxy_message_code_handler, &pc) != 0)
	xvp_log_errno(XVP_LOG_FATAL, "pthread_create");

    return true;
}

static bool xvp_proxy_handle_cut_text(SSL *server_handle, char *text, int len)
{
    int i, c, flag;

    /*
     * As XenServer consoles ignore RFB ClientCutText messages,
     * convert to a sequence of down/up KeyEvent messages.
     *
     * Unfortunately, people implementing VNC servers often don't read
     * or act on the guidelines in the RFB specification that explain
     * the issues arounding "shift state", and so for instance just
     * sending all keys without shift to the XenServer console of a
     * Windows VM causes "!" to be interpreted as "1" and "#" as "3".
     * Of course, what's shifted depends on the keyboard layout: on
     * a UK PC keyboard, "#" is not shifted.  The best we can do here
     * is fake left shifts based on a standard US PC layout.
     *
     * Note that xvpviewer sends non-ASCII as UTF-8, and if XenServer
     * conformed to the RFB protocol as far as non-ASCII key events
     * are concerned, we should here convert UTF-8 to UTF-32 and then
     * map to X11 keysyms as in /usr/include/X11/keysymdef.h and as
     * done in xvpviewer when sending KeyEvent messages, and it should
     * not be be necessary to fake shift key presses.  However, with
     * XenServer as it is, a bodge is to treat each byte in the UTF-8
     * encoded string from the viewer as an individual key, and set the
     * shift state for uppercase ASCII letters and shifted keys for the
     * the standard US PC keyboard layout.  This does also cause some
     * accented ISO-8859-1 characters to be interpreted correctly by
     * Linux VMs.
     */

    static char *shiftsyms = "~!@#$%^&*()_+|{}:\"<>?";
    bool shifted;

    xvp_proxy_key_event.message_type = XVP_RFB_MESSAGE_TYPE_KEY_EVENT;
    for (i = 0; i < len; i++) {
	c = *((unsigned char *)text + i);
	shifted = false;
	if (c == '\n')
	    c = 0xff0d;
	else if (c < 0x20)
	    continue;
	else if ((c >= 'A' && c <= 'Z') || strchr(shiftsyms, c) != NULL)
	    shifted = true;

	if (xvp_verbose && xvp_tracing) {
	    xvp_log(XVP_LOG_DEBUG, "ClientCutText %s%c 0x%02x",
		    shifted ? "Shift " : "", c, c);
	}

	if (shifted) {
	    xvp_proxy_key_event.key = htonl(0xffe1);
	    xvp_proxy_key_event.down_flag = 1;
	    if (SSL_write(server_handle, &xvp_proxy_key_event, 8) != 8)
		return false;
	}
	xvp_proxy_key_event.key = htonl(c);
	for (flag = 1; flag >= 0; flag--) {
	    xvp_proxy_key_event.down_flag = flag;
	    if (SSL_write(server_handle, &xvp_proxy_key_event, 8) != 8)
		return false;
	}
	if (shifted) {
	    xvp_proxy_key_event.key = htonl(0xffe1);
	    xvp_proxy_key_event.down_flag = 0;
	    if (SSL_write(server_handle, &xvp_proxy_key_event, 8) != 8)
		return false;
	}
    }

    return true;
}

static void *xvp_proxy_writer(void *arg)
{
    xvp_proxy_handles *ph = (xvp_proxy_handles *)arg;
    char buf[XVP_PROXY_BUF_SIZE];
    int len, expected, sig = SIGQUIT;
    U8 type;

    while (true) {
	/*
	 * don't read sizeof(buf) as could get multiple messages in 1 call:
	 * just read 1st byte (message type) and then take it from there
	 */
	if ((len = read(ph->client_sock, buf, 1)) <= 0)
	    break;

	type = buf[0];

	/*
	 * Set expected byte length to size of message (if fixed) or
	 * size of fixed part, and ensure have this so can parse
	 */
	switch (type) {
	case XVP_RFB_MESSAGE_TYPE_SET_PIXEL_FORMAT:
	    expected = 20;
	    break;
	case XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS:
	    expected = 4; /* +4 for each encoding */
	    break;
	case XVP_RFB_MESSAGE_TYPE_FB_UPDATE_REQUEST:
	    expected = 10;
	    break;
	case XVP_RFB_MESSAGE_TYPE_KEY_EVENT:
	    expected = 8;
	    break;
	case XVP_RFB_MESSAGE_TYPE_POINTER_EVENT:
	    expected = 6;
	    break;
	case XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT:
	    expected = 8; /* + length */
	    break;
	case XVP_RFB_MESSAGE_TYPE_XVP:
	    expected = 4;
	    break;
	default:
	    expected = 0;
	    break;
	}

	if (expected > len) {
	    if (!xvp_read_all(ph->client_sock, buf + len, expected - len))
		break;
	    len = expected;
	}

	if (expected == 0) {
	    xvp_log(XVP_LOG_ERROR, "Unrecognised client message type %d",
		    buf[0]);
	    break;
	}

	if (type == XVP_RFB_MESSAGE_TYPE_SET_PIXEL_FORMAT) {

	    /* save pixel format for use in re-init */
	    memcpy(&xvp_proxy_pixel_format, buf, sizeof(xvp_proxy_pixel_format));

	} else if (type == XVP_RFB_MESSAGE_TYPE_SET_ENCODINGS) {

	    bool seen = (xvp_proxy_encodings.message_type != 0xff);
	    memcpy(&xvp_proxy_encodings, buf, len);
	    expected += ntohs(xvp_proxy_encodings.number) * sizeof(S32);
	    if (expected > len) {
		if (!xvp_read_all(ph->client_sock, buf + len, expected - len))
		    break;
		len = expected;
		memcpy(&xvp_proxy_encodings, buf, len);
	    }
	    if (!seen && !xvp_proxy_extensions_init(ph->client_sock))
		break;

	} else if (type == XVP_RFB_MESSAGE_TYPE_CLIENT_CUT_TEXT) {

	    memcpy(&xvp_proxy_cut_text, buf, len);
	    expected += ntohl(xvp_proxy_cut_text.text_length);
	    if (expected > len) {
		if (!xvp_read_all(ph->client_sock, buf + len, expected - len))
		    break;
		len = expected;
	    }
	    if (!xvp_proxy_handle_cut_text(ph->server_handle, buf + 8, len - 8))
		break;
	    continue;

	} else if (type == XVP_RFB_MESSAGE_TYPE_XVP) {

	    xvp_proxy_code_message *cm = (xvp_proxy_code_message *)buf;
	    if (!xvp_proxy_handle_extensions(ph->client_sock, cm->version, cm->code))
		break;
	    continue;
	}

	if (xvp_verbose && xvp_tracing)
	    xvp_proxy_trace_client(buf, len, false);

	if (SSL_write(ph->server_handle, buf, len) != len)
	    return NULL;
    }

    if (write(xvp_child_sigpipe[1], &sig, sizeof(sig)) != sizeof(sig))
	exit(1);

    return NULL;
}

static void *xvp_proxy_reader(void *arg)
{
    xvp_proxy_handles *ph = (xvp_proxy_handles *)arg;
    char buf[XVP_PROXY_BUF_SIZE];
    int len, sig = SIGQUIT;
    static int incarnation = 0;

    while (true) {
	if ((len = SSL_read(ph->server_handle, buf, sizeof(buf))) <= 0)
	    return NULL;

	if (xvp_verbose && xvp_tracing)
	    xvp_proxy_trace_server(buf, len);

	if (!xvp_write_all(ph->client_sock, buf, len))
	    break;
    }

    if (write(xvp_child_sigpipe[1], &sig, sizeof(sig)) != sizeof(sig))
	exit(1);

    return NULL;
}

static bool xvp_proxy_ssl_read(SSL *ssl, void *buf, int len)
{
    int res = SSL_read(ssl, buf, len);

    if (res != len) {
	xvp_log(XVP_LOG_ERROR, "SSL read: Error %d",
		SSL_get_error(ssl, res));
	return false;
    }

    return true;
}

static bool xvp_proxy_ssl_write(SSL *ssl, void *buf, int len)
{
    int res = SSL_write(ssl, buf, len);

    if (res != len) {
	xvp_log(XVP_LOG_ERROR, "SSL write: Error %d",
		SSL_get_error(ssl, res));
	return false;
    }

    return true;
}

static bool xvp_proxy_version_known(unsigned int major, unsigned int minor)
{
    if (major != XVP_RFB_MAJOR)
	return false;

    switch (minor) {
    case XVP_RFB_MINOR_3:
    case XVP_RFB_MINOR_7:
    case XVP_RFB_MINOR_8:
	return true;
    }

    return false;
}

/*
 * This is run in a background thread to avoid blocking signal handling
 * or dead client detection.  We signal the main thread when we're done.
 */
static void *xvp_proxy_server_handshake(void *arg)
{
    xvp_server_info *info = (xvp_server_info *)arg;
    unsigned int major, minor, type, len;
    int sig;
    char buf[XVP_PROXY_BUF_SIZE];
    SSL *ssl;

    *info->sslp = NULL;

    if (!(ssl = xvp_xenapi_open_stream(info->vm)))
	goto end;

    if (!xvp_proxy_ssl_read(ssl, buf, 12))
	goto end;
    buf[12] = '\0';
    if (sscanf(buf, "RFB %03u.%03u\n", &major, &minor) != 2 ||
	!xvp_proxy_version_known(major, minor)) {
	xvp_log(XVP_LOG_ERROR, "Unsupported server version: %s", buf);
	goto end;
    }
    sprintf(buf, "RFB %03u.%03u\n", XVP_RFB_MAJOR, XVP_RFB_MINOR_SERVER);
    if (!xvp_proxy_ssl_write(ssl, buf, 12))
	goto end;

    if (!xvp_proxy_ssl_read(ssl, &type, sizeof(type)))
	goto end;
    if (ntohl(type) != XVP_RFB_SECURITY_NONE) {
	xvp_log(XVP_LOG_ERROR, "Unexpected security type: %d", ntohl(type));
	goto end;
    }
    buf[0] = (info->shared ? 1 : 0);
    if (!xvp_proxy_ssl_write(ssl, buf, 1))
	goto end;
    if (!xvp_proxy_ssl_read(ssl, &xvp_proxy_server_details,
			      sizeof(xvp_proxy_server_details)))
	goto end;
    len = ntohl(xvp_proxy_server_details.name_length);
    if (!xvp_proxy_ssl_read(ssl, buf, len))
	goto end;

    if (info->reinit) {

	if (xvp_proxy_pixel_format.message_type != 0xff) {
	    len = sizeof(xvp_proxy_pixel_format);
	    xvp_proxy_trace_client(&xvp_proxy_pixel_format, len, true);
	    if (!xvp_proxy_ssl_write(ssl, &xvp_proxy_pixel_format, len))
		goto end;
	}

	if (xvp_proxy_encodings.message_type != 0xff) {
	    len = XVP_PROXY_MAX_ENCODINGS - htons(xvp_proxy_encodings.number);
	    len = sizeof(xvp_proxy_encodings) - len * sizeof(S32);
	    xvp_proxy_trace_client(&xvp_proxy_encodings, len, true);
	    if (!xvp_proxy_ssl_write(ssl, &xvp_proxy_encodings, len))
	    goto end;
	}

	xvp_proxy_fb_request.message_type = 3;
	xvp_proxy_fb_request.incremental = 0;
	xvp_proxy_fb_request.x_position = 0;
	xvp_proxy_fb_request.y_position = 0;
	xvp_proxy_fb_request.width = xvp_proxy_server_details.fb_width;
	xvp_proxy_fb_request.height = xvp_proxy_server_details.fb_height;

	len = sizeof(xvp_proxy_fb_request);
	xvp_proxy_trace_client(&xvp_proxy_fb_request, len, true);
	if (!xvp_proxy_ssl_write(ssl, &xvp_proxy_fb_request, len))
	    goto end;
    }

    *info->sslp = ssl;

    xvp_log(XVP_LOG_DEBUG, "Server handshake successful");

 end:

    sig = SIGCHLD;
    if (write(xvp_child_sigpipe[1], &sig, sizeof(sig)) != sizeof(sig))
	exit(1);

    if (!*info->sslp || !xvp_xenapi_event_wait(info->vm))
	return NULL;

    xvp_log(XVP_LOG_INFO, "Lost connection to console");
    pthread_cancel(xvp_proxy_writer_thread);
    pthread_cancel(xvp_proxy_reader_thread);

    if (xvp_reconnect_delay <= 0) {
	sleep(-xvp_reconnect_delay);
	sig = SIGTERM;
    } else {
	xvp_log(XVP_LOG_INFO, "Reconnect attempt in %d seconds",
		xvp_reconnect_delay);
	sleep(xvp_reconnect_delay);
	sig = SIGPIPE;
    }

    if (write(xvp_child_sigpipe[1], &sig, sizeof(sig)) != sizeof(sig))
	exit(1);

    return NULL;
}

static void xvp_proxy_server_init(xvp_vm *vm, bool shared, bool reinit,
				  SSL **sslp)
{
    static xvp_server_info info;
    pthread_t pt;

    info.shared = shared;
    info.reinit = reinit;
    info.vm = vm;
    info.sslp = sslp;
    if (pthread_create(&pt, NULL, xvp_proxy_server_handshake, &info) != 0)
	xvp_log_errno(XVP_LOG_FATAL, "pthread_create"); 
}

static void xvp_proxy_start_proxying(int client_sock, SSL *server_handle)
{
    static xvp_proxy_handles ph;

    ph.client_sock = client_sock;
    ph.server_handle = server_handle;

    xvp_log(XVP_LOG_DEBUG, "Starting reader-writer threads\n");

    if (pthread_create(&xvp_proxy_writer_thread,
		       NULL, xvp_proxy_writer, &ph) != 0 ||
	pthread_create(&xvp_proxy_reader_thread,
		       NULL, xvp_proxy_reader, &ph) != 0)
	xvp_log_errno(XVP_LOG_FATAL, "pthread_create"); 
}

static int xvp_proxy_mainloop(xvp_vm *vm, int client_sock, unsigned int client_ip)
{
    int sigpipe, nfds, nready, i, len, size;
    fd_set read_fds, write_fds;
    unsigned int major, minor, type, res, challenge[4], response[4];
    unsigned char *user_target;
    bool authok, shared, wrongvm;
    SSL *ssl;
    char buf[XVP_PROXY_BUF_SIZE];
    xvp_pool *pool;
    char *target, *vmname, *username;
    xvp_vm *real_vm;

    sigpipe = xvp_child_sigpipe[0];
    nfds = MAX(sigpipe, client_sock) + 1;

    xvp_proxy_state = XVP_STATE_SERVER_VERSION;
    xvp_proxy_writing = true;
    wrongvm = false;

    while (true) {

	FD_ZERO(&read_fds);
	FD_SET(sigpipe, &read_fds);
	if (xvp_proxy_state != XVP_STATE_IDLING &&
	    xvp_proxy_state != XVP_STATE_CONSOLE_DELETED)
	    FD_SET(client_sock, &read_fds);

	if (xvp_proxy_writing) {
	    FD_ZERO(&write_fds);
	    FD_SET(client_sock, &write_fds);
	}

	nready = select(nfds, &read_fds, xvp_proxy_writing ? &write_fds : NULL,
			NULL, NULL);

	if (nready < 0) {
	    if (errno == EINTR)
		continue;
	    xvp_log_errno(XVP_LOG_FATAL, "select");
	}


	if (FD_ISSET(sigpipe, &read_fds) && !xvp_process_signal_handler()) {
	    return 0;
	} else if (xvp_proxy_state == XVP_STATE_CONSOLE_DELETED) {
	    SSL_shutdown(ssl);
	    xvp_log(XVP_LOG_DEBUG, "Closed old console connection");
	    ssl = NULL;
	    xvp_proxy_state = XVP_STATE_IDLING;
	    xvp_proxy_writing = false;
	    xvp_proxy_server_init(vm, shared, true, &ssl);
	    continue;
	} else if (FD_ISSET(client_sock, &read_fds)) {
	    if (xvp_proxy_writing)
		return 1;
	} else if (!xvp_proxy_writing) {
	    continue;
	} else if (!FD_ISSET(client_sock, &write_fds)) {
	    continue;
	}

	switch (xvp_proxy_state) {
	case XVP_STATE_SERVER_VERSION:
	    sprintf(buf, "RFB %03u.%03u\n",
		    XVP_RFB_MAJOR, XVP_RFB_MINOR_CLIENT);
	    if (!xvp_write_all(client_sock, buf, strlen(buf)))
		return 1;
	    xvp_proxy_state = XVP_STATE_CLIENT_VERSION;
	    xvp_proxy_writing = false;
	    continue;
	case XVP_STATE_CLIENT_VERSION:
	    *buf = '\0';
	    if ((len = xvp_read_line(client_sock, buf, 16)) <= 0)
		return 1;
	    buf[len] = '\0';
	    if (sscanf(buf, "RFB %03u.%03u\n", &major, &minor) != 2 ||
		!xvp_proxy_version_known(major, minor))
		return 1;
	    xvp_log(XVP_LOG_DEBUG,
		    "RFB version %03u.%03u agreed", major, minor);
	    xvp_proxy_minor_version = minor;
	    xvp_proxy_state = XVP_STATE_REQUIRE_AUTH;
	    xvp_proxy_writing = true;
	    break;
	case XVP_STATE_REQUIRE_AUTH:
	    if (xvp_proxy_minor_version == XVP_RFB_MINOR_3) {
		type = htonl(XVP_RFB_SECURITY_VNC);
		if (!xvp_write_all(client_sock, &type, sizeof(type)))
		    return 1;
		xvp_proxy_state = XVP_STATE_CHALLENGE_AUTH;
		xvp_proxy_writing = true;
	    } else {
		char *tp = (char *)&type;
		tp[0] = 2; /* no of sec types */
		tp[1] = XVP_RFB_SECURITY_VNC;
		tp[2] = XVP_RFB_SECURITY_XVP;
		if (!xvp_write_all(client_sock, &type, tp[0] + 1))
		    return 1;
		xvp_proxy_state = XVP_STATE_SELECT_AUTH;
		xvp_proxy_writing = false;
	    }
	    break;
	case XVP_STATE_SELECT_AUTH:
	    if (!xvp_read_all(client_sock, buf, 1) ||
		(*buf != XVP_RFB_SECURITY_VNC && *buf != XVP_RFB_SECURITY_XVP))
		return 1;
	    xvp_proxy_security_type = *buf;
	    xvp_log(XVP_LOG_DEBUG,
		    "RFB security type %u agreed", xvp_proxy_security_type);
	    if (xvp_proxy_security_type == XVP_RFB_SECURITY_XVP) {
		xvp_proxy_state = XVP_STATE_USER_TARGET;
		xvp_proxy_writing = false;
	    } else {
		xvp_proxy_state = XVP_STATE_CHALLENGE_AUTH;
		xvp_proxy_writing = true;
	    }
	    break;
	case XVP_STATE_USER_TARGET:
	    /*
	     * XVP authentication extension to RFB, client sends:
	     *
	     *   U8 user-length
	     *   U8 target-length
	     *   U8 array user-string
	     *   U8 array target-string  ( [pool:]vm)
	     *
	     * and then we proceed as in VNC authentication
	     */
	    user_target = (unsigned char *)buf;
	    if (!xvp_read_all(client_sock, buf, 2))
		return 1;
	    len = user_target[0] + user_target[1];
	    if (len > 0 && !xvp_read_all(client_sock, buf + 2, len))
		return 1;
	    len += 2;
	    buf[len] = '\0';
	    target = xvp_strdup(buf + 2 + user_target[0]);
	    buf[2 + user_target[0]] = '\0';
	    username = xvp_strdup(buf + 2);
	    xvp_log(XVP_LOG_INFO, "XVP auth credentials %s@%s",
		    username, target);

	    if ((vmname = strchr(target, ':'))) {
		*vmname++ = '\0';
		if (!(pool = xvp_config_pool_by_name(target))) {
		    wrongvm = true;
		    xvp_proxy_state = XVP_STATE_CHALLENGE_AUTH;
		    xvp_proxy_writing = true;
		    break;
		}
	    } else {
		vmname = target;
		pool = NULL;
	    }

	    /*
	     * If client connected to multiplex port, only now do we now
	     * which VM it actually wants to connect to.  If it didn't
	     * tell us which one, just let things drop through and fail
	     * at challenge/response.  If not connected to multiplex
	     * port, but target specified, it had better match.
	     */

	    if (xvp_xenapi_is_uuid(vmname))
		real_vm = xvp_config_vm_by_uuid(pool, vmname);
	    else
		real_vm = xvp_config_vm_by_name(pool, vmname);

	    if (vm == xvp_multiplex_vm) {
		if (real_vm) {
		    xvp_proxy_set_name(vm = real_vm);
		    xvp_log(XVP_LOG_INFO,
			    "Multiplexer selecting VM %s in pool %s",
			    vm->vmname, vm->pool->poolname);
		} else {
		    wrongvm = true;
		}
	    } else if ((pool || *vmname) && vm != real_vm) {
		wrongvm = true;
	    }

	    xvp_proxy_state = XVP_STATE_CHALLENGE_AUTH;
	    xvp_proxy_writing = true;
	    break;
	case XVP_STATE_CHALLENGE_AUTH:
	    srandom((unsigned int)(time(NULL) ^ 0xdf214a30));
	    for (i = 0; i < 4; i++) {
		challenge[i] = (random() ^ 0x51a488ce);
	    }
	    if (!xvp_write_all(client_sock, challenge, sizeof(challenge)))
		return 1;
	    xvp_proxy_state = XVP_STATE_RESPONSE_AUTH;
	    xvp_proxy_writing = false;
	    break;
	case XVP_STATE_RESPONSE_AUTH:
	    if (!xvp_read_all(client_sock, response, 16))
		return 1;
	    authok = (vm == xvp_multiplex_vm || wrongvm) ? false :
		xvp_password_vnc_ok(vm->password, client_ip,
				    (char *)challenge, (char *)response);
	    xvp_proxy_state = XVP_STATE_CONFIRM_AUTH;
	    xvp_proxy_writing = true;
	    break;
	case XVP_STATE_CONFIRM_AUTH:
	    res = authok ? 0 : htonl(1); /* VNC security result */
	    if (authok)
		xvp_log(XVP_LOG_DEBUG, "Client authentication succeeded");
	    else
		xvp_log(XVP_LOG_INFO, "Client authentication failed");
	    if (!xvp_write_all(client_sock, &res, sizeof(res)))
		return 1;
	    if (authok) {
		xvp_proxy_state = XVP_STATE_CLIENT_INIT;
		xvp_proxy_writing = false;
	    } else if (xvp_proxy_minor_version <= XVP_RFB_MINOR_7) {
		return 1;
	    } else {
		strcpy(buf + 4, "Access denied");
		len = strlen(buf + sizeof(int));
		*(int *)buf = htonl(len);
		(void)xvp_write_all(client_sock, buf, len + sizeof(int));
		return 1;
	    }
	    break;
	case XVP_STATE_CLIENT_INIT:
	    if (!xvp_read_all(client_sock, buf, 1))
		return 1;
	    shared = (*buf != 0); /* Xen ignores this, always shared */
	    xvp_proxy_state = XVP_STATE_SERVER_CONNECT;
	    xvp_proxy_writing = false;
	    /* this starts background thread which signals when done */
	    xvp_proxy_server_init(vm, shared, false, &ssl);
	    break;
	case XVP_STATE_SERVER_CONNECT:
	    if (FD_ISSET(client_sock, &read_fds))
		return 1;
	case XVP_STATE_SERVER_INIT:
	    if (!ssl)
		return 2;
	    size = sizeof(xvp_proxy_server_details);
	    sprintf(buf + size, "VM Console - %s", vm->vmname);
	    len = strlen(buf + size);
	    xvp_proxy_server_details.name_length = htonl(len);
	    memcpy(buf, &xvp_proxy_server_details, size);
	    if (!xvp_write_all(client_sock, buf, size + len))
		return 1;
	    xvp_proxy_start_proxying(client_sock, ssl);
	    xvp_proxy_state = XVP_STATE_IDLING;
	    xvp_proxy_writing = false;
	    break;
	case XVP_STATE_SERVER_REINIT:
	    if (!ssl)
		return 2;
	    xvp_proxy_start_proxying(client_sock, ssl);
	    xvp_proxy_state = XVP_STATE_IDLING;
	    xvp_proxy_writing = false;
	    break;
	default:
	    xvp_log(XVP_LOG_FATAL, "Internal error: Broken state");
	    break;
	}	
    }
}

void xvp_proxy_resume(void)
{
    switch (xvp_proxy_state) {
    case XVP_STATE_SERVER_CONNECT:
	xvp_proxy_state = XVP_STATE_SERVER_INIT;
	xvp_proxy_writing = true;
	break;
    case XVP_STATE_IDLING:
	xvp_proxy_state = XVP_STATE_SERVER_REINIT;
	xvp_proxy_writing = true; /* just to force next select to return */
	break;
    default:
	xvp_proxy_state = XVP_STATE_BROKEN;
	xvp_proxy_writing = false;
	break;
    }
}

void  xvp_proxy_console_deleted(void)
{
    xvp_proxy_state = XVP_STATE_CONSOLE_DELETED;
    xvp_proxy_writing = false;
}

int xvp_proxy_main(xvp_vm *vm, int client_sock, unsigned int client_ip)
{
    int rc;

    struct hostent *hp;

    if (client_ip == htonl(INADDR_LOOPBACK)) {
	strcpy(client_hostname, "localhost");
    } else if (hp = gethostbyaddr(&client_ip, sizeof(client_ip), AF_INET)) {
	strncpy(client_hostname, hp->h_name, XVP_MAX_HOSTNAME);
    } else {
	strcpy(client_hostname, inet_ntoa(*(struct in_addr *)&client_ip));
    }

    xvp_proxy_set_name(vm);
    xvp_log(XVP_LOG_INFO, "Starting %s", xvp_proxy_get_name());

    xvp_proxy_pixel_format.message_type = 0xff;
    xvp_proxy_encodings.message_type = 0xff;
    xvp_proxy_extensions = false;

    rc = xvp_proxy_mainloop(vm, client_sock, client_ip);

    xvp_log(XVP_LOG_INFO, "Stopping: %s", xvp_proxy_get_name());

    return rc;
}
