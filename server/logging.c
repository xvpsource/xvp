/*
 * logging.c - logging handling for Xen VNC Proxy
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "xvp.h"

#define XVP_LOG_LINEBUF_SIZE 4096

char *xvp_log_filename = XVP_LOG_FILENAME;
bool  xvp_verbose = false;
bool  xvp_tracing = false;
int   xvp_log_fd = -1;

static FILE *stream = NULL;

void xvp_log_init(void)
{
    bool reinit = false;

    if (stream != NULL && xvp_log_fd != 1) {
	reinit = true;
	xvp_log(XVP_LOG_INFO, "Closing log file on signal");
	fclose(stream);
    }

    if (!strcmp(xvp_log_filename, "-")) {
	stream = stdout;
	xvp_log_fd = 1;
    } else if (!(stream = fopen(xvp_log_filename, "a"))) {
	perror(xvp_log_filename);
	exit(1);
    } else {
	xvp_log_fd = fileno(stream);
    }

    setlinebuf(stream);

    if (reinit)
	xvp_log(XVP_LOG_INFO, "Re-opening log file on signal");
}

void xvp_log(xvp_log_type type, char *format, ...)
{
    char buf[XVP_LOG_LINEBUF_SIZE], tbuf[16], *typename;
    va_list ap;
    time_t now = time(NULL);

    if (!xvp_verbose && type == XVP_LOG_DEBUG)
	return;

    switch (type) {
    case XVP_LOG_DEBUG:
	typename = "Debug:";
	break;
    case XVP_LOG_INFO:
	typename = "Info: ";
	break;
    case XVP_LOG_ERROR:
	typename = "Error:";
	break;
    case XVP_LOG_FATAL:
	typename = "Fatal:";
	break;
    default:
	typename = "Oops: ";
	break;
    }

    strftime(tbuf, sizeof(tbuf), "%b %e %T", localtime(&now));
    sprintf(buf, "%s xvp[%d]: %s ", tbuf, xvp_pid, typename);

    va_start(ap, format);
    (void)vsprintf(buf + strlen(buf), format, ap);
    va_end(ap);

    if (!strchr(buf, '\n'))
	strcat(buf, "\n");

    fputs(buf, stream ? stream : stdout);

    if (type == XVP_LOG_FATAL) {
	xvp_process_cleanup();
	xvp_log_close();
	exit(1);
    }
}

void xvp_log_errno(xvp_log_type type, char *format, ...)
{
    char buf[XVP_LOG_LINEBUF_SIZE];
    int errnum = errno, len;

    va_list ap;
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);

    if ((len = strlen(buf)) > 0 && buf[len-1] == '\n')
	buf[len-1] = '\0';

    xvp_log(type, "%s: %s", buf, strerror(errnum));
}

void xvp_log_close(void)
{
    if (stream != NULL && xvp_log_fd > 2)
	(void)fclose(stream);
    stream = NULL;
}
