/* ncpserv.c 29-Jul-97 */
/* (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany
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
#if !CALL_NWCONN_OVER_SOCKET
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

static  int        ncp_fd = -1;
static  uint8      ipx_in_data[IPX_MAX_DATA];
static  NCPREQUEST *ncprequest = (NCPREQUEST*)&ipx_in_data;
static  struct     t_unitdata ud;
static  int        in_len=0;
static  uint8      ipx_pack_typ=17;
static  ipxAddr_t  from_addr;   /* aktuelle Anforderungsaddresse */
static  ipxAddr_t  my_addr;
static  int        rcv_flags = 0;
static  char       my_nwname[50];
static  time_t 	   akttime;
static  int        server_goes_down=0;
static  int 	   ipx_out_fd=-1;
static  int        sock_nwbind=-1;
static  int        sock_echo  =-1;
static  int        highest_fd = 10;

static  int        station_restrictions=0;
static int         max_connections=MAX_CONNECTIONS;

#if !CALL_NWCONN_OVER_SOCKET
static char        *nwconn_state; /* shared memory segment will be 
                                   * attached to this pointer */
static int         nwconn_state_shm_id;
#endif

static void set_highest_fd(int fd)
{
  if (fd > highest_fd)
    highest_fd=fd;
}

static int get_ini(int full)
{
  FILE *f  = open_nw_ini();
  if (f){
    uint8 buff[256];
    int   what;
    while (0 != (what =get_ini_entry(f, 0, buff, sizeof(buff)))) {
      if (60 == what && full) {   /* max_connections */
        max_connections=atoi((char*)buff);
        if (max_connections < 1)
          max_connections=MAX_CONNECTIONS;
      } else if (400 == what) {   /* station file */
        new_str(station_fn, buff);
      } else if (402 == what) {  /* station connect restrictions */
        station_restrictions=atoi((char*)buff);
      }
    } /* while */
    fclose(f);
  }
  return(0);
}

/* next should be '1', is for testing only */
#define USE_PERMANENT_OUT_SOCKET  1

static void write_to_nwserv(int what, int connection, int mode,
       	    			char *data, int size)
{
  switch (what) {
    case 0x2222  : /* insert wdog connection */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   write(FD_NWSERV, &size,       sizeof(int));
                   write(FD_NWSERV, data,        size);  /* ipxAddr_t + socknr */
                   break;

    case 0x4444  : /* tell the wdog there's no need to look  0 */
                   /* activate wdogs to free connection      1 */
                   /* the connection ist closed        	    99 */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   write(FD_NWSERV, &mode,       sizeof(int));
                   break;

    case 0x5555  : /* close connection */
    case 0x6666  : /* send to client that server holds message */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   break;

    case 0xffff  : /* tell nwserv to down the server */
                   write(FD_NWSERV, &what, sizeof(int));
                   write(FD_NWSERV, &what, sizeof(int));
                   break;

    default     :  break;
  }
}

#define nwserv_insert_conn(connection, adr, size) \
   write_to_nwserv(0x2222, (connection), 0, (adr), (size))

#define nwserv_activate_wdogs() \
   write_to_nwserv(0x4444, 0, 1, NULL, 0)

#define nwserv_reset_wdog(connection) \
   write_to_nwserv(0x4444, (connection), 0,  NULL, 0)

#define nwserv_close_conn(connection) \
   write_to_nwserv(0x5555, (connection), 0, NULL, 0)

#define nwserv_handle_msg(connection) \
   write_to_nwserv(0x6666, (connection), 0, NULL, 0)

#define nwserv_down_server() \
   write_to_nwserv(0xffff, 0, 0, NULL, 0)

static int open_ipx_sockets(void)
{
  ncp_fd  = open_ipx_socket(&my_addr, SOCK_NCP);
  if (ncp_fd < 0)
    return(-1);
#if USE_PERMANENT_OUT_SOCKET
  ipx_out_fd = open_ipx_socket(NULL, 0);
#endif
  return(0);
}

typedef struct {
   int        fd;           /* writepipe                      */
                            /* or if CALL_NWCONN_OVER_SOCKET  */
                            /* then sock_nr of nwconn 	      */
   int        pid;          /* pid from son          */
   ipxAddr_t  client_adr;   /* address client        */
   int        sequence;     /* previous sequence     */
   time_t     last_access;  /* time of last 0x2222 request */
} CONNECTION;

