/*
 * process.c - process handling for Xen VNC Proxy
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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "xvp.h"

char  *xvp_pid_filename = XVP_PID_FILENAME;
bool   xvp_daemon = true;
pid_t  xvp_pid, xvp_child_pid = -1;
int    xvp_master_sigpipe[2];
int    xvp_child_sigpipe[2];

static char *xvp_process_name;
static int xvp_process_maxlen = 0;

static void xvp_process_background(void)
{
    pid_t pid = fork();
    int fd;

    switch (pid) {
    case -1:
	perror("fork");
	exit(1);
	break;
    case 0:
	xvp_pid = getpid();
	(void)setsid();

	for (fd = getdtablesize() - 1; fd >= 0; fd--)
	    if (fd != xvp_log_fd)
		(void)close(fd);

	(void)open("/dev/null", O_RDONLY);
	(void)open("/dev/null", O_WRONLY);
	(void)open("/dev/null", O_WRONLY);

	break;
    default:
	exit(0);
	break;
    }
}

static void xvp_process_write_pidfile(void)
{
    FILE *stream = fopen(xvp_pid_filename, "w");

    if (stream != NULL && fprintf(stream, "%d\n", xvp_pid) > 0) {
	fclose(stream);
	return;
    }

    xvp_log_errno(XVP_LOG_FATAL, "%s", xvp_pid_filename);
}

static void xvp_process_delete_pidfile(void)
{
    struct stat buf;

    if (stat(xvp_pid_filename, &buf) == 0 && S_ISREG(buf.st_mode))
	(void)unlink(xvp_pid_filename);
}

static void xvp_process_signal_pipe(int sig)
{
    int fd = (xvp_child_pid ? xvp_master_sigpipe[1] : xvp_child_sigpipe[1]);
    (void)write(fd, &sig, sizeof(sig));
}

static bool xvp_process_signal_children(int sig)
{
    if (xvp_child_pid) {
	signal(sig, SIG_IGN);
	killpg(xvp_pid, sig);
	signal(sig, xvp_process_signal_pipe);
    }
}

void xvp_process_init(int argc, char **argv, char **envp)
{
    /*
     * On Linux we can safely overwrite argv[0] for the benefit of "ps",
     * as the environment strings are directly after the argv strings in
     * memory, provided we relocate the environment first, take care not
     * to walk off the end of the original environment, and discard
     * argv[1] onwards.
     */

    int i;
    char *last;

    for (i = 0; envp[i]; i++) {
	last = envp[i];
	envp[i] = xvp_strdup(envp[i]);
    }

    xvp_process_name = argv[0];
    if ((xvp_process_maxlen = last + strlen(last) - argv[0]) > 0) {
	xvp_process_set_name("xvp: master");
	for (i = 1; i < argc; i++)
	    argv[i] = NULL;
    }

    xvp_pid = getpid();
    (void)setsid();

    if (xvp_daemon)
	xvp_process_background();

    if (pipe(xvp_master_sigpipe) != 0)
	xvp_log_errno(XVP_LOG_FATAL, "Unable to create master pipe");

    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP,  xvp_process_signal_pipe);
    signal(SIGINT,  xvp_process_signal_pipe);
    signal(SIGQUIT, xvp_process_signal_pipe);
    signal(SIGUSR1, xvp_process_signal_pipe);
    signal(SIGUSR2, xvp_process_signal_pipe);
    signal(SIGCHLD, xvp_process_signal_pipe);
    signal(SIGTERM, xvp_process_signal_pipe);

    xvp_process_write_pidfile();
}

void xvp_process_set_name(char *process_name)
{
    if (xvp_process_maxlen > 0)
	strncpy(xvp_process_name, process_name, xvp_process_maxlen);
}

char *xvp_process_get_name(void)
{
    return xvp_process_name;
}

