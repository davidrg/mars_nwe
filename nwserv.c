/* nwserv.c 20-Dec-95 */
/* (C)opyright (C) 1993,1995  Martin Stover, Marburg, Germany
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

uint32     internal_net  = 0x0L; /* NETWORKNUMMER INTERN (SERVER) */
ipxAddr_t  my_server_adr;        /* Address of this server    */
char       my_nwname[50];        /* Name of this server       */

/* <========== DEVICES ==========> */
int            anz_net_devices=0;
NW_NET_DEVICE *net_devices[MAX_NET_DEVICES];

uint16  ipx_sock_nummern[]={ 0,                 /*  auto sock */
                             0,                 /*  auto sock */
			     SOCK_SAP,
			     SOCK_RIP,
	                     SOCK_ROUTE,
	                     SOCK_DIAGNOSE
#ifdef ECHO_SLOT
	                     , SOCK_ECHO
#endif
#ifdef ERROR_SLOT
	                     , SOCK_ERROR
#endif
	                     };

#define NEEDED_SOCKETS  (sizeof(ipx_sock_nummern) / sizeof(uint16))
#define NEEDED_POLLS    (NEEDED_SOCKETS+1)

static uint16        sock_nummern [NEEDED_SOCKETS];

int                  sockfd       [NEEDED_SOCKETS];
static struct        pollfd  polls[NEEDED_POLLS];

static uint16        spx_diag_socket;    /* SPX DIAGNOSE SOCKET       */
static ipxAddr_t     nw386_adr;          /* Address of NW-TEST Server */
static int           nw386_found    = 0;
static int           client_mode    = 0;
static int    	     ipxdebug	    =  0;
static int    	     pid_ncpserv    = -1;
static int           fd_ncpserv_out = -1;  /* ctrl-pipe out to  ncpserv  */
static int           fd_ncpserv_in  = -1;  /* ctrl-pipe in  from ncpserv */

static	time_t 	     akttime_stamp             =  0;
static  int 	     broadsecs		       =  2048;
static  time_t 	     server_down_stamp         =  0;
static  int          server_goes_down_secs     = 10;
static  int          save_ipx_routes	       =  0;
static  int 	     bytes_to_write_to_ncpserv =  0;

static void inform_ncpserv(void)
{
  if (bytes_to_write_to_ncpserv && pid_ncpserv > -1) {
#if 0
    XDPRINTF((2, "inform_ncpserv bytes=%d\n", bytes_to_write_to_ncpserv));
    kill(pid_ncpserv, SIGHUP);    /* tell ncpserv to read input */
#endif
    bytes_to_write_to_ncpserv=0;
  }
}

static void write_to_ncpserv(int what, int connection,
                           char *data, int data_size)
{
  XDPRINTF((2, "write_to_ncpserv what=0x%x, conn=%d, data_size=%d\n",
           what, connection, data_size));

  switch (what) {
    case 0x5555  : /* kill connection  */
          bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, (char*) &what,        sizeof(int));
          bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, (char*) &connection,  sizeof(int));
         break;

    case 0x3333  : /* 'bindery' calls  */
          bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, (char*) &what,        sizeof(int));
          bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, (char*) &data_size,   sizeof(int));
          bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, data,   data_size);
         break;

    case 0xeeee  : /* hup, read init */
         bytes_to_write_to_ncpserv +=
            write(fd_ncpserv_out, (char*) &what,        sizeof(int));
         break;

    case 0xffff  : /* 'down server' */
         bytes_to_write_to_ncpserv +=
           write(fd_ncpserv_out, (char*) &what, sizeof(int));
         bytes_to_write_to_ncpserv +=
           write(fd_ncpserv_out, (char*) &what, sizeof(int));
         inform_ncpserv();
         return;

    default     :  break;
  }
  if (bytes_to_write_to_ncpserv > 255) inform_ncpserv();
}

