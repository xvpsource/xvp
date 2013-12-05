/*
 * xvp.h - header for Xen VNC Proxy
 *
 * Copyright (C) 2009-2013, Colin Dean
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

#define XVP_MAJOR  1
#define XVP_MINOR  16
#define XVP_BUGFIX 0

#ifndef XEN_API_XEN_ALL_H
typedef int bool;
#ifndef true
#define true (1)
#endif
#ifndef false
#define false (0)
#endif
#endif

// Highest RFB versions we will use
#define XVP_RFB_MAJOR 3
#define XVP_RFB_MINOR_CLIENT 8
#define XVP_RFB_MINOR_SERVER 3

// Minor versions we know about
#define XVP_RFB_MINOR_3      3
#define XVP_RFB_MINOR_7      7
#define XVP_RFB_MINOR_8      8

#define XVP_CONFIG_FILENAME "/etc/xvp.conf"
#define XVP_LOG_FILENAME    "/var/log/xvp.log"
#define XVP_PID_FILENAME    "/var/run/xvp.pid"

#define XVP_VNC_PORT_MIN 5900
#define XVP_VNC_PORT_MAX 5999
#define XVP_VNC_LISTEN_BACKLOG 10
#define XVP_CONNECT_TIMEOUT 10
#define XVP_RECONNECT_DELAY 20

typedef enum {
    XVP_OTP_DENY,
    XVP_OTP_ALLOW,
    XVP_OTP_REQUIRE
} xvp_otp;

typedef enum {
    XVP_IPCHECK_OFF,
    XVP_IPCHECK_ON,
    XVP_IPCHECK_HTTP
} xvp_ipcheck;

#define XVP_OTP_MODE XVP_OTP_ALLOW
#define XVP_OTP_IPCHECK XVP_IPCHECK_OFF
#define XVP_OTP_WINDOW 60
#define XVP_OTP_MAX_WINDOW 3600

#define XVP_MAX_POOL     80
#define XVP_MAX_MANAGER  32
#define XVP_MAX_HOSTNAME 80 /* should be >= MAXHOSTNAMELEN */
#define XVP_MAX_ADDRESS  15 /* long enough for any dotted IPv4 address */
#define XVP_MAX_XEN_PW   16 /* 2 x sizeof DES_cblock */
#define XVP_MAX_VNC_PW    8 /* 1 x sizeof DES_cblock */
#define XVP_UUID_LEN     36 /* length of a XenServer UUID */
#define XVP_UUID_DASHES  { 8, 13, 18, 23 } /* pos of dashes in UUIDs */
#define XVP_UUID_NDASHES  4 /* how many dashes in a UUID */

typedef struct xvp_pool xvp_pool;
typedef struct xvp_host xvp_host;
typedef struct xvp_vm   xvp_vm;

struct xvp_pool {
    struct xvp_pool *next;
    xvp_host        *hosts;
    xvp_vm          *vms;
    char             poolname[XVP_MAX_POOL + 1];
    char             domainname[XVP_MAX_HOSTNAME + 1];
    char             manager[XVP_MAX_MANAGER + 1];
    char             password[XVP_MAX_XEN_PW + 1];
};

struct xvp_host {
    struct xvp_pool *pool;
    struct xvp_host *next;
    int              hostname_is_ipv4; /* odd behaviour when used "bool" */
    char             hostname[XVP_MAX_HOSTNAME + 1];
    char             address[XVP_MAX_ADDRESS + 1];
    char             uuid[XVP_UUID_LEN + 1];
};

struct xvp_vm {
    struct xvp_pool *pool;
    struct xvp_vm   *next;
    int              sock;
    unsigned short   port;
    char             vmname[XVP_MAX_HOSTNAME + 1];
    char             password[XVP_MAX_VNC_PW + 1];
    char             uuid[XVP_UUID_LEN + 1];
};

typedef struct {
    int sock;
    unsigned int addr;
} xvp_client;

typedef enum {
    XVP_PASSWORD_XEN,
    XVP_PASSWORD_VNC
} xvp_password_type;

