/*****************************************************************************
           Netlinks prototypes header file for Amnuts version 2.1.1
        Copyright (C) Andrew Collington - Last updated: 25th May, 1999
 *****************************************************************************/


/* Object functions for Netlinks */

NL_OBJECT create_netlink args((void));
void      destruct_netlink args((NL_OBJECT nl));

/* Connection functions */

void      init_connections args((void));
int       connect_to_site args((NL_OBJECT nl));

/* Event functions relating to netlinks */

void      check_nethangs_send_keepalives args((void));

/* NUTS Netlink protocol */

void      accept_server_connection args((int sock,struct sockaddr_in acc_addr));
void      exec_netcom args((NL_OBJECT nl, char *inpstr));
void      nl_transfer args((NL_OBJECT nl,char *name,char *pass,int lev,char *inpstr));
void      nl_release args((NL_OBJECT nl,char *name));
void      nl_action args((NL_OBJECT nl,char *name, char *inpstr));
void      nl_granted args((NL_OBJECT nl,char *name));
void      nl_denied args((NL_OBJECT nl,char *name, char *inpstr));
void      nl_mesg args((NL_OBJECT nl,char *name));
void      nl_prompt args((NL_OBJECT nl,char *name));
void      nl_verification args((NL_OBJECT nl,char *w2,char *w3,int com));
void      nl_removed args((NL_OBJECT nl,char *name));
void      nl_error args((NL_OBJECT nl));
void      nl_checkexist args((NL_OBJECT nl,char *to,char *from));
void      nl_user_notexist args((NL_OBJECT nl,char *to,char *from));
void      nl_user_exist args((NL_OBJECT nl,char *to,char *from));
void      nl_mail args((NL_OBJECT nl,char *to,char *from));
void      nl_endmail args((NL_OBJECT nl));
void      nl_mailerror args((NL_OBJECT nl,char *to,char *from));
void      nl_rstat args((NL_OBJECT nl,char *to));
void      shutdown_netlink args((NL_OBJECT nl));

/* User commands relating to netlink functions */

void      home args((UR_OBJECT user));
void      netstat args((UR_OBJECT user));
void      netdata args((UR_OBJECT user));
void      connect_netlink args((UR_OBJECT user));
void      disconnect_netlink args((UR_OBJECT user));
void      remote_stat args((UR_OBJECT user));