static void ins_del_bind_net_addr(char *name, ipxAddr_t *adr)
{
  uint8 buf[1024];
  uint8 *p  = buf;
  int   len = 0;
  if (NULL != adr) { /* insert */
    *p=0x01;
    p+=2; len+=2;
    U16_TO_BE16(0x4, p);
    p+=2; len+=2;
    *p = strlen(name);
    strmaxcpy(p+1, name, *p);
    len += (*p+1); p+=(*p + 1);
    memcpy(p, adr, sizeof(ipxAddr_t));
    len+=sizeof(ipxAddr_t);
  } else {  /* delete */
    *p=0x02;
    p+=2; len+=2;
    U16_TO_BE16(0x4, p);
    p+=2; len+=2;
    *p = strlen(name);
    strmaxcpy(p+1, name, *p);
    len += (*p+1); p+=(*p + 1);
  }
  write_to_ncpserv(0x3333, 0, buf, len);
}

static int open_ipx_socket(uint16 sock_nr, int nr, int open_mode)
{
  int ipx_fd=t_open("/dev/ipx", open_mode, NULL);
  struct t_bind   bind;
  if (ipx_fd < 0) {
     t_error("t_open !Ok");
     return(-1);
  }
  U16_TO_BE16(sock_nr, my_server_adr.sock);   /* actual read socket */
  bind.addr.len    = sizeof(ipxAddr_t);
  bind.addr.maxlen = sizeof(ipxAddr_t);
  bind.addr.buf    = (char*)&my_server_adr;
  bind.qlen        = 0; /* ever */
  if (t_bind(ipx_fd, &bind, &bind) < 0){
    char sxx[200];
    sprintf(sxx,"NWSERV:t_bind !OK in open_ipx_socket, sock=%d", sock_nr);
    t_error(sxx);
    t_close(ipx_fd);
    return(-1);
  }
  sock_nummern[nr] = GET_BE16(my_server_adr.sock); /* really socket nmbr */
  if (nw_debug) print_ipx_addr(&my_server_adr);
  return(ipx_fd);
}

static int start_ncpserv(char *nwname, ipxAddr_t *addr)
{
  int fds_out[2];
  int fds_in[2];
  int pid;
  if (pipe(fds_out) < 0 || pipe(fds_in) < 0) return(-1);

  switch (pid=fork()) {
    case 0 : {  /* new Process */
	       char *progname="ncpserv";
	       char addrstr[100];
	       char pathname[300];
	       int j = FD_NWSERV;
	       close(fds_out[1]);   /* no need to write      */
	       dup2(fds_out[0], 0); /* becommes stdin        */
	       close(fds_out[0]);   /* no  longer needed     */

	       close(fds_in[0]);            /* no need to read       */
	       dup2(fds_in[1], FD_NWSERV);  /* becommes fd FD_NWSERV */
	       close(fds_in[1]);            /* no  longer needed     */
	       while (j++ < 100) close(j);  /* close all > 4 	     */
	       ipx_addr_to_adr(addrstr, addr);
	       execl(get_exec_path(pathname, progname), progname,
	              nwname,  addrstr, NULL);
	       exit(1);
	     }
	     break;

    case -1: close(fds_out[0]);
    	     close(fds_out[1]);
    	     close(fds_in[0]);
    	     close(fds_in[1]);
    	     return(-1);  /* error */
  }
  fds_out[0]     = -1;
  fd_ncpserv_out = fds_out[1];
  fds_in[1]      = -1;
  fd_ncpserv_in  = fds_in[0];
  pid_ncpserv    = pid;
  return(0); /*  OK */
}