bool xvp_process_spawn(xvp_vm *vm)
{
    int client_sock, client_ip, fd;
    struct sockaddr_in client_addr;
    socklen_t len;

    len = sizeof(client_addr);
    client_sock = accept(vm->sock, (struct sockaddr *)&client_addr, &len);
    if (client_sock == -1)
	return;

    client_ip = client_addr.sin_addr.s_addr;

    if (pipe(xvp_child_sigpipe) != 0) {
	xvp_log_errno(XVP_LOG_ERROR, "Unable to create child pipe");
	return false;
    }

    /*
     * To try to avoid race conditions, use xvp_child pid
     * being zero as unambiguously indicating running in
     * a spawned process, not the master.
     */
    switch (xvp_child_pid = fork()) {
    case 0: /* child */
	xvp_pid = getpid();
	for (fd = getdtablesize() - 1; fd > 2; fd--)
	    if (fd != client_sock && fd != xvp_log_fd && 
		fd != xvp_child_sigpipe[0] && fd != xvp_child_sigpipe[1])
		close(fd);
	signal(SIGQUIT, SIG_IGN); /* used as internal signal */
	signal(SIGCHLD, SIG_IGN); /* used as internal signal */
	exit(xvp_proxy_main(vm, client_sock, client_ip));
	break;
    case -1:
	xvp_log_errno(XVP_LOG_ERROR, "Unable to spawn process for %s -> %s",
		      inet_ntoa(*(struct in_addr *)&client_ip), vm->vmname);
	close(xvp_child_sigpipe[0]);
	close(xvp_child_sigpipe[1]);
	close(client_sock);
	return false;
	break;
    default:
	close(xvp_child_sigpipe[0]);
	close(xvp_child_sigpipe[1]);
	close(client_sock);
	xvp_log(XVP_LOG_DEBUG, "Spawned process %d", xvp_child_pid);
	break;
    }

    return true;
}

void xvp_process_cleanup(void)
{
    if (xvp_child_pid) {
	signal(SIGCHLD, SIG_IGN);
	xvp_process_signal_children(SIGTERM);
	xvp_process_delete_pidfile();
    }
}

bool xvp_process_signal_handler(void)
{
    int fd, sig, status;
    pid_t pid;

    fd = (xvp_child_pid ? xvp_master_sigpipe[0]: xvp_child_sigpipe[0]);
    if (read(fd, &sig, sizeof(sig)) != sizeof(sig))
	xvp_log_errno(XVP_LOG_FATAL, "Error reading signal pipe");

    switch (sig) {
    case SIGHUP: /* re-open log files */
	xvp_log_init();
	if (xvp_child_pid)
	    xvp_process_signal_children(sig);
	break;

    case SIGINT:  /* terminate cleanly, parent sorts out children */
	if (!xvp_child_pid)
	    break;
	/* drop thru */
    case SIGTERM: /* terminate cleanly */
	xvp_log(XVP_LOG_INFO,
		"Terminating on signal %d (%s)", sig, sys_siglist[sig]);
	return false;
	break;

    case SIGPIPE:
	if (!xvp_child_pid) { /* child only - server console deleted */
	    xvp_proxy_console_deleted();
	}
	break;

    case SIGUSR1:
	if (xvp_child_pid) { /* master only - re-read config file */
	    xvp_config_init();
	    xvp_listen_init();
	}
	break;

    case SIGUSR2: /* dump current connections to log */
	if (xvp_child_pid) {
	    xvp_log(XVP_LOG_INFO, "Dumping active session list");
	    xvp_process_signal_children(sig);
	} else {
	    xvp_proxy_dump();
	}
	break;

    case SIGQUIT:
	if (xvp_child_pid) { /* master - kill children only */
	    xvp_log(XVP_LOG_INFO, "Disconnecting all active sessions");
	    xvp_process_signal_children(SIGTERM);
	} else { /* child - end main loop */
	    return false;
	}
	break;

    case SIGCHLD:
	if (xvp_child_pid) { /* master - reap child */
	    if ((pid = wait(&status)) < 0)
		xvp_log_errno(XVP_LOG_ERROR, "Wait failed");
	    else if (WIFEXITED(status))
		xvp_log(XVP_LOG_DEBUG,
			"Child %d exited %d", pid, WEXITSTATUS(status));
	    else if (WIFSIGNALED(status))
		xvp_log(XVP_LOG_ERROR,
			"Child %d terminated by %s",
			pid, sys_siglist[WTERMSIG(status)]);
	    else
		xvp_log(XVP_LOG_ERROR,
			"Child %d terminated with unexpected status 0x%x",
			pid, status);
	} else { /* child - stop idling, sub-thread has completed task */
	    xvp_proxy_resume();
	}
	break;

    default:
	xvp_log(XVP_LOG_ERROR,
		"Unexpected signal %d (%s)", sig, sys_siglist[sig]);
	break;
    }

    return true;
}