typedef enum {
    XVP_LOG_DEBUG,
    XVP_LOG_INFO,
    XVP_LOG_ERROR,
    XVP_LOG_FATAL
} xvp_log_type;

typedef enum {
    XVP_MESSAGE_CODE_FAIL     = 0,
    XVP_MESSAGE_CODE_INIT     = 1,
    XVP_MESSAGE_CODE_SHUTDOWN = 2,
    XVP_MESSAGE_CODE_REBOOT   = 3,
    XVP_MESSAGE_CODE_RESET    = 4
} xvp_message_code;

extern char       *xvp_config_filename;
extern char       *xvp_log_filename;
extern char       *xvp_pid_filename;
extern bool        xvp_daemon;
extern int         xvp_verbose;
extern int         xvp_tracing;
extern bool        xvp_reconnect_delay;
extern xvp_pool   *xvp_pools;
extern int         xvp_log_fd;
extern pid_t       xvp_pid;
extern pid_t       xvp_child_pid;
extern int         xvp_master_sigpipe[2];
extern int         xvp_child_sigpipe[2];
extern bool        xvp_vm_is_host;
extern xvp_otp     xvp_otp_mode;
extern xvp_ipcheck xvp_otp_ipcheck;
extern int         xvp_otp_window;    
extern xvp_vm     *xvp_multiplex_vm;

extern void     *xvp_alloc(int size);
extern char     *xvp_strdup(char *s);
extern bool      xvp_is_ipv4(char *address);
extern void      xvp_free(void *p);
extern char     *xvp_xmlescape(char *text, char *buf, int buflen);

extern void      xvp_listen_init(void);
extern char     *xvp_message_code_to_text(int code);

extern void      xvp_config_init(void);
extern xvp_pool *xvp_config_last_pool(void);
extern xvp_pool *xvp_config_pool_by_name(char *poolname);
extern xvp_host *xvp_config_last_host(xvp_pool *pool);
extern xvp_host *xvp_config_host_by_name(xvp_pool *pool, char *hostname);
extern xvp_vm   *xvp_config_last_vm(xvp_pool *pool);
extern xvp_vm   *xvp_config_vm_by_name(xvp_pool *pool, char *vmname);
extern xvp_vm   *xvp_config_vm_by_uuid(xvp_pool *pool, char *uuid);
extern xvp_vm   *xvp_config_vm_by_port(int port);
extern xvp_vm   *xvp_config_vm_by_sock(int sock);

extern void      xvp_log_init(void);
extern void      xvp_log(xvp_log_type type, char *format, ...);
extern void      xvp_log_errno(xvp_log_type type, char *format, ...);
extern void      xvp_log_close(void);

extern void      xvp_password_encrypt(char *src, char *dst, xvp_password_type type);
extern void      xvp_password_decrypt(char *src, char *dst, xvp_password_type type);
extern bool      xvp_password_hex_to_text(char *hex, char *text, xvp_password_type type);
extern bool      xvp_password_text_to_hex(char *text, char *hex, xvp_password_type type);
extern bool      xvp_password_vnc_ok(char *password, unsigned int client_ip, char *challenge, char *response);

extern void      xvp_process_init(int argc, char **argv, char **envp);
extern void      xvp_process_set_name(char *process_name);
extern char     *xvp_process_get_name(void);
extern bool      xvp_process_spawn(xvp_vm *vm);
extern void      xvp_process_cleanup(void);
extern bool      xvp_process_signal_handler(void);

extern int       xvp_proxy_main(xvp_vm *vm, int client_sock, unsigned int client_ip);
extern void      xvp_proxy_dump(void);
extern void      xvp_proxy_resume(void);
extern void      xvp_proxy_console_deleted(void);
extern void     *xvp_xenapi_open_stream(xvp_vm *vm);
extern bool      xvp_xenapi_event_wait(xvp_vm *vm);
extern bool      xvp_xenapi_handle_message_code(int code);
extern bool      xvp_xenapi_is_uuid(char *text);