static int start_nwclient(void)
{
  switch (fork()){
    case 0 : {  /* new Process */
	       char *progname="nwclient";
	       char pathname[300];
	       char my_addrstr[100];
	       char serv_addrstr[100];
	       ipx_addr_to_adr(my_addrstr,     &my_server_adr);
	       ipx_addr_to_adr(serv_addrstr,   &nw386_adr);
	       execl(get_exec_path(pathname, progname), progname,
	              my_addrstr, serv_addrstr, NULL);
	     }
	     exit(1);

    case -1: return(-1);  /* error */
  }
  return(0);       /*  OK */
}

/* ===========================  WDOG =============================== */

#ifdef _WDOG_TESTING_
/* for testing */
# define WDOG_TRIE_AFTER_SEC   1
# define MAX_WDOG_TRIES        1
#else
# define WDOG_TRIE_AFTER_SEC   300  /* ca. 5 min.    */
# define MAX_WDOG_TRIES        11   /* Standardtries */
#endif


static void send_wdog_packet(ipxAddr_t *addr, int conn, int what)
{
  uint8     data[2];
  data[0] = (uint8) conn;
  data[1] = (uint8) what;
  send_ipx_data(sockfd[WDOG_SLOT], 17, 2, data, addr, "WDOG");
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
  send_ipx_data(sockfd[WDOG_SLOT], 17, 2, data, addr, "BCAST");
}

typedef struct {
  ipxAddr_t  addr;        /* address of client     */
  time_t     last_time;   /* last wdog packet sent */
  int        counter;     /* max. 11 packets       */
} CONN;

static CONN conns[MAX_CONNECTIONS];
static int hi_conn=0;     /* highest connection nr in use */

static void insert_wdog_conn(int conn, ipxAddr_t *adr)
{
  if (conn > 0 && conn <= MAX_CONNECTIONS) {
    CONN *c;
    while (hi_conn < conn) {
      c=&(conns[hi_conn++]);
      memset(c, 0, sizeof(CONN));
    }
    c=&(conns[--conn]);
    c->last_time = akttime_stamp;
    c->counter   = 0;
    if (NULL != adr) memcpy(&(c->addr), adr, sizeof(ipxAddr_t));
  }
}

static void modify_wdog_conn(int conn, int mode)
/* mode =  0  : reset        */
/* mode =  1  : force test 1 */
/* mode =  2  : force test 2 */
/* mode = 99  : remove wdog  */
{
  if (conn > 0 && --conn < hi_conn) {
    CONN *c=&(conns[conn]);
    if (mode < 99) {
      c->last_time = akttime_stamp;
      switch (mode) {
        case 1  : c->counter =  MAX_WDOG_TRIES;  /* quick test */
                  broadsecs  =  4096;
                  break;

        case 2  : c->counter =  1;               /* slow test (activate)*/
                  broadsecs  =  4096;
                  break;

        default : c->counter = 0;  /* reset */
        	  break;
      } /* switch */
    } else if (mode == 99) {  /* remove */
      memset(c, 0, sizeof(CONN));
      if (conn + 1 == hi_conn) {
        while (hi_conn) {
          c=&(conns[hi_conn-1]);
          if (!c->last_time) hi_conn--;
          else break;
        }
      }
    }
  }
}