static CONNECTION *connections=NULL;
static int 	  count_connections=0;   /* actual count connections */

#define TEST_HIGH_CONN  0

static int new_conn_nr(void)
{
  int  j = -1;
  if (!count_connections){ /* init all */
    j = max_connections;
    while (j--) {
      connections[j].fd       	= -1;
      connections[j].pid       	= -1;
    }
    count_connections++;

#if TEST_HIGH_CONN    
    return(count_connections=301);   /* TESTS ONLY */
#endif
    return(1);
  }
  j = -1;

#if TEST_HIGH_CONN    
  j=300-1;   /* TESTS ONLY */
#endif  
  
  while (++j < max_connections) {
    CONNECTION *c=&(connections[j]);
    if (c->fd < 0 && c->pid < 0) {
      if (++j > count_connections) count_connections=j;
      return(j);
    }
  }
  /* nothing free */
  nwserv_activate_wdogs();
  return(0); /* nothing free */
}

static int free_conn_nr(int nr)
{
  if (nr && --nr < count_connections) {
    connections[nr].fd  = -1;
    connections[nr].pid = -1;
    return(0);
  }
  return(-1);
}

static int find_conn_nr(ipxAddr_t *addr)
{
  int j = -1;
  while (++j < count_connections) {
    if (connections[j].fd > -1 &&
      !memcmp((char*)&(connections[j].client_adr),
	      (char*)addr, sizeof(ipxAddr_t))) return(++j);
  }
  return(0);
}

static void clear_connection(int conn)
{
  nwserv_close_conn(conn);
  if (conn > 0 && --conn < count_connections) {
    CONNECTION *c = &connections[conn];
    if (c->fd > -1) {
#if !CALL_NWCONN_OVER_SOCKET
      close(c->fd);
#endif
      c->fd = -1;
      if (c->pid > -1) kill(c->pid, SIGTERM); /* kill it */
    }
  }
}

static void kill_connections(void)
/* here the connections will really removed */
{
  int conn;
  int stat_loc;
  int pid;
  while ((pid=waitpid(-1, &stat_loc, WNOHANG)) > 0) {
    conn =  count_connections;
    while (conn--) {
      if (connections[conn].pid == pid) clear_connection(conn+1);
    }
  }
  conn = count_connections;
  while (conn--) {
    CONNECTION *c = &connections[conn];
    if (c->fd < 0 && c->pid > -1) {
      kill(c->pid, SIGKILL); /* kill it */
      c->pid = -1;
    }
  }
  conn  = count_connections;
  while (conn--) {
    CONNECTION *c = &connections[conn];
    if (c->fd < 0 && c->pid < 0) count_connections--;
    else break;
  }
}

