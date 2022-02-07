/* nwserv.c 19-May-98 */
/* MAIN Prog for NWSERV + NWROUTED  */

/* (C)opyright (C) 1993,1998  Martin Stover, Marburg, Germany
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "net.h"
#include "nwserv.h"

#ifdef LINUX
#  include <netdb.h>
#endif

uint32     internal_net  = 0x0L;     /* NETWORKNUMMER INTERN (SERVER) */
int        no_internal   =   0;      /* no use of internal net        */
int        auto_detect_interfaces=0;

ipxAddr_t  my_server_adr;            /* Address of this server        */
char       my_nwname[50];            /* Name of this server           */
int        print_route_tac   = 0;    /* every x broadcasts print it   */
int        print_route_mode  = 0;    /* append                        */
char       *pr_route_info_fn = NULL; /* filename                      */
int        wdogs_till_tics   = 0;    /* send wdogs to all             */
time_t     acttime_stamp     = 0;    /* actual received time (second) */


/* <========== DEVICES ==========> */
int           count_net_devices=0;
int           max_net_devices=0;
NW_NET_DEVICE **net_devices=NULL;

#if !IN_NWROUTED
  uint16  ipx_sock_nummern[]={  SOCK_AUTO    /* WDOG    */
# ifdef EXTERN_SLOT
                             ,SOCK_EXTERN  /* Xmarsmon */
# endif

# ifdef PSERVER_SLOT
                             ,SOCK_PSERVER
# endif

# if INTERNAL_RIP_SAP
                             ,SOCK_SAP
# else
                             ,SOCK_AUTO
# endif
#else
  uint16  ipx_sock_nummern[]={SOCK_SAP
#endif

#ifdef RIP_SLOT
                             ,SOCK_RIP
#endif

#ifdef ROUTE_SLOT
                             ,SOCK_ROUTE
#endif
#ifdef DIAG_SLOT
                             ,SOCK_DIAGNOSE
#endif

#ifdef ECHO_SLOT
                             ,SOCK_ECHO
#endif
#ifdef ERROR_SLOT
                             ,SOCK_ERROR
#endif
                             };

#define NEEDED_SOCKETS  (sizeof(ipx_sock_nummern) / sizeof(uint16))

#if IN_NWROUTED
#  define NEEDED_POLLS    (NEEDED_SOCKETS+1)
#else
#  define NEEDED_POLLS    (NEEDED_SOCKETS+2)
#endif

static uint16        sock_nummern [NEEDED_SOCKETS];

int                  sockfd       [NEEDED_SOCKETS];
static struct        pollfd  polls[NEEDED_POLLS];
static uint16        spx_diag_socket;    /* SPX DIAGNOSE SOCKET       */
static int           ipxdebug       =  0;
static int           pid_ncpserv    = -1;
static int           fd_ncpserv_in  = -1;  /* ctrl-pipe in from ncpserv */

static int           pid_nwbind     = -1;

#if !IN_NWROUTED
static int           sock_nwbind    = -1;
#endif

static int           fd_nwbind_in   = -1;  /* ctrl-pipe in from nnwbind */

static  int          broadmillisecs            =  2000; /* 2 sec */
static  time_t       server_down_stamp         =  0;
static  int          server_goes_down_secs     = 10;
static  int          server_broadcast_secs     = 60;
static  int          ipx_flags                 =  0;
static  int          handle_all_sap_typs=HANDLE_ALL_SAP_TYPS;
static  int          nearest_request_flag=0;

#if IN_NWROUTED
static  char         *prog_name_typ="ROUTER";
#define   IN_PROG        NWROUTED
#else
static  char         *prog_name_typ="SERVER";
#define   IN_PROG        NWSERV
#endif

#if !IN_NWROUTED
static  int        ipx_out_fd=-1;
/* next should be '1', is for testing only */
#define USE_PERMANENT_OUT_SOCKET  1

static void add_wdata(IPX_DATA *d, char *data, int size)
{
  memcpy(d->owndata.d.data+d->owndata.d.size, data, size);
  d->owndata.d.size+=size;
}

static void write_wdata(IPX_DATA *d, int what, int sock)
{
  int  fd = (ipx_out_fd > -1) ? ipx_out_fd : open_ipx_socket(NULL,  0);
  if (fd > -1) {
    ipxAddr_t  toaddr;
    d->owndata.d.function=what;
    d->owndata.d.size +=sizeof(int);
    memcpy(&toaddr, &my_server_adr, sizeof(ipxAddr_t));
    U16_TO_BE16(sock, toaddr.sock);
    if (send_own_data(fd, d, &toaddr)) {
      errorp(0, "write_wdata", "to %s",
            (sock == SOCK_NCP) ? "NCPSERV" : "NWBIND" );
    }
    d->owndata.d.size=0;
    if (ipx_out_fd != fd) {
      t_unbind(fd);
      t_close(fd);
    }
  } else {
    errorp(0, "write_wdata", "fd not open");
  }
}

static void write_to_sons(int what, int connection,
                           char *data, int data_size, int sock)
{
  IPX_DATA ipxd;
  ipxd.owndata.d.size = 0;
  XDPRINTF((2, 0, "write_to_sons what=0x%x, conn=%d, data_size=%d",
           what, connection, data_size));

  switch (what) {
    case 0x2222  : /* insert connection */
            add_wdata(&ipxd, (char*) &connection,  sizeof(int));
            add_wdata(&ipxd, (char*) &data_size,   sizeof(int));
            add_wdata(&ipxd, data, data_size);
         break;

    case 0x3333  : /* 'bindery' calls  */
            add_wdata(&ipxd,  (char*)&data_size, sizeof(int));
            add_wdata(&ipxd, data, data_size);
         break;

    case 0x5555  : /* kill connection  */
            add_wdata(&ipxd, (char*) &connection,  sizeof(int));
         break;

    case 0xeeee  : /* hup, read init */
         break;

    case 0xffff  : /* 'down server' */
           add_wdata(&ipxd, (char*) &what,  sizeof(int));
         break;

    default : return;
  }
  write_wdata(&ipxd, what, sock);
}

#define write_to_ncpserv(what, connection, data, data_size) \
   write_to_sons((what), (connection), (data), (data_size), SOCK_NCP)

#define write_to_nwbind(what, connection, data, data_size) \
   write_to_sons((what), (connection), (data), (data_size), sock_nwbind)

void ins_del_bind_net_addr(uint8 *name, int styp, ipxAddr_t *adr)
{
  uint8 buf[1024];
  uint8 *p  = buf;
  int   len = 0;
  if (NULL != adr) { /* insert */
    *p=0x01;
    p+=2; len+=2;
    U16_TO_BE16(styp, p);
    p+=2; len+=2;
    *p = strlen((char*)name);
    strmaxcpy(p+1, name, *p);
    len += (*p+1); p+=(*p + 1);
    memcpy(p, adr, sizeof(ipxAddr_t));
    len+=sizeof(ipxAddr_t);
  } else {  /* delete */
    *p=0x02;
    p+=2; len+=2;
    U16_TO_BE16(styp, p);
    p+=2; len+=2;
    *p = strlen((char*)name);
    strmaxcpy(p+1, name, *p);
    len += (*p+1); p+=(*p + 1);
  }
  write_to_nwbind(0x3333, 0, (char *)buf, len);
}

#else
# define USE_PERMANENT_OUT_SOCKET  0
# define write_to_ncpserv(what, connection, data, data_size) /* */
# define write_to_nwbind(what, connection, data, data_size) /* */
#endif

static int loc_open_ipx_socket(int sock_nr, int nr)
{
  int ipx_fd=open_ipx_socket(&my_server_adr, sock_nr);
  if (ipx_fd > -1) {
    sock_nummern[nr] = GET_BE16(my_server_adr.sock); /* really socket nmbr */
    if (nw_debug)
      print_ipx_addr(&my_server_adr);
  } else
    errorp(0, "loc_open_ipx_socket", "nr=%d", sock_nr);
  return(ipx_fd);
}


static int start_ncpserv(char *nwname, ipxAddr_t *addr)
{
#if !IN_NWROUTED
  int fds_in[2];
  int pid;
  if (pipe(fds_in) < 0)
     return(-1);

  switch (pid=fork()) {
    case 0 : {  /* new Process */
               char *progname="ncpserv";
               char addrstr[100];
               char pathname[300];
               char nwbindsock[20];
               char echosock[20];
               int j = FD_NWSERV;
               close(fds_in[0]);            /* no need to read       */
               dup2(fds_in[1], FD_NWSERV);  /* becommes fd FD_NWSERV */
               close(fds_in[1]);            /* no  longer needed     */
               while (j++ < 100) close(j);  /* close all > 4         */
               U16_TO_BE16(SOCK_NCP, addr->sock);
               ipx_addr_to_adr(addrstr, addr);
               sprintf(nwbindsock, "%04x", sock_nwbind);
               sprintf(echosock,   "%04x", sock_nummern[WDOG_SLOT]);
               execl(get_exec_path(pathname, progname), progname,
                      nwname,  addrstr, nwbindsock, echosock, NULL);
               exit(1);
             }
             break;

    case -1:
             close(fds_in[0]);
             close(fds_in[1]);
             return(-1);  /* error */
  }
  close(fds_in[1]);
  fd_ncpserv_in  = fds_in[0];
  pid_ncpserv    = pid;
  sleep(2);
#endif
  return(0); /*  OK */
}

static int start_nwbind(char *nwname)
{
#if !IN_NWROUTED
  int       fds_in[2];
  int       pid;
  ipxAddr_t addr;
  int    ipx_fd=open_ipx_socket(&addr, 0);
  if (ipx_fd < 0) {
    errorp(1, "start_nwbind", NULL);
    return(-1);
  }
  if (pipe(fds_in) < 0){
    errorp(1, "start_nwbind", "pipe");
    t_close(ipx_fd);
    return(-1);
  }
  sock_nwbind  = (int) GET_BE16(addr.sock);
  switch (pid=fork()) {
    case 0 : {  /* new Process */
               char    *progname="nwbind";
               char    addrstr[100];
               char    pathname[300];
               char    nwbindsock[20];
               int j = FD_NWSERV;

               close(fds_in[0]);              /* no need to read       */
               if (fds_in[1] != FD_NWSERV) {
                 dup2(fds_in[1], FD_NWSERV);  /* becommes fd FD_NWSERV */
                 close(fds_in[1]);            /* no  longer needed     */
               }
               dup2(ipx_fd, 0);               /* stdin                 */
               close(ipx_fd);

               while (j++ < 100) close(j);    /* close all > FD_NWSERV  */
               U16_TO_BE16(SOCK_NCP, addr.sock);
               ipx_addr_to_adr(addrstr, &addr);
               sprintf(nwbindsock, "%04x", sock_nwbind);
               execl(get_exec_path(pathname, progname), progname,
                      nwname,  addrstr,  nwbindsock, NULL);
               exit(1);
             }
             break;

    case -1: close(fds_in[0]);
             close(fds_in[1]);
             t_close(ipx_fd);
             errorp(1, "start_nwbind", "t_bind");
             return(-1);  /* error */
  }
  close(fds_in[1]);
  close(ipx_fd);
  fd_nwbind_in = fds_in[0];
  pid_nwbind   = pid;
  sleep(2);
#endif
  return(0); /*  OK */
}

#if !IN_NWROUTED

/* ===========================  WDOG =============================== */
#ifndef _WDOG_TESTING_
#define _WDOG_TESTING_  0
#endif

#if _WDOG_TESTING_
/* for testing */
# define WDOG_TRIE_AFTER_SEC   1
# define MAX_WDOG_TRIES        1
#else
# define WDOG_TRIE_AFTER_SEC   60  /* ca. 1 min.    */
# define MAX_WDOG_TRIES        3   /* should be enough */
#endif

static void modify_wdog_conn(int conn, int mode);

static void send_wdog_packet(ipxAddr_t *addr, int conn, int what)
{
  uint8     data[2];
  data[0] = (uint8) conn;
  data[1] = (uint8) what;
  if (what == '?' && dont_send_wdog(addr)) {
    modify_wdog_conn(conn, 0);
    XDPRINTF((2,0, "No wdog to %s", visable_ipx_adr(addr)));
  } else
    send_ipx_data(sockfd[WDOG_SLOT], 17, 2, (char*)data, addr, "WDOG");
}

static void send_bcast_packet(ipxAddr_t *addr, int conn, int signature)
{
  uint8     data[2];
  data[0] = (uint8) conn;
  data[1] = (uint8) signature;
  /*  signatures
   *  '!' = broadcast waiting inform
   *  '@' = sft_iii server change over inform.
   */
  send_ipx_data(sockfd[WDOG_SLOT], 17, 2, (char*)data, addr, "BCAST");
}

typedef struct {
  ipxAddr_t  addr;        /* address of client             */
  time_t     last_time;   /* time of last wdog packet sent */
  int        counter;     /* max. 11 packets               */
} CONNECTION;

static int max_connections=MAX_CONNECTIONS;
static CONNECTION *connections=NULL;
static int hi_conn=0;     /* highest connection nr in use */

static void insert_wdog_conn(int conn, ipxAddr_t *adr)
{
  if (conn > 0 && conn <= max_connections) {
    CONNECTION *c;
    while (hi_conn < conn) {
      c=&(connections[hi_conn++]);
      memset(c, 0, sizeof(CONNECTION));
    }
    c=&(connections[conn-1]);
    c->last_time = acttime_stamp;
    c->counter   = 0;
    if (NULL != adr) memcpy(&(c->addr), adr, sizeof(ipxAddr_t));
  }
}

static void modify_wdog_conn(int conn, int mode)
/* mode =  0  : reset        */
/* mode =  1  : activate     */
/* mode = 99  : remove wdog  */
{
  if (conn > 0 && --conn < hi_conn) {
    CONNECTION *c=&(connections[conn]);
    if (mode < 99) {
      switch (mode) {
        case 1  : /* activate Wdog */
                  if (!c->counter) c->counter=1;
                  c->counter = max(c->counter, MAX_WDOG_TRIES-1);
                  c->last_time    =  1;
                  break;

        default : c->counter      =  0;  /* reset */
                  c->last_time    = acttime_stamp;
                  break;
      } /* switch */
    } else if (mode == 99) {  /* remove */
      memset(c, 0, sizeof(CONNECTION));
      if (conn + 1 == hi_conn) {
        while (hi_conn) {
          c=&(connections[hi_conn-1]);
          if (!c->last_time) hi_conn--;
          else break;
        }
      }
    }
  }
}

static void send_wdogs()
{
  int  k  = hi_conn;
  while (k--) {
    CONNECTION  *c = &(connections[k]);
    if (c->last_time) {
      time_t t_diff = acttime_stamp - c->last_time;
      if (   (c->counter && t_diff > 50)
          || t_diff > WDOG_TRIE_AFTER_SEC) { /* max. 1 minute */
        if (c->counter > MAX_WDOG_TRIES) {
          /* now its enough with trying */
          /* clear connection */
          write_to_ncpserv(0x5555, k+1, NULL, 0);
        } else {
          ipxAddr_t adr;
          c->last_time = acttime_stamp;
          memcpy(&adr, &(c->addr), sizeof(ipxAddr_t));
          U16_TO_BE16(GET_BE16(adr.sock)+1, adr.sock);
          send_wdog_packet(&adr, k+1, '?');
        }
        c->counter++;
      }
    }
  }
}

static void send_bcasts(int conn)
{
  if (conn > 0 && --conn < hi_conn) {
    CONNECTION  *c = &(connections[conn]);
    ipxAddr_t adr;
    memcpy(&adr, &(c->addr), sizeof(ipxAddr_t));
    U16_TO_BE16(GET_BE16(adr.sock)+2, adr.sock);
    send_bcast_packet(&adr, conn+1, '!');  /* notify */
  }
}
#endif


static void handle_sap(int fd,
                int        ipx_pack_typ,
                int        data_len,
                IPX_DATA   *ipxdata,
                ipxAddr_t  *from_addr)
{
  int query_type   = GET_BE16(ipxdata->sqp.query_type);
  int server_type  = GET_BE16(ipxdata->sqp.server_type);
  int flag=0;

  if (query_type == 3) {
    XDPRINTF((2,0,"SAP NEAREST SERVER request typ=%d from %s",
             server_type, visable_ipx_adr(from_addr)));
    /* Get Nearest File Server */
    if (!nearest_request_flag)
      send_server_response(4, server_type, from_addr);
#if INTERNAL_RIP_SAP
    else {
      int  do_sent = (nearest_request_flag == 1) ? 1 : 0;
      if (find_station_match(1, from_addr)) do_sent = !do_sent;

      if (do_sent)
        send_server_response(4, server_type, from_addr);
      XDPRINTF((3,0, "SAP NEAREST REQUEST =%d, nearest_request_flag=%d",
                       do_sent, nearest_request_flag));
    }
#endif
  } else if (query_type == 1) {  /* general Request */
    XDPRINTF((2,0, "SAP GENERAL request server_type =%d", server_type));
    send_server_response(2, server_type, from_addr);
  } else if (query_type == 2 || query_type == 4) {
   /*  periodic general or shutdown response (2)
    *  or nearests Service Response (4)
    */
    int entries  = (data_len-2) / sizeof(SAPS);
    uint8    *p  = ((uint8*)ipxdata)+2;
    XDPRINTF((2,0,"SAP PERIODIC (entries=%d) from %s", entries, visable_ipx_adr(from_addr)));
    while (entries--) {
      int    type    = GET_BE16(p);
      uint8 *name    = p+2;
      ipxAddr_t *ad  = (ipxAddr_t*) (p+50);
      int   hops     = GET_BE16(p+ sizeof(SAPS) -2);
     /*  if (hops < 16)   U16_TO_BE16(hops+1, p+ sizeof(SAPS) -2); */
     /* if (hops < 16)  hops++; */
      XDPRINTF((2,0, "TYP=%2d,hops=%2d, Addr=%s, Name=%s", type, hops,
          visable_ipx_adr(ad), name));
      
      if (handle_all_sap_typs || type == 4) {  /* from Fileserver */
        if (16 == hops) {
          /* shutdown */
          XDPRINTF((2,0, "SERVER %s IS GOING DOWN", name));
          insert_delete_server(name, type, NULL, NULL,      16, 1, 0);
        } else {
          insert_delete_server(name, type, ad, from_addr, hops, 0, 0);
          if (type == 4) flag=1;
        }
      }

      p+=sizeof(SAPS);
    } /* while */
  } else {
    XDPRINTF((1,0, "UNKNOWN SAP query %x, server %x", query_type, server_type));
  }

#if INTERNAL_RIP_SAP
  if (flag) {
    uint32 from_net=GET_BE32(from_addr->net);
    if (activate_slow_net(from_net))
      send_sap_rip_broadcast(4);
  }
#endif
}

/*
**      Task state constants (IPX Connection States)
**
**
**      IPX_TASK_FREE           - Task node is currently on the free list
**      IPX_TASK_INUSE          - Task has been allocated for use
**      IPX_TASK_TRANSMIT       - Packet has been transmitted (SendPacket)
**      IPX_TASK_TIMEDOUT       - Task has been disabled due to watchdog or
**                                retry fail
**      IPX_TASK_DYING          - Last NCP Response had Bad Connection Status
**                                Bit (Server Gone or Connection Gone).
**      IPX_TASK_CONNECTED      - Task is connected by address to the server
**      IPX_TASK_BURST          - A Packet Burst transaction is in progress
**
*/
#if 0
#define IPX_TASK_FREE           0       /* Not in use                   */
#define IPX_TASK_INUSE          (1<<0)  /* In use                       */
#define IPX_TASK_TRANSMIT       (1<<1)  /* Packet on the wire           */
#define IPX_TASK_TIMEDOUT       (1<<2)  /* Connection is timed out      */
#define IPX_TASK_DYING          (1<<3)  /* Connection no longer valid   */
#define IPX_TASK_CONNECTED      (1<<4)  /* Connection in place          */
#define IPX_TASK_BURST          (1<<5)  /* Packet Burst in progress     */

#define CONN_STATUS_BAD_CONNECTION      0x01
#define CONN_STATUS_NO_SLOTS_AVAIL      0x04
#define CONN_STATUS_SERVER_DOWN         0x10
#define CONN_STATUS_MESSSAGE_WAITING    0x01
#endif


#ifdef DIAG_SLOT
static void response_ipx_diag(int fd, int ipx_pack_typ,
                         ipxAddr_t *to_addr)
{
  IPX_DATA      ipxdata;
  DIAGRESP      *dia = &ipxdata.diaresp;
  uint8         *p   = (uint8*) (dia+1);
  uint8		*net_count;
  FILE		*f;
  char		buff[200];
  int		i;
  unsigned int	b;
  uint32	rnet;
  uint8		dname[25];
  char		node[20];	
  int		flags;
  int		fframe;
  int           datalen = sizeof(DIAGRESP);
  
  dia->majorversion = 1;
  dia->minorversion = 1;
  U16_TO_BE16(spx_diag_socket, dia->spx_diag_sock);
  dia->anz          = 3;
  *p++              = 0; /* IPX/SPX */
  datalen++;
  *p++              = 1; /* Bridge Driver */
  datalen++;
  /* now extended */
  *p++              = 6; /* Fileserver/Bridge (internal) */
  datalen++;
  net_count  = p++;
  *net_count = 0;
  datalen++;
  /* --- Code by  Valeri Bourak ----- */
  if (internal_net) {
    (*net_count)++;
    *p++ = 1;          /* virtual board */
    datalen++;
    U32_TO_BE32(internal_net, p);
    p       += IPX_NET_SIZE;
    datalen += IPX_NET_SIZE;
    memcpy(p, my_server_adr.node, IPX_NODE_SIZE);
    p       += IPX_NODE_SIZE;
    datalen += IPX_NODE_SIZE;
  }
  if (NULL != (f=fopen("/proc/net/ipx_interface", "r"))) {
    while (fgets((char*)buff, sizeof(buff), f) != NULL){
      fframe = read_interface_data((uint8*) buff, &rnet, node, &flags, dname);
      if (fframe < 0) continue;
      if (rnet > 0L && !(flags & 2)) { /* not internal */
        (*net_count)++;
        *p++ = 0;          /* LAN board */
        datalen++;
        U32_TO_BE32(rnet, p);
        p += IPX_NET_SIZE;
        datalen += IPX_NET_SIZE;
        for (i = 0; i < 12 ; i += 2) {
	  sscanf(&node[i], "%2x", &b);
          *p++ = (uint8)b;
        }
        datalen += IPX_NODE_SIZE;
      }
    }
    fclose(f);
  }
  send_ipx_data(fd, ipx_pack_typ,
                    datalen,
                    (char*)&ipxdata,
                    to_addr, "DIAG Response");
}

static void handle_diag(int fd, int ipx_pack_typ,
                int data_len, IPX_DATA *ipxdata,
                ipxAddr_t *from_addr)
/* should handle CONFIGURATION REQUESTS one time */
{
  CONFREQ *conf       = &(ipxdata->confreq);
  int count           = (int) conf->count;
  int j               = count;
  uint8  *exnodes     = conf->ex_node;
  while (j--) {
    if IPXCMPNODE(exnodes, my_server_adr.node) {
       XDPRINTF((2, 0, "NO RESPONSE TO DIAG"));
       return;
    }
    exnodes += IPX_NODE_SIZE;
  }
  XDPRINTF((2,0,"DIAG Request, ipx_pack_typ %d, data_len %d, count %d",
         (int)ipx_pack_typ, data_len, count));
  response_ipx_diag(fd, ipx_pack_typ, from_addr);
}
#endif

#ifdef EXTERN_SLOT
ipxAddr_t auth_addr;
int       is_auth=0;

static void handle_extern_call(int fd,
                int        ipx_pack_typ,
                int        data_len,
                IPX_DATA   *ipxdata,
                ipxAddr_t  *from_addr)
{
  if (memcmp(&auth_addr, from_addr, sizeof(ipxAddr_t))){
    memcpy(&auth_addr, from_addr, sizeof(ipxAddr_t));
    is_auth=0;
  }
}
#endif

static void handle_event(int fd, uint16 socknr, int slot)
{
  struct        t_unitdata ud;
  ipxAddr_t     source_adr;
  uint8         ipx_pack_typ;
  IPX_DATA      ipx_data_buff;
  int           flags = 0;

  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ; /* gets actual typ */

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);

  ud.addr.buf      = (char*)&source_adr;
  ud.udata.len     = IPX_MAX_DATA;
  ud.udata.maxlen  = IPX_MAX_DATA;
  ud.udata.buf     = (char*)&ipx_data_buff;

  if (t_rcvudata(fd, &ud, &flags) < 0){
    struct t_uderr uderr;
    ipxAddr_t  erradr;
    uint8      err_pack_typ;
    uderr.addr.len      = sizeof(ipxAddr_t);
    uderr.addr.maxlen   = sizeof(ipxAddr_t);
    uderr.addr.buf      = (char*)&erradr;
    uderr.opt.len       = sizeof(err_pack_typ);
    uderr.opt.maxlen    = sizeof(err_pack_typ);
    uderr.opt.buf       = (char*)&err_pack_typ; /* get actual typ */
    ud.addr.buf         = (char*)&source_adr;
    t_rcvuderr(fd, &uderr);
    XDPRINTF((2, 0, "Error from %s, Code = 0x%lx", visable_ipx_adr(&erradr), uderr.error));
    if (nw_debug) t_error("t_rcvudata !OK");
    return;
  }

  XDPRINTF((3,0,"Ptyp:%d from: %s", (int)ipx_pack_typ, visable_ipx_adr(&source_adr) ));

  if (server_down_stamp) return; /* no more interests */

#if INTERNAL_RIP_SAP
  if ( IPXCMPNODE(source_adr.node, my_server_adr.node) &&
       IPXCMPNET (source_adr.net,  my_server_adr.net)) {

    int source_sock = (int) GET_BE16(source_adr.sock);
    if  (
#if !IN_NWROUTED
          source_sock  == sock_nummern[WDOG_SLOT] ||
#endif
          source_sock  == SOCK_SAP
       || source_sock  == SOCK_RIP) {
      XDPRINTF((2,0,"OWN Packet from sock:0x%04x, ignored", source_sock));
      return;
    }

    /* it also can be Packets from DOSEMU OR ncpfs on this machine */
    XDPRINTF((2,0,"Packet from OWN maschine:sock=0x%x", source_sock));
  }
  if (auto_detect_interfaces && test_ins_device_net(GET_BE32(source_adr.net)))
     broadmillisecs = 3000;  /* now faster rip/sap to new devices */
#endif

  switch (slot) {
#ifdef WDOG_SLOT
    case WDOG_SLOT :

                     if (2 == ud.udata.len) {
                       XDPRINTF((2,0, "WDOG Packet len=%d connid=%d, status=%d",
                          (int)ud.udata.len, (int) ipx_data_buff.wdog.connid,
                        (int)ipx_data_buff.wdog.status));
                        if ('Y' == ipx_data_buff.wdog.status) {
                          if (max_connections < 256)
                            modify_wdog_conn(ipx_data_buff.wdog.connid, 0);
                          else {
                            int k=-1;
                            while (++k < hi_conn) {
                              CONNECTION *c=&(connections[k]);
                              if (IPXCMPNODE(c->addr.node, source_adr.node)){
                                modify_wdog_conn(k+1, 0);
                                break;
                              }
                            }
                          }
                        }
                     } else if ( 2 < ud.udata.len
                             && ipx_data_buff.data[0] == 0x11
                             && ipx_data_buff.data[1] == 0x11 ) {
                       /* now we make an echo of this data */
                       send_ipx_data(sockfd[WDOG_SLOT],
                        17, ud.udata.len, ud.udata.buf, &source_adr, "ECHO");
                     } else {
                       uint8 *p = (uint8*)&ipx_data_buff;
                       int    k = 0;
                       XDPRINTF((1, 2, "UNKNOWN from WDOG sock"));
                       while (k++ < ud.udata.len){
                          XDPRINTF((1, 3, " %x", (int) *p++));
                       }
                       XDPRINTF((1, 1, NULL));
                     }
                     break;
#endif

#ifdef EXTERN_SLOT
    case EXTERN_SLOT   : handle_extern_call(fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
#endif

    case SAP_SLOT      : handle_sap( fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
#ifdef RIP_SLOT
    case RIP_SLOT      : handle_rip( fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
#endif

#ifdef DIAG_SLOT
    case DIAG_SLOT     : handle_diag(fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
#endif

    default :
#if DO_DEBUG
              {
                uint8 *p = (uint8*)&ipx_data_buff;
                int    k = 0;
                XDPRINTF((1, 2, "UNKNOWN"));
                while (k++ < ud.udata.len){
                  XDPRINTF((1, 3, " %x", (int) *p++));
                }
                XDPRINTF((1, 1, NULL));
                /*
                print_ud_data(&ud);
                */
              }
#endif
              break;
  }
}

static void get_ini(int full)
{
  FILE   *f    = open_nw_ini();
  int     k;
  uint32  node = 1;  /* default 1 */
  if (full) {
    gethostname(my_nwname, 48);
    upstr((uint8*)my_nwname);
  }
  if (f){
    uint8 buff[500];
    int  what;
    while (0 != (what=get_ini_entry(f, 0, buff, sizeof(buff)))) {
      char inhalt[500];
      char inhalt2[500];
      char inhalt3[500];
      char inhalt4[500];
      char dummy;
      int  anz;
      if ((anz=sscanf((char*)buff, "%500s %500s %500s %500s", inhalt, inhalt2,
                                                inhalt3, inhalt4)) >  0) {
         switch (what) {
           case 2 : if (full) {
                       strncpy(my_nwname, inhalt, 48);
                       my_nwname[47] = '\0';
                       upstr((uint8*)my_nwname);
                     }
                     break;

#if INTERNAL_RIP_SAP
           case 3 :  if (full) {
                       upstr(inhalt);
                       if (!strcmp(inhalt, "AUTO")) internal_net = 0;
                       else {
                         if (sscanf(inhalt, "%ld%c", &internal_net, &dummy) != 1)
                             sscanf(inhalt, "%lx",   &internal_net);
                       }
                       if (anz > 1) {
                         if (sscanf(inhalt2, "%ld%c", &node, &dummy) != 1)
                            sscanf(inhalt2, "%lx",   &node);
                       }
                       if (0 == internal_net) {   /* now we try ip number */
                         char locname[50];
                         struct hostent *hent;
                         gethostname(locname, 48);
                         hent=gethostbyname(locname);
                         if (NULL != hent && hent->h_length == 4) {
                           internal_net = GET_BE32(*(hent->h_addr_list));
                         } else {
                           errorp(10, "Get_ini", "Cannot gethostbyname from '%s'",
                             locname);
                           if (hent) {
                             XDPRINTF((0, 0, "hent->h_length=%d", hent->h_length));
                           }
                         }
                         if (0==internal_net)
                           errorp(11, "Get_ini", "Cannot get AUTO internal net with help of gethostbyname.\n%s",
                                "Please read section 3 of nwserv.conf carefully.");
                       }
                     }
                     break;

           case 4 :  if (full && ( (!count_net_devices) || anz > 2) ) {
                       NW_NET_DEVICE **pnd;
                       NW_NET_DEVICE *nd;

                       if (count_net_devices >= max_net_devices)
                          realloc_net_devices();

                       pnd=&(net_devices[count_net_devices++]);
                       nd=*pnd=(NW_NET_DEVICE*)
                                xcmalloc(sizeof(NW_NET_DEVICE));
                       nd->ticks  = 1;
                       nd->frame  = IPX_FRAME_8023;
                       new_str(nd->devname, "eth0");

                       if (sscanf(inhalt, "%ld%c", &nd->net, &dummy) != 1)
                           sscanf(inhalt, "%lx", &nd->net);

                       if (nd->net && (nd->net == internal_net)) {
                         errorp(11, "Get_ini", "device net 0x%lx = internal net", nd->net);
                         exit(1);
                       }

                       if (anz > 1)
                         new_str(nd->devname, inhalt2);

                       if (anz > 2) {
                         upstr(inhalt3);
                         if (!strcmp(inhalt3, "AUTO"))
                            nd->frame=-1;
                         if (!strcmp(inhalt3, "802.3"))
                            nd->frame=IPX_FRAME_8023;
                         else if (!strcmp(inhalt3, "802.2"))
                            nd->frame=IPX_FRAME_8022;
                         else if (!strcmp(inhalt3, "SNAP"))
                            nd->frame=IPX_FRAME_SNAP;
                         else if (!strcmp(inhalt3, "ETHERNET_II"))
                            nd->frame=IPX_FRAME_ETHERII;
# ifdef IPX_FRAME_TR_8022
                         else if (!strcmp(inhalt3, "TOKEN"))
                            nd->frame=IPX_FRAME_TR_8022;
# endif
                       }
                       if (anz > 3) nd->ticks = atoi(inhalt4);
                     }
                     break;

           case   5 : ipx_flags=hextoi(inhalt);
                      break;
#endif

           case  69 : handle_all_sap_typs=atoi(inhalt);
                      break;

#if !IN_NWROUTED
           case  60 : if (full) { /* connections */
                        max_connections=atoi(inhalt);
                        if (max_connections < 1)
                          max_connections=MAX_CONNECTIONS;
                      }
                      break;
#endif
           case 210 : server_goes_down_secs=atoi(inhalt);
                      if (server_goes_down_secs < 1 ||
                        server_goes_down_secs > 600)
                        server_goes_down_secs = 10;
                      break;

           case 211 : server_broadcast_secs=atoi(inhalt);
                      if (server_broadcast_secs < 10 ||
                        server_broadcast_secs > 600)
                        server_broadcast_secs = 60;
                      break;

           case 300 : print_route_tac=atoi(inhalt);
                      break;

           case 301 : new_str(pr_route_info_fn, (uint8*)inhalt);
                      break;

           case 302 : print_route_mode=hextoi(inhalt);
                      break;

           case 310 : wdogs_till_tics=atoi(inhalt);
                      break;

           case 400 : new_str(station_fn, (uint8*)inhalt);
                      break;

           case 401 : nearest_request_flag=atoi(inhalt);
                      break;

           default : break;
         } /* switch */
      } /* if */
    } /* while */
    fclose(f);
  }
  if (print_route_tac && !pr_route_info_fn && !*pr_route_info_fn)
    print_route_tac = 0;
  if (!print_route_tac) xfree(pr_route_info_fn);

#if INTERNAL_RIP_SAP
  server_broadcast_secs /= 2;
#endif

  if (full) {
#ifdef LINUX
# if INTERNAL_RIP_SAP
    no_internal = !internal_net;
    if (no_internal && count_net_devices > 1) {
      errorp(11, "Get_ini", "No internal net, but more than 1 Device specified");
      exit(1);
    }
    init_ipx(internal_net, node, ipxdebug, ipx_flags);
    for (k=0; k < count_net_devices; k++){
      NW_NET_DEVICE *nd=net_devices[k];
      int  result;
      uint8 frname[30];
      char *sp     = "DEVICE=%s, FRAME=%s, NETWORK=0x%lx";
      (void) get_frame_name(frname, nd->frame);
      XDPRINTF((1, 0, sp, nd->devname, frname, nd->net));

      if (nd->devname[0] == '*') nd->wildmask|=1;
      if (nd->frame       <   0) nd->wildmask|=2;
      if (!nd->net)              nd->wildmask|=4;

      if ((result=init_dev(nd->devname, nd->frame, nd->net, nd->wildmask)) < 0) {
        if (result == -99) {
          errorp(11, "init_dev", "AUTO device is only in combination with internal net allowed");
          exit(1);
        } else
          errorp(1, "init_dev", sp, nd->devname, frname, nd->net);
      } else if (!result) {
        nd->is_up = 1;
      } else
        auto_detect_interfaces=1;
    }
# endif
#endif

    if (!get_ipx_addr(&my_server_adr)) {
      internal_net = GET_BE32(my_server_adr.net);
    } else {
      errorp(1, "No ipx-router running !", NULL);
      exit(1);
    }
#if INTERNAL_RIP_SAP
    if (no_internal) {
      errorp(10, "WARNING:No use of internal net", NULL);
    } else if (!count_net_devices) {
      errorp(10, "WARNING:No external devices specified", NULL);
    }
    print_routing_info(1);
#endif

    XDPRINTF((1, 0, "%s name='%s', INTERNAL NET=0x%lx, NODE=0x%02x:%02x:%02x:%02x:%02x:%02x",
        prog_name_typ, my_nwname, internal_net,
        (int)my_server_adr.node[0],
        (int)my_server_adr.node[1],
        (int)my_server_adr.node[2],
        (int)my_server_adr.node[3],
        (int)my_server_adr.node[4],
        (int)my_server_adr.node[5]));
  } /* if full */
}

static void send_down_broadcast(void)
{
  send_sap_rip_broadcast(2);
}

static void close_all(void)
{
  int  j = NEEDED_SOCKETS;
#if USE_PERMANENT_OUT_SOCKET
  if (ipx_out_fd > -1) {
    t_unbind(ipx_out_fd);
    t_close(ipx_out_fd);
  }
#endif
  
  while (j--) {
#ifdef RIP_SLOT
    if ( j != RIP_SLOT ) {
#endif
      t_unbind(sockfd[j]);
      t_close(sockfd[j]);
#ifdef RIP_SLOT
    }
#endif
  }

  if (pid_ncpserv > 0) {
    int status;
    if (fd_ncpserv_in  > -1)  {
      close(fd_ncpserv_in);
      fd_ncpserv_in = -1;
    }
    kill(pid_ncpserv, SIGQUIT);  /* terminate ncpserv */
    waitpid(pid_ncpserv, &status, 0);
    kill(pid_ncpserv, SIGKILL);  /* kill ncpserv */
  }

  if (pid_nwbind > 0) {
    int status;
    if (fd_nwbind_in  > -1)  {
      close(fd_nwbind_in);
      fd_nwbind_in = -1;
    }
    kill(pid_nwbind, SIGQUIT);  /* terminate nwbind */
    waitpid(pid_nwbind, &status, 0);
    kill(pid_nwbind, SIGKILL);  /* kill nwbind */
  }

#ifdef LINUX
# if INTERNAL_RIP_SAP
#if 1
  if ((!ipx_inuse(0)) || (ipx_flags&4)) {
    send_sap_rip_broadcast(22);
    sleep(2);
    send_sap_rip_broadcast(22);
    sleep(1);
    t_unbind(sockfd[RIP_SLOT]);
    t_close(sockfd[RIP_SLOT]);
    if (!(ipx_flags&1)) {
      for (j=0; j<count_net_devices;j++) {
        NW_NET_DEVICE *nd=net_devices[j];
        if (nd->is_up==1) { /* only no auto interfaces */
          XDPRINTF((1, 0, "Close Device=%s, frame=%d",
                  nd->devname, nd->frame));
          exit_dev(nd->devname, nd->frame);
        }
      }
    }
  } else {
    XDPRINTF((1, 0, "Not sending rip hangup, because ipxinuse"));
  }
#endif
  exit_ipx(ipx_flags);
# endif
#endif
}

static void down_server(void)
{
  if (!server_down_stamp) {
    signal(SIGHUP,   SIG_IGN);
    signal(SIGPIPE,  SIG_IGN);
    write_to_ncpserv(0xffff, 0, NULL, 0);
    write_to_nwbind( 0xffff, 0, NULL, 0);
    fprintf(stderr, "\007");
    fprintf(stderr, "\n*********************************************\n");
    fprintf(stderr, "\nWARNING: NWE-%s shuts down in %3d sec !!!\n",
                     prog_name_typ, server_goes_down_secs);
    fprintf(stderr, "\n*********************************************\n");
    sleep(1);
    fprintf(stderr, "\007\n");
    broadmillisecs  =  100;
    server_down_stamp = acttime_stamp;
    send_down_broadcast();
  }
}

#if !IN_NWROUTED
static int fl_got_sigchld=0;
static void sig_chld(int rsig)
{
  fl_got_sigchld++;
}
#endif

static int  fl_get_int=0; /* 1 .. 8 */
static void sig_quit(int rsig)
{
  signal(rsig,   SIG_IGN);
  signal(SIGHUP, SIG_IGN);  /* don't want it anymore */
  XDPRINTF((2, 0, "Got Signal=%d", rsig));
  fl_get_int|=2;
}

static void handle_hup_reqest(void)
{
  get_ini_debug(IN_PROG);
  XDPRINTF((2,0, "Got HUP, reading ini."));
  get_ini(0);
  write_to_ncpserv(0xeeee, 0, NULL, 0); /* inform ncpserv */
  write_to_nwbind( 0xeeee, 0, NULL, 0); /* inform nwbind  */
  send_sap_rip_broadcast(1);  /* firsttime broadcast */
}

static void sig_hup(int rsig)
{
  fl_get_int|=1;
  signal(SIGHUP, sig_hup);
}

static void handle_usr1_request(void)
{
  XDPRINTF((2,0, "Got USR1, update internal devices/routes"));
  send_sap_rip_broadcast(3);
}

static void handle_usr2_request(void)
{
  XDPRINTF((2,0, "Got USR2, force sending rip/sap"));
  send_sap_rip_broadcast(5);
}

static void sig_usr1(int rsig)
{
  fl_get_int|=4;
  signal(rsig, sig_usr1);
}

static void sig_usr2(int rsig)
{
  fl_get_int|=8;
  signal(rsig, sig_usr2);
}

static void set_sigs(int mode)
{
  signal(SIGPIPE,  SIG_IGN);
  if (!mode) {
    signal(SIGTERM,  SIG_IGN);
    signal(SIGQUIT,  SIG_IGN);
    signal(SIGINT,   SIG_IGN);
    signal(SIGHUP,   SIG_IGN);
    signal(SIGUSR1,  SIG_IGN);
    signal(SIGUSR2,  SIG_IGN);
  } else {
    signal(SIGTERM,  sig_quit);
    signal(SIGQUIT,  sig_quit);
    signal(SIGINT,   sig_quit);
    signal(SIGHUP,   sig_hup);
    signal(SIGUSR1,  sig_usr1);
    signal(SIGUSR2,  sig_usr2);
  }
}

static int server_is_down=0;

static int usage(char *prog)
{
  fprintf(stderr, "============== mars_nwe by Koan ===============\n\n") ; // (M@)
  
  
#if IN_NWROUTED || INTERNAL_RIP_SAP
  fprintf(stderr, "usage:\t%s [-V|-h|-f|-u|-k[q]|y]\n", prog);
  fprintf(stderr, "or:\t%s -a device frame netnum\n", prog);
  fprintf(stderr, "or:\t%s -d device frame\n", prog);
#else
  fprintf(stderr, "usage:\t%s [-V|-h|-u|-k[q]]\n", prog);
#endif
  fprintf(stderr, "\t-V: print version\n");
#if IN_NWROUTED || INTERNAL_RIP_SAP
  fprintf(stderr, "\t-a: add interface, frames = '802.2' '802.3' 'etherii' 'snap'\n");
  fprintf(stderr, "\t-d: delete interface.\n");
  fprintf(stderr, "\t-h: send HUP to main process\n");
  fprintf(stderr, "\t-f: force send rip/sap and update routing int. table\n");
  fprintf(stderr, "\t-u: update int. routing table\n");
  fprintf(stderr, "\t-k: stop main process, wait for it.\n");
  fprintf(stderr, "\t-kq: don't wait till stop of main process\n");
#endif
#if !IN_NWROUTED
  fprintf(stderr, "\t y: start testclient code.\n");
#endif
  return(1);
}

int main(int argc, char **argv)
{
  int j = 0;
  int init_mode=0;
  if (seteuid(0) < 0 || setuid(0) < 0) {
    fprintf(stderr, "You must have root permission !\n");
    exit(1);
  }
#ifdef FREEBSD
  set_emu_tli();
#endif
  tzset();
  while (++j < argc)  {
    char *a=argv[j];
    if (*a == '-') {
      while (*(++a)) {
        switch (*a)  {
#ifdef LINUX        
          case 'a' : 
          case 'd' : 
            if (    (*a == 'a' && argc - j == 4) 
                 || (*a == 'd' && argc - j == 3) ) {
              int    result;
              int    frame=-1;
              uint32 netnum=0L;
              char buf[256];
              strncpy(buf, argv[j+2], sizeof(buf)-1);
              upstr(buf);
              if (!strcmp(buf, "802.3"))
                frame=IPX_FRAME_8023;
              else if (!strcmp(buf, "802.2"))
                frame=IPX_FRAME_8022;
              else if (!strcmp(buf, "SNAP"))
                frame=IPX_FRAME_SNAP;
              else if (!strcmp(buf, "ETHERNET_II"))
                frame=IPX_FRAME_ETHERII;
              else if (!strcmp(buf, "ETHERII"))
                frame=IPX_FRAME_ETHERII;
# ifdef IPX_FRAME_TR_8022
              else if (!strcmp(buf, "TOKEN"))
                frame=IPX_FRAME_TR_8022;
# endif
              if (*a == 'a' && frame > -1) {
                char dummy;
                if (sscanf(argv[j+3], "%ld%c", &netnum, &dummy) != 1)
                    sscanf(argv[j+3], "%lx", &netnum);
              }
#if IN_NWROUTED || INTERNAL_RIP_SAP
              if (netnum > 0)
                result=add_device_net(argv[j+1], frame, netnum); 
              else if ( *a == 'd') {
                exit_dev(argv[j+1], frame); 
                result=0;
              } else
#endif
                return(usage(argv[0]));
              return((result<0) ? 1 : 0);
            } else
              return(usage(argv[0]));
#endif          
          case 'h' : init_mode = 1; break;
          case 'k' : init_mode = 2; break;
          case 'u' : init_mode = 3; break;
          case 'f' : init_mode = 5; break;
          case 'q' : if (init_mode == 2) init_mode=4; break;
          case 'v' :
          case 'V' : fprintf(stderr, "\n%s:Version %d.%d.pl%d [ KOAN ]\n",
                     argv[0], _VERS_H_, _VERS_L_, _VERS_P_ );
                     return(0);
          default  : return(usage(argv[0]));
        }
      }
    } else
      return(usage(argv[0]));
  }
#if !DO_TESTING
  chdir("/");
#endif
  setgroups(0, NULL);
  init_tools(IN_PROG, init_mode);
  set_sigs(0);
  get_ini(1);
#if !IN_NWROUTED
  if (max_connections < 1)
    max_connections=1;
  connections=(CONNECTION*)xcmalloc(max_connections*sizeof(CONNECTION));
#endif
  j=-1;
  while (++j < NEEDED_POLLS) {
    polls[j].events  = POLLIN|POLLPRI;
    polls[j].revents = 0;
    if (j < NEEDED_SOCKETS) {
      int fd = loc_open_ipx_socket(ipx_sock_nummern[j], j);
      if (fd < 0) {
        while (j--) {
          t_unbind(sockfd[j]);
          t_close(sockfd[j]);
        }
        return(1);
      } else {
        sockfd[j]        = fd;
        polls[j].fd      = fd;
      }
    } else {
      polls[j].fd        = -1;
    }
  }

#if USE_PERMANENT_OUT_SOCKET
  ipx_out_fd=open_ipx_socket(NULL, 0);
#endif

#if !IN_NWROUTED
  XDPRINTF((1, 0, "USE_PERMANENT_OUT_SOCKET %s",
                (ipx_out_fd > -1) ? "enabled" : "disabled"));

  XDPRINTF((1, 0, "IPX_MAX_DATA=%d, RW_BUFFERSIZE =%d",
                 IPX_MAX_DATA, RW_BUFFERSIZE));
  signal(SIGCHLD,  sig_chld);
#endif

  if ( !start_nwbind(my_nwname)
   &&  !start_ncpserv(my_nwname, &my_server_adr) ) {
    /* now do polling */
    time_t broadtime;
    time(&broadtime);
    set_sigs(1);
    polls[NEEDED_SOCKETS].fd = fd_nwbind_in;

    U16_TO_BE16(SOCK_NCP, my_server_adr.sock);
#if !IN_NWROUTED
    {
      ipxAddr_t server_adr_sap;
      polls[NEEDED_SOCKETS+1].fd = fd_ncpserv_in;
      memcpy(&server_adr_sap, &my_server_adr, sizeof(ipxAddr_t));
      U16_TO_BE16(SOCK_SAP, server_adr_sap.sock);
      insert_delete_server((uint8*)my_nwname, 0x4,
                     &my_server_adr, &server_adr_sap, 0, 0, 0);
    }
#endif
    while (!server_is_down) {
      int anz_poll = poll(polls, NEEDED_POLLS, broadmillisecs);
#if !IN_NWROUTED
      int call_wdog=0;
#endif
      time(&acttime_stamp);
#if !IN_NWROUTED
      if (fl_got_sigchld) {
        int stat_loc=-1;
        int pid;
        int status=-1;
        fl_got_sigchld=0;
        if ((pid =waitpid(-1, &stat_loc, 0)) > -1) {
          status=WEXITSTATUS(stat_loc);
          if (WIFSIGNALED(stat_loc)) status=-99;
        }
        if (pid == pid_nwbind || pid == pid_ncpserv) {
          errorp(1, "CHILD died", "Child=%s, %s=%d",
              (pid==pid_nwbind) ? "NWBIND" : "NCPSERV",
               (status==-99) ? "got signal" : "result" ,
               (status==-99) ? WTERMSIG(stat_loc) : status);
          down_server();
        } else
          errorp(1, "unknown CHILD died", NULL);

      }
#endif
      if (fl_get_int) {
        if (fl_get_int & 1)
          handle_hup_reqest();
        if (fl_get_int & 4)
          handle_usr1_request();
        if (fl_get_int & 8)
          handle_usr2_request();
        if (fl_get_int & 2)
          down_server();
        fl_get_int=0;
      }
      if (anz_poll > 0) { /* i have to work */
        struct pollfd *p = &polls[0];
        j = -1;
        while (++j < NEEDED_POLLS) {
          if (p->revents){
            if (j < NEEDED_SOCKETS) {  /* socket */
              XDPRINTF((99, 0,"POLL %d, SOCKET %x", p->revents, sock_nummern[j]));
              if (p->revents & ~POLLIN)
                errorp(0, "STREAM error", "revents=0x%x", p->revents );
              else handle_event(p->fd, sock_nummern[j], j);
            }  else {  /* fd_ncpserv_in */
              XDPRINTF((2, 0, "POLL %d, fh=%d", p->revents, p->fd));
#if !IN_NWROUTED
              if (p->revents & ~POLLIN)
                 errorp(0, "STREAM error", "revents=0x%x", p->revents );
              else {
                if (p->fd > -1) {
                  int       what;
                  int       conn;
                  int       size;
                  uint8     buf[200];
                  if (sizeof(int) == read(p->fd,
                            (char*)&what, sizeof(int))) {
                    XDPRINTF((2, 0, "GOT %s_in what=0x%x",
                       (p->fd == fd_ncpserv_in) ? "ncpserv" : "nwbind" ,  what));
                    switch (what) {
                      case  0x2222 :  /* insert wdog connection */
                            if (sizeof(int) == read(p->fd,
                                 (char*)&conn, sizeof(int))
                            &&   sizeof(int) == read(p->fd,
                                 (char*)&size, sizeof(int))
                            &&   sizeof(ipxAddr_t) + sizeof(uint16) + sizeof(uint32)
                                  == read(p->fd,
                                 (char*)buf, size)) {
                                insert_wdog_conn(conn, (ipxAddr_t*)buf);
                                write_to_nwbind(what, conn, (char*)buf, size);
                              }
                            break;

                      case  0x4444 :  /* reset wdog connection   =  0 */
                                      /* activate wdogs          =  1 */
                                      /* remove wdog             = 99 */
                            if (sizeof(int) == read(p->fd,
                                 (char*)&conn, sizeof(int))
                            &&   sizeof(int) == read(p->fd,
                                 (char*)&what, sizeof(what))) {
                              if (what == 1) {
                                while (conn++ < hi_conn) {
                                  modify_wdog_conn(conn, what);
                                }
                                call_wdog++;
                              } else if (what == 0) { /* reset wdog */
                                modify_wdog_conn(conn, what);
                              }
                            }
                            break;

                      case  0x5555 :  /* close connection */
                            if (sizeof(int) == read(p->fd,
                                 (char*)&conn, sizeof(int))) {
                              modify_wdog_conn(conn, 99);
                              write_to_nwbind(what, conn, NULL, 0);
                            }
                            break;

                      case  0x6666 :  /* bcast message */
                            if (sizeof(int) == read(p->fd,
                                (char*)&conn, sizeof(int)))
                              send_bcasts(conn);
                            break;

                      case  0xffff :  /* down file server */
                            if (sizeof(int) == read(p->fd,
                                (char*)&conn, sizeof(int)) &&
                                conn == what) {
                              down_server();
                            }
                            break;

                      default : break;
                    }
                  }
                }
              }
#endif
            }
            if (! --anz_poll) break;
          } /* if */
          p++;
        } /* while */
      } else {
        XDPRINTF((99,0,"POLLING ..."));
      }

      if (server_down_stamp) {
        if (acttime_stamp - server_down_stamp > server_goes_down_secs)
          server_is_down++;
      } else {
        int bsecs    = broadmillisecs / 1000;
        int difftime = acttime_stamp - broadtime;
        if (difftime > bsecs) {
          send_sap_rip_broadcast((bsecs < 3) ? 1 : 0);  /* firsttime broadcast */
          if (bsecs < server_broadcast_secs) {
            rip_for_net(MAX_U32);
            get_servers();
            bsecs *= 2;
            if (bsecs > server_broadcast_secs)
                bsecs=server_broadcast_secs;
            broadmillisecs = bsecs*1000+10;
          }
#if !IN_NWROUTED
          send_wdogs();
#endif
          broadtime = acttime_stamp;
        } else {
#if !IN_NWROUTED
          if (call_wdog) send_wdogs(1);
#endif
        }
      }
    } /* while */
    send_down_broadcast();
    sleep(1);
    send_down_broadcast();
  }
  close_all();
  fprintf(stderr, "\nNWE-%s is down now !!\n", prog_name_typ);
  exit_tools();

#if !IN_NWROUTED
  xfree(connections);
#endif
  return(0);
}