static void send_wdogs(void)
{
  int  k  = hi_conn;
  while (k--) {
    CONN  *c = &(conns[k]);
    if (c->last_time) {
      time_t t_diff = akttime_stamp - c->last_time;
      if (c->counter || t_diff > WDOG_TRIE_AFTER_SEC) { /* max. 5 minutes */
        if (c->counter > MAX_WDOG_TRIES) {
          /* now its enough with trying */
          /* clear connection */
          write_to_ncpserv(0x5555, k+1, NULL, 0);
        } else {
          ipxAddr_t adr;
          c->last_time = akttime_stamp;
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
    CONN  *c = &(conns[conn]);
    ipxAddr_t adr;
    memcpy(&adr, &(c->addr), sizeof(ipxAddr_t));
    U16_TO_BE16(GET_BE16(adr.sock)+2, adr.sock);
    send_bcast_packet(&adr, conn+1, '!');  /* notify */
  }
}


static void send_server_respons(int fd, uint8 ipx_pack_typ,
	                 int respond_typ, int server_typ,
	                 ipxAddr_t *to_addr)
{
  IPX_DATA           ipx_data;
  memset(&ipx_data, 0, sizeof(ipx_data.sip));
  strcpy(ipx_data.sip.server_name, my_nwname);
  memcpy(&ipx_data.sip.server_adr, &my_server_adr, sizeof(ipxAddr_t));

  U16_TO_BE16(SOCK_NCP,      ipx_data.sip.server_adr.sock);
      /* NCP SOCKET verwenden */

  U16_TO_BE16(respond_typ,   ipx_data.sip.response_type);
  U16_TO_BE16(server_typ,    ipx_data.sip.server_type);
  U16_TO_BE16(0,             ipx_data.sip.intermediate_networks);
  send_ipx_data(fd, ipx_pack_typ,
	            sizeof(ipx_data.sip),
	            (char *)&(ipx_data.sip),
	            to_addr, "Server Response");
}

void get_server_data(char *name,
                ipxAddr_t *adr,
                ipxAddr_t *from_addr)
{
   if (!nw386_found) {
     memcpy(&nw386_adr, adr, sizeof(ipxAddr_t));
     nw386_found++;
     if (client_mode) {
       start_nwclient();
       client_mode = 0;  /* only start once */
     }
   }
   XDPRINTF((2,"NW386 %s found at:%s\n", name, visable_ipx_adr(adr)));
   ins_del_bind_net_addr(name, adr);
}

static void handle_sap(int fd,
     	        int 	   ipx_pack_typ,
	        int 	   data_len,
	        IPX_DATA   *ipxdata,
	        ipxAddr_t  *from_addr)
{
  int query_type   = GET_BE16(ipxdata->sqp.query_type);
  int server_type  = GET_BE16(ipxdata->sqp.server_type);
  if (query_type == 3) {
    XDPRINTF((2,"SAP NEAREST SERVER request typ=%d von %s\n",
             server_type, visable_ipx_adr(from_addr)));
    if (server_type == 4) {
      /* Get Nearest File Server */
      send_server_respons(fd, ipx_pack_typ, 4, server_type, from_addr);
    }
  } else if (query_type == 1) {  /* general Request */
    XDPRINTF((2,"SAP GENERAL request server_type =%d\n", server_type));
    if (server_type == 4) {
      /* Get General  File Server Request */
      send_server_respons(fd, ipx_pack_typ, 4, server_type, from_addr);
    }
  } else if (query_type == 2 || query_type == 4) {
   /*  periodic general or shutdown response (2)
    *  or nearests Service Response (4)
    */
    int entries  = (data_len-2) / sizeof(SAPS);
    uint8    *p  = ((uint8*)ipxdata)+2;
    XDPRINTF((2,"SAP PERIODIC (entries=%d) from %s\n", entries, visable_ipx_adr(from_addr)));
    while (entries--) {
      int    type    = GET_BE16(p);
      uint8 *name    = p+2;
      ipxAddr_t *ad  = (ipxAddr_t*) (p+50);
      int   hops     = GET_BE16(p+ sizeof(SAPS) -2);
      XDPRINTF((2,"TYP=%2d,hops=%2d, Addr=%s, Name=%s\n", type, hops,
          visable_ipx_adr(ad), name));

      if (type == 4 && strcmp(name, my_nwname)) {  /* from Fileserver */
        if (16 == hops) {
          /* shutdown */
          XDPRINTF((2,"SERVER %s IS GOING DOWN\n", name));
          ins_del_bind_net_addr(name, NULL);
        } else {
          get_server_data(name, ad, from_addr);
        }
      }
      p+=sizeof(SAPS);
    } /* while */
  } else {
    XDPRINTF((1,"UNKNOWN SAP query %x, server %x\n", query_type, server_type));
  }
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


static void response_ipx_diag(int fd, int ipx_pack_typ,
	                 ipxAddr_t *to_addr)
{
  IPX_DATA           ipxdata;
  DIAGRESP           *dia = &ipxdata.diaresp;
  uint8              *p   = (uint8*) (dia+1);
  int datalen   =    sizeof(DIAGRESP);  /* erstmal */
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
  *p++              = 1; /* Anz. Networks */
  datalen++;
  *p++              = 0; /* LAN BOARD */
  datalen++;
  memcpy(p, my_server_adr.net, IPX_NET_SIZE);
  p       += IPX_NET_SIZE;
  datalen += IPX_NET_SIZE;
  memcpy(p, my_server_adr.node, IPX_NODE_SIZE);
  datalen += IPX_NODE_SIZE;
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
       DPRINTF(("NO RESPONSE TO DIAG\n"));
       return;
    }
    exnodes += IPX_NODE_SIZE;
  }
  XDPRINTF((2,"DIAG Request, ipx_pack_typ %d, data_len %d, count %d\n",
	 (int)ipx_pack_typ, data_len, count));
  response_ipx_diag(fd, ipx_pack_typ, from_addr);
}

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
    DPRINTF(("Error from %s, Code = 0x%lx\n", visable_ipx_adr(&erradr), uderr.error));
    if (nw_debug) t_error("t_rcvudata !OK");
    return;
  }

  XDPRINTF((3,"Ptyp:%d from: %s\n", (int)ipx_pack_typ, visable_ipx_adr(&source_adr) ));

  if (server_down_stamp) return; /* no more interests */

  if ( IPXCMPNODE(source_adr.node, my_server_adr.node) &&
       IPXCMPNET (source_adr.net,  my_server_adr.net)) {

    int source_sock = (int) GET_BE16(source_adr.sock);
    if (  source_sock  == sock_nummern[MY_BROADCAST_SLOT]
       || source_sock  == sock_nummern[WDOG_SLOT]
       || source_sock  == SOCK_SAP
       || source_sock  == SOCK_RIP) {
      XDPRINTF((2,"OWN Packet from sock:0x%04x, ignored\n", source_sock));
      return;
    }

    /* it also can be Packets from DOSEMU OR ncpfs on this machine */
    XDPRINTF((2,"Packet from OWN maschine:sock=0x%x\n", source_sock));
  }

  switch (socknr) {
    case SOCK_SAP      : handle_sap( fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
    case SOCK_RIP      : handle_rip( fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;
    case SOCK_DIAGNOSE : handle_diag(fd, (int) ipx_pack_typ, ud.udata.len, &ipx_data_buff, &source_adr); break;

    default :
              if (WDOG_SLOT == slot) {  /* this is a watchdog packet */
                XDPRINTF((2,"WDOG Packet len=%d connid=%d, status=%d\n",
                        (int)ud.udata.len, (int) ipx_data_buff.wdog.connid,
                        (int)ipx_data_buff.wdog.status));
                if (2 == ud.udata.len) {
                  if ('Y' == ipx_data_buff.wdog.status)
                      modify_wdog_conn(ipx_data_buff.wdog.connid, 0);
                }
              } else {
                uint8 *p = (uint8*)&ipx_data_buff;
                int    k = 0;
                DPRINTF(("UNKNOWN"));
                while (k++ < ud.udata.len){
                  DPRINTF((" %x", (int)   *p++));
                }
                DPRINTF(("\n"));
                /*
                print_ud_data(&ud);
                */
	      }
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
    upstr(my_nwname);
  }
  if (f){
    char buff[500];
    int  what;
    while (0 != (what=get_ini_entry(f, 0, (char*)buff, sizeof(buff)))) {
      char inhalt[500];
      char inhalt2[500];
      char inhalt3[500];
      char inhalt4[500];
      char dummy;
      int  anz;
      if ((anz=sscanf((char*)buff, "%s %s %s", inhalt, inhalt2,
                                              inhalt3, inhalt4)) >  0) {
         switch (what) {
           case 2 : if (full) {
                       strncpy(my_nwname, inhalt, 48);
                       my_nwname[47] = '\0';
                       upstr(my_nwname);
                     }
                     break;

           case 3 :
                     if (full) {
                       if (sscanf(inhalt, "%ld%c", &internal_net, &dummy) != 1)
                           sscanf(inhalt, "%lx",   &internal_net);
                       if (anz > 1) {
                         if (sscanf(inhalt2, "%ld%c", &node, &dummy) != 1)
                            sscanf(inhalt2, "%lx",   &node);
                       }
                     }
                     break;

           case 4 :
                     if (full) {
                       if (anz_net_devices < MAX_NET_DEVICES &&
                         (!anz_net_devices || anz > 2) ) {
                         NW_NET_DEVICE **pnd=&(net_devices[anz_net_devices++]);
                         NW_NET_DEVICE *nd=*pnd=
                                (NW_NET_DEVICE*)xmalloc(sizeof(NW_NET_DEVICE));
                         memset(nd, 0, sizeof(NW_NET_DEVICE));
                         nd->ticks  = 1;
                         nd->frame  = IPX_FRAME_8023;
                         new_str(nd->devname, "eth0");

                         if (sscanf(inhalt, "%ld%c", &nd->net, &dummy) != 1)
                             sscanf(inhalt, "%lx", &nd->net);
                         if (anz > 1)
                           new_str(nd->devname, inhalt2);

                         if (anz > 2) {
                           upstr(inhalt3);
                           if (!strcmp(inhalt3, "802.3"))
                              nd->frame=IPX_FRAME_8023;
                           else if (!strcmp(inhalt3, "802.2"))
                              nd->frame=IPX_FRAME_8022;
                           else if (!strcmp(inhalt3, "SNAP"))
                              nd->frame=IPX_FRAME_SNAP;
                           else if (!strcmp(inhalt3, "ETHERNET_II"))
                              nd->frame=IPX_FRAME_ETHERII;
                         }
                         if (anz > 3) nd->ticks = atoi(inhalt4);
                       }
                     }
                     break;
#ifdef LINUX
           case   5 : save_ipx_routes=atoi(inhalt);
           	      break;
#endif
           case 104 : /* nwclient */
                      if (client_mode && atoi(inhalt))
                          client_mode++;
                      break;

           case 210 : server_goes_down_secs=atoi(inhalt);
                      if (server_goes_down_secs < 1 ||
                        server_goes_down_secs > 600)
                        server_goes_down_secs = 10;
                      break;

           default : break;
         } /* switch */
      } /* if */
    } /* while */
    fclose(f);
  }
  if (client_mode < 2) client_mode=0;
  if (full) {
#ifdef LINUX
    init_ipx(internal_net, node, ipxdebug);
    for (k=0; k < anz_net_devices; k++){
      NW_NET_DEVICE *nd=net_devices[k];
      char *frname=NULL;
      switch (nd->frame) {
        case IPX_FRAME_8022    : frname = "802.2";      break;
        case IPX_FRAME_8023    : frname = "802.3";      break;
        case IPX_FRAME_SNAP    : frname = "SNAP";       break;
        case IPX_FRAME_ETHERII : frname = "ETHERNET_II";break;
        default : break;
      } /* switch */
      DPRINTF(("DEVICE=%s, FRAME=%s, NETWORK=0x%lx\n",
               nd->devname, frname, nd->net));
      init_dev(nd->devname, nd->frame, nd->net);
    }
#endif
    if (!get_ipx_addr(&my_server_adr)) {
      internal_net = GET_BE32(my_server_adr.net);
    } else exit(1);
    DPRINTF(("Servername='%s', INTERNAL NET=0x%lx, NODE=0x%02x:%02x:%02x:%02x:%02x:%02x\n",
        my_nwname, internal_net,
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
  send_sap_broadcast(2);
  send_rip_broadcast(2); /* shutdown rip */
}

static void close_all(void)
{
  int  j = NEEDED_SOCKETS;

  while (j--) {
    t_unbind(sockfd[j]);
    t_close(sockfd[j]);
  }

  if (pid_ncpserv > 0) {
    int status;
    if (fd_ncpserv_out > -1) {
      close(fd_ncpserv_out);
      fd_ncpserv_out =-1;
    }
    if (fd_ncpserv_in  > -1)  {
      close(fd_ncpserv_in);
      fd_ncpserv_in = -1;
    }
    kill(pid_ncpserv, SIGTERM);  /* terminate ncpserv */
    waitpid(pid_ncpserv, &status, 0);
    kill(pid_ncpserv, SIGKILL);  /* kill ncpserv */
  }

#ifdef LINUX
  if (!save_ipx_routes) {
    for (j=0; j<anz_net_devices;j++) {
      NW_NET_DEVICE *nd=net_devices[j];
      DPRINTF(("Close Device=%s, frame=%d\n",
                nd->devname, nd->frame));
      exit_dev(nd->devname, nd->frame);
    }
  }
  exit_ipx(!save_ipx_routes);
#endif

}

static void down_server(void)
{
  if (!server_down_stamp) {
    write_to_ncpserv(0xffff, 0, NULL, 0);
    signal(SIGHUP,   SIG_IGN);
    signal(SIGPIPE,  SIG_IGN);
    fprintf(stderr, "\007");
    fprintf(stderr, "\n*********************************************\n");
    fprintf(stderr, "\nWARNING: NWE-SERVER shuts down in %3d sec !!!\n",
                     server_goes_down_secs);
    fprintf(stderr, "\n*********************************************\n");
    sleep(1);
    fprintf(stderr, "\007\n");
    broadsecs=100;
    server_down_stamp = akttime_stamp;
    send_down_broadcast();
  }
}

static int  fl_get_int=0;
static void sig_quit(int rsig)
{
  signal(rsig,   SIG_IGN);
  signal(SIGHUP, SIG_IGN);  /* don't want it anymore */
  fl_get_int=2;
}

static void handle_hup_reqest(void)
{
  get_ini_debug(NWSERV);
  XDPRINTF((2, "NWSERV:Got HUP, reading ini.\n"));
  get_ini(0);
  write_to_ncpserv(0xeeee, 0, NULL, 0); /* inform ncpserv */
  fl_get_int=0;
}

static void sig_hup(int rsig)
{
  fl_get_int=1;
  signal(SIGHUP, sig_hup);
}

static void set_sigs(void)
{
  signal(SIGTERM,  sig_quit);
  signal(SIGQUIT,  sig_quit);
  signal(SIGINT,   sig_quit);
  signal(SIGPIPE,  SIG_IGN);
  signal(SIGHUP,   sig_hup);
}

int main(int argc, char **argv)
{
  int j = -1;
  tzset();
  if (argc > 1) client_mode=1;
     /* in client mode the testprog 'nwclient' will be startet. */

  init_tools(NWSERV);
  get_ini(1);

  while (++j < NEEDED_POLLS) {
    polls[j].events  = POLLIN|POLLPRI;
    polls[j].revents = 0;
    if (j < NEEDED_SOCKETS) {
      int fd = open_ipx_socket(ipx_sock_nummern[j], j, O_RDWR);
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

  U16_TO_BE16(SOCK_NCP, my_server_adr.sock);
  if (!start_ncpserv(my_nwname, &my_server_adr)) {
    /* Jetzt POLLEN */
    time_t broadtime;
    time(&broadtime);
    set_sigs();
    polls[NEEDED_SOCKETS].fd = fd_ncpserv_in;
    while (1) {
      int anz_poll = poll(polls, NEEDED_POLLS, broadsecs);
      time(&akttime_stamp);
      if (fl_get_int) {
        if (fl_get_int == 1)      handle_hup_reqest();
        else if (fl_get_int == 2) down_server();
      }
      if (anz_poll > 0) { /* i have to work */
	struct pollfd *p = &polls[0];
	j = -1;
	while (++j < NEEDED_POLLS) {
	  if (p->revents){
            if (j < NEEDED_SOCKETS) {  /* socket */
	      XDPRINTF((99,"POLL %d, SOCKET %x, ", p->revents, sock_nummern[j]));
	      if (p->revents & ~POLLIN)
	        errorp(0, "STREAM error", "revents=0x%x", p->revents );
	      else handle_event(p->fd, sock_nummern[j], j);
            }  else {  /* fd_ncpserv_in */
	      XDPRINTF((2,"POLL %d, fh=%d\n", p->revents, p->fd));
	      if (p->revents & ~POLLIN)
	         errorp(0, "STREAM error", "revents=0x%x", p->revents );
              else {
                if (p->fd == fd_ncpserv_in) {
                  int       what;
                  int       conn;
                  int       size;
                  ipxAddr_t adr;
                  if (sizeof(int) == read(fd_ncpserv_in,
                            (char*)&what, sizeof(int))) {
	            XDPRINTF((2,"GOT ncpserv_in what=0x%x\n", what));
                    switch (what) {
                      case  0x2222 :  /* insert wdog connection */
                            if (sizeof(int) == read(fd_ncpserv_in,
                                 (char*)&conn, sizeof(int))
                            &&   sizeof(int) == read(fd_ncpserv_in,
                                 (char*)&size, sizeof(int))
                            &&   sizeof(ipxAddr_t) == read(fd_ncpserv_in,
                                 (char*)&adr, size))
                                 insert_wdog_conn(conn, &adr);
                            break;

                      case  0x4444 :  /* reset wdog connection   =  0 */
                                      /* force test wdog conn 1  =  1 */
                                      /* force test wdog conn 2  =  2 */
                                      /* remove wdog	         = 99 */
                            if (sizeof(int) == read(fd_ncpserv_in,
                                 (char*)&conn, sizeof(int))
                            &&   sizeof(int) == read(fd_ncpserv_in,
                                 (char*)&what, sizeof(what)))
                                modify_wdog_conn(conn, what);
                            break;

                      case  0x6666 :  /* bcast message */
                            if (sizeof(int) == read(fd_ncpserv_in,
                                (char*)&conn, sizeof(int)))
                              send_bcasts(conn);
                            break;

                      case  0xffff :  /* down file server */
                            if (sizeof(int) == read(fd_ncpserv_in,
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
            }
	    if (! --anz_poll) break;
	  } /* if */
	  p++;
	} /* while */
      } else {
        XDPRINTF((99,"POLLING ...\n"));
      }
      if (server_down_stamp) {
        if (akttime_stamp - server_down_stamp > server_goes_down_secs) break;
      } else {
        if (akttime_stamp - broadtime > (broadsecs / 1000)) { /* ca. 60 seconds */
          send_sap_broadcast(broadsecs<3000);  /* firsttime broadcast */
          send_rip_broadcast(broadsecs<3000);  /* firsttime broadcast */
          if (broadsecs < 32000) {
            rip_for_net(MAX_U32);
            get_servers();
            broadsecs *= 2;
          }
          inform_ncpserv();
          send_wdogs();
          broadtime = akttime_stamp;
        } else if (client_mode) get_servers();  /* Here more often */
      }
    } /* while */
    send_down_broadcast();
    sleep(1);
    send_down_broadcast();
  }
  close_all();
  fprintf(stderr, "\nNWE-SERVER is down now !!\n");
  return(0);
}