static int find_get_conn_nr(ipxAddr_t *addr)
{
  int connection=find_conn_nr(addr);

  if (!connection){
    if ((connection = new_conn_nr()) > 0){
      CONNECTION *c=&(connections[connection-1]);

#if !CALL_NWCONN_OVER_SOCKET
      int fds[2];
      int res=pipe(fds);
      memcpy((char*) &(c->client_adr), (char *)addr, sizeof(ipxAddr_t));
      if (res > -1) {
        set_highest_fd(fds[0]);
        set_highest_fd(fds[1]);
      }
      if (res < 0) {
	errorp(0, "find_get_conn_nr, pipe", NULL);
	free_conn_nr(connection);
	return(0);
      } else {
#else
      int ipx_fd=-1;
      struct t_bind bind;
      memcpy((char*) &(c->client_adr), (char *)addr, sizeof(ipxAddr_t));
      ipx_fd=t_open("/dev/ipx", O_RDWR, NULL);
      if (ipx_fd > -1) {
        U16_TO_BE16(SOCK_AUTO,  my_addr.sock); /* actual write socket */
        bind.addr.len    = sizeof(ipxAddr_t);
        bind.addr.maxlen = sizeof(ipxAddr_t);
        bind.addr.buf    = (char*)&my_addr;
        bind.qlen        = 0; /* allways */
        if (t_bind(ipx_fd, &bind, &bind) < 0){
          if (nw_debug) t_error("t_bind !OK");
          t_close(ipx_fd);
          ipx_fd = -1;
        } else { 
          set_highest_fd(ipx_fd);
        }
      } else {
        if (nw_debug) t_error("t_open !Ok");
      }

      if (ipx_fd < 0) {
	errorp(0, "find_get_conn_nr, socket", NULL);
	free_conn_nr(connection);
	return(0);
      } else {
#endif
	int akt_pid = getpid();
	int pid     = fork();
	if (pid < 0) {
	  errorp(0, "find_get_conn_nr, fork", NULL);
	  free_conn_nr(connection);

#if !CALL_NWCONN_OVER_SOCKET
	  close(fds[0]);
	  close(fds[1]);
#endif
	  return(0);
	}

	if (pid == 0) {
         /* new process */
	  char *progname="nwconn";
	  char pathname[300];
	  char connstr[20];
	  char addrstr[100];
          char divstr[50];
	  int  l;
	  int j = 3;
#if !CALL_NWCONN_OVER_SOCKET
	  close(fds[1]);   /* no writing    */
	  dup2(fds[0], 0); /* becomes stdin */
	  close(fds[0]);   /* not needed    */
#else
          dup2(ipx_fd, 0); /* becomes stdin */
          close(ipx_fd);
#endif

          dup2(ncp_fd, 3); /* becomes 3 */
	  while (j++ < highest_fd) close(j);  /* close all > 3 */

	  sprintf(connstr, "%d", connection);
	  ipx_addr_to_adr(addrstr, addr);

#if !CALL_NWCONN_OVER_SOCKET
          l=sprintf(divstr, "()INIT-:%08x,%04x,%04x,%08x-",
                 akt_pid, sock_nwbind, sock_echo, nwconn_state_shm_id);
#else
	  l=sprintf(divstr, "()INIT-:%08x,%04x,%04x-", 
	         akt_pid, sock_nwbind, sock_echo);
#endif    
          
          if (l < 48) {
            memset(divstr+l, '-', 48-l);
            *(divstr+48)='\0';
          }
	  execl(get_exec_path(pathname, progname), progname,
	          connstr, addrstr, divstr, NULL);

	  exit(1);  /* normaly not reached */
        }
	c->pid = pid;

#if CALL_NWCONN_OVER_SOCKET
        c->fd = (int) GET_BE16(my_addr.sock);
        close(ipx_fd);
#else
	c->fd  = fds[1];
	close(fds[0]);   /* no need to read */
#endif
        XDPRINTF((5,0, "AFTER FORK new PROCESS =%d, connection=%d", pid, connection));
      }
    }
  }
  if (connection>0) {
    uint8 buff[sizeof(ipxAddr_t)+sizeof(uint16)+sizeof(uint32)];
    memcpy(buff, addr, sizeof(ipxAddr_t));
#if CALL_NWCONN_OVER_SOCKET
    /* here i can use the nwconn socket */
    U16_TO_BE16(connections[connection-1].fd, buff+sizeof(ipxAddr_t));
#else
    /* and in this mode all must be go over ncpserv */
    U16_TO_BE16(SOCK_NCP, buff+sizeof(ipxAddr_t));
#endif
    U32_TO_BE32(connections[connection-1].pid,
                buff+sizeof(ipxAddr_t)+sizeof(uint16));
    nwserv_insert_conn(connection, (char*)buff, sizeof(buff));
  }
  return(connection);
}

#if CALL_NWCONN_OVER_SOCKET
static void send_to_nwconn(int nwconn_sock, char *data, int size)
{
  ipxAddr_t nwconn_addr;
  memcpy(&nwconn_addr, &my_addr, sizeof(ipxAddr_t));
  U16_TO_BE16(nwconn_sock, nwconn_addr.sock);
  send_ipx_data(ipx_out_fd, 17, size, data, &nwconn_addr, NULL);
}
#endif


static void ncp_response(int type, int sequence,
	          int connection,      int task,
	          int completition,    int connect_status,
	          int data_len)
{
  IPX_DATA     ipx_out_data;
  NCPRESPONSE  *ncpresponse=(NCPRESPONSE*)&ipx_out_data;
  U16_TO_BE16(type, ncpresponse->type);
  ncpresponse->sequence       = (uint8) sequence;
  ncpresponse->connection     = (uint8) connection;
  ncpresponse->task           = (uint8) task;
  ncpresponse->high_connection= (connection >> 8) & 0xff;
  ncpresponse->completition   = (uint8)completition;
  ncpresponse->connect_status = (uint8)connect_status;
  if (nw_debug){
    char comment[80];
    sprintf(comment, "NCP-RESP compl=0x%x ", completition);
    send_ipx_data(ncp_fd, 17, sizeof(NCPRESPONSE) + data_len,
	                 (char *) ncpresponse,
	                 &from_addr, comment);
  } else
    send_ipx_data(ncp_fd, 17, sizeof(NCPRESPONSE) + data_len,
	               (char *) ncpresponse,
	                &from_addr, NULL);
}


static void close_all(void)
{
  int k=0;
  while (k++ < count_connections) clear_connection(k);
  kill_connections();
  k=0;
  while (k++ < count_connections) clear_connection(k);
  kill_connections();
  if (ncp_fd > -1) {
    t_unbind(ncp_fd);
    t_close(ncp_fd);
    XDPRINTF((2,0, "LEAVE ncpserv"));
    ncp_fd = -1;
    if (ipx_out_fd > -1) {
      t_unbind(ipx_out_fd);
      t_close(ipx_out_fd);
      ipx_out_fd = -1;
    }
  }
}

static int server_is_down=0;
static void sig_quit(int isig)
{
  server_is_down++;
}

static int got_sig_child=0;
static void sig_child(int isig)
{
  got_sig_child++;
}

static void set_sig(void)
{
  signal(SIGQUIT,  sig_quit);
  signal(SIGTERM,  SIG_IGN);
  signal(SIGINT,   SIG_IGN);
  signal(SIGPIPE,  SIG_IGN);
  signal(SIGCHLD,  sig_child);
  signal(SIGHUP,   SIG_IGN);
}

static int xread(IPX_DATA *ipxd, int *offs, uint8 *data, int size)
{
  if (size > -1 && *offs + size > ipxd->owndata.d.size) {
    errorp(1, "xread:", "prog error: *offs=%d + size=%d > d.size=%d",
                  *offs, size, ipxd->owndata.d.size);
    size = -1;
  }
  if (size > -1) {
    memcpy(data,  ((uint8 *)&(ipxd->owndata.d.function)) + *offs, size);
    *offs+=size;
  } else {
    errorp(1, "xread:", "readerror");
  }
  return(size);
}

static int handle_ctrl(void)
/* packets from nwserv */
{
  int   what;
  int   conn;
  int   result   = 0;
  int   offs=0;
  IPX_DATA *ipxd =  (IPX_DATA*)&ipx_in_data;
  int   data_len =  xread(ipxd, &offs, (uint8*)&(what), sizeof(int));

  if   (data_len == sizeof(int)) {
    XDPRINTF((2, 0, "GOT CTRL what=0x%x, len=%d", what, ipxd->owndata.d.size));
#if 1
    (void)send_own_reply(ipx_out_fd, 0, ipxd->owndata.h.sequence, &from_addr);
#endif
    switch (what) {
      case 0x5555 : /* clear_connection, from wdog process */
        if (sizeof (int) ==
            xread(ipxd, &offs, (uint8*)&(conn), sizeof(int)))
          clear_connection(conn);
        break;

      case 0xeeee:
        get_ini_debug(NCPSERV);
        get_ini(0);
        break;

      case 0xffff : /* server down */
        data_len = xread(ipxd, &offs, (uint8*)&conn, sizeof(int));
        if (sizeof(int) == data_len && conn == what)
          server_goes_down++;
        break;

      default : break;
    } /* switch */
    result++;
  } else {
    errorp(1, "handle_ctrl", "wrong data len=%d", data_len);
  }
  return(result);
}

static void handle_ncp_request(void)
{
  if (t_rcvudata(ncp_fd, &ud, &rcv_flags) > -1){
    int type;
    int compl;
    int cstat;
    in_len = ud.udata.len;
    time(&akttime);
    XDPRINTF((20, 0, "NCPSERV-LOOP von %s", visable_ipx_adr(&from_addr)));
    if ((type = GET_BE16(ncprequest->type)) == 0x2222
                                    || type == 0x5555) {
      int connection     = (int)ncprequest->connection
                         | (((int)ncprequest->high_connection) << 8);

      XDPRINTF((10,0, "GOT 0x%x in NCPSERV connection=%d", type, connection));
#if 0
      ncp_response(0x9999, ncprequest->sequence,
    	                    connection, ncprequest->task,
    	                    0x0, 0, 0);
#endif
      if ( connection > 0 && connection <= count_connections) {
        CONNECTION *c = &(connections[connection-1]);

        if (!memcmp(&from_addr, &(c->client_adr), sizeof(ipxAddr_t))) {
          if (c->fd > -1){
            if (type == 0x2222) {
              int diff_time  = akttime - c->last_access;
              c->last_access = akttime;
              if (diff_time > 50) /* after max. 50 seconds */
                 nwserv_reset_wdog(connection);
                 /* tell the wdog there's no need to look */

#if !CALL_NWCONN_OVER_SOCKET
              if (ncprequest->sequence == c->sequence
                  && nwconn_state[connection] > 0) { /* check, is nwconn
                                                      * actually busy? */
                ncp_response(0x9999, ncprequest->sequence,
    	                    connection, ncprequest->task,
    	                    0x0, 0, 0);
                XDPRINTF((2, 0, "Send Request being serviced, connection:%d, func=%x, difftime=%d, task=%d", connection, ncprequest->function, diff_time, ncprequest->task));
                return;
              }
#endif
              if (1) {
                int anz=
#if CALL_NWCONN_OVER_SOCKET
                in_len;
                send_to_nwconn(c->fd, (char*)ncprequest, in_len);
#else
                write(c->fd, (char*)ncprequest, in_len);
#endif
                XDPRINTF((10,0, "write to %d, anz = %d", c->fd, anz));
              }
              c->sequence    = ncprequest->sequence; /* save last sequence */
              return;
            } else {  /* 0x5555, close connection  */

#if !CALL_NWCONN_OVER_SOCKET
              if ( (uint8) (c->sequence+1) == (uint8) ncprequest->sequence)
#endif
              {
                clear_connection(connection);
                ncp_response(0x3333,
                             ncprequest->sequence,
                             connection,
         	                 0,    /* task here is 0 !? */
         	                 0x0,  /* completition */
         	                 0,    /* conn status  */
         	                 0);
                return;
              }
            }
          }
        }
        /* here someting is wrong */
        XDPRINTF((1,0, "Not ok:0x%x,%s,fd=%d,conn=%d of %d",
             type,
             visable_ipx_adr(&from_addr),
             c->fd,
             (int)ncprequest->connection
               | (((int)ncprequest->high_connection) << 8),
             count_connections));
      } else {
        /* here the connection number is wrong */
        XDPRINTF((1,0, "Not ok:0x%x conn=%d of %d conns",
          type, ncprequest->connection,
          count_connections));
      }

      if (type == 0x5555 || (type == 0x2222 && ncprequest->function == 0x19)) {
        compl = 0;
        cstat = 0;
      } else {
        compl = 0;
        cstat = 1;
      }
      ncp_response(0x3333, ncprequest->sequence,
    	               (int)ncprequest->connection
                         | (((int)ncprequest->high_connection) << 8),
    	               (type== 0x5555) ? 0 : ncprequest->task,     /* task  */
    	               compl, /* completition */
    	               cstat, /* conn status  */
	               0);

#if ENABLE_BURSTMODE
    } else if (type == 0x7777) { /* BURST-mode */
      int connection = (int) GET_16(((BURSTPACKET*)ncprequest)->dest_conn);
      if ( connection > 0 && connection <= count_connections) {
        CONNECTION *c = &(connections[connection-1]);
#if CALL_NWCONN_OVER_SOCKET
        send_to_nwconn(c->fd, (char*)ncprequest, in_len);
#else
        write(c->fd, (char*)ncprequest, in_len);
#endif
      }
#endif

#if !CALL_NWCONN_OVER_SOCKET
    /* here comes a call from nwbind */
    } else if (type == 0x3333
       && IPXCMPNODE(from_addr.node, my_addr.node)
       && IPXCMPNET (from_addr.net,  my_addr.net)) {
      int connection = (int)ncprequest->connection
                    | (((int)ncprequest->high_connection) << 8);
      XDPRINTF((6,0, "GOT 0x3333 in NCPSERV connection=%d", connection));
      if ( connection > 0 && connection <= count_connections) {
        CONNECTION *c = &(connections[connection-1]);
        if (c->fd > -1) write(c->fd, (char*)ncprequest, in_len);
      }
#endif
    } else if (type == 0x1111) {
      /* GIVE CONNECTION Nr connection */
      int connection = 0;

      if (!server_goes_down) {
        if (!station_restrictions)
          connection=find_get_conn_nr(&from_addr);
        else {
          int do_sent = (station_restrictions == 1) ? 1 : 0;
          if (find_station_match(2, &from_addr))
             do_sent = !do_sent;
          if (do_sent)
            connection=find_get_conn_nr(&from_addr);
        }
      }

      XDPRINTF((2, 0, "GIVE CONNECTION NR=%d", connection));
      if (connection) {
        CONNECTION *c = &(connections[connection-1]);
        int anz;
        c->sequence   = 0;

#if CALL_NWCONN_OVER_SOCKET
        anz = in_len;
        send_to_nwconn(c->fd, (char*)ncprequest, in_len);
#else
        anz=write(c->fd, (char*)ncprequest, in_len);
#endif
        XDPRINTF((10, 0, "write to oldconn %d, anz = %d", c->fd, anz));
      } else  /* no free connection */
        ncp_response(0x3333, 0, 0, 0,
                     0xf9, /* completition */
                     0,    /* conn status  */
                     0);

    } else if (type == 0xeeee
       && IPXCMPNODE(from_addr.node, my_addr.node)
       && IPXCMPNET (from_addr.net,  my_addr.net)) {
      /* comes from nwserv  */
      handle_ctrl();
#ifdef _MAR_TESTS_xx
    } else if (type == 0xc000) {
      /* rprinter */
      int connection     = (int)ncprequest->connection
                         | (((int)ncprequest->high_connection) << 8);
      int sequence       = (int)ncprequest->sequence;
      ncp_response(0x3333, sequence, connection, 1, 0x0, 0, 0);
#endif
    } else {
      int connection     = (int)ncprequest->connection
                         | (((int)ncprequest->high_connection) << 8);
      int sequence       = (int)ncprequest->sequence;
      XDPRINTF((1,0, "Got UNKNOWN TYPE: 0x%x", type));
      ncp_response(0x3333, sequence, connection, 1, 0xfb, 0, 0);
    }
  }
}

int main(int argc, char *argv[])
{
  init_tools(NCPSERV, 0);
  if (argc != 5) {
    errorp(1, "Usage:", "%s: nwname address nwbindsock echosocket", argv[0]);
    return(1);
  }
  get_ini(1);
  if (max_connections < 1)
    max_connections=1;
  connections=(CONNECTION*)xcmalloc(max_connections*sizeof(CONNECTION));
  strncpy(my_nwname, argv[1], 48);
  my_nwname[47] = '\0';
  adr_to_ipx_addr(&my_addr, argv[2]);
  sscanf(argv[3], "%x", &sock_nwbind);
  sscanf(argv[4], "%x", &sock_echo);
  
#if !CALL_NWCONN_OVER_SOCKET
  nwconn_state_shm_id = shmget(IPC_PRIVATE, MAX_CONNECTIONS, IPC_CREAT|0600);
  if (nwconn_state_shm_id == -1) {
    errorp(1, "Can't get shared memory", NULL);
    return(1);
  }
  nwconn_state = shmat(nwconn_state_shm_id, NULL, SHM_R);
  if ((int )(nwconn_state) == -1) {
    errorp(1, "Can't attach shared memory segment", NULL);
    return(1);
  }
  shmctl(nwconn_state_shm_id, IPC_RMID, NULL); /* mark shm as destroyed,
                                                * it will actually be destroyed
                                                * after program exit. /lenz */
#endif

#ifdef LINUX
  set_emu_tli();
#endif

  if (open_ipx_sockets()) {
    errorp(1, "open_ipx_sockets", NULL);
    return(1);
  }

  XDPRINTF((1, 0, "USE_PERMANENT_OUT_SOCKET %s",
                (ipx_out_fd > -1) ? "enabled" : "disabled"));

  ud.opt.len       = sizeof(ipx_pack_typ);
  ud.opt.maxlen    = sizeof(ipx_pack_typ);
  ud.opt.buf       = (char*)&ipx_pack_typ; /* gets actual Typ */

  ud.addr.len      = sizeof(ipxAddr_t);
  ud.addr.maxlen   = sizeof(ipxAddr_t);
  ud.addr.buf      = (char*)&from_addr;

  ud.udata.len     = IPX_MAX_DATA;
  ud.udata.maxlen  = IPX_MAX_DATA;
  ud.udata.buf     = (char*)&ipx_in_data;
  set_sig();

  while (!server_is_down) {
    handle_ncp_request();
    if (got_sig_child) {
      XDPRINTF((2, 0, "Got SIGCHLD"));
      kill_connections();
      got_sig_child=0;
      signal(SIGCHLD, sig_child);
    }
  }
  close_all();
  xfree(connections);
  return(0);
}



