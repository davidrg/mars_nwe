/* ncpserv.c, 24-Dec-95 */

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
#include "nwdbm.h"
static  struct     pollfd polls[2];
static  int        ncp_fd = -1;
static  uint8      ipx_in_data[IPX_MAX_DATA+500]; /* space for additional data */
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

static void write_to_nwserv(int what, int connection, int mode,
       	    			char *data, int size)
{
  switch (what) {
    case 0x2222  : /* insert wdog connection */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   write(FD_NWSERV, &size,       sizeof(int));
                   write(FD_NWSERV, data,        size);  /* ipxAddr_t */
                   break;

    case 0x4444  : /* tell the wdog there's no need to look  0 */
                   /* fast activate wdog to free connection  1 */
                   /* slow activate wdog to free connection  2 */
                   /* the connection ist closed        	    99 */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   write(FD_NWSERV, &mode,       sizeof(int));
                   break;

    case 0x6666  : /* send to client that server holds message */
                   write(FD_NWSERV, &what,       sizeof(int));
                   write(FD_NWSERV, &connection, sizeof(int));
                   break;

    case 0xffff  : /* say nwserv to down the server */
                   write(FD_NWSERV, &what, sizeof(int));
                   write(FD_NWSERV, &what, sizeof(int));
                   break;

    default     :  break;
  }
}

#define nwserv_insert_wdog(connection, adr) \
   write_to_nwserv(0x2222, (connection), 0, (adr), sizeof(ipxAddr_t))

#define nwserv_handle_wdog(connection, mode) \
   write_to_nwserv(0x4444, (connection), (mode), NULL, 0)

#define nwserv_reset_wdog(connection) \
   write_to_nwserv(0x4444, (connection), 0,  NULL, 0)

#define nwserv_close_wdog(connection) \
   write_to_nwserv(0x4444, (connection), 99, NULL, 0)

#define nwserv_handle_msg(connection) \
   write_to_nwserv(0x6666, (connection), 0, NULL, 0)

#define nwserv_down_server() \
   write_to_nwserv(0xffff, 0, 0, NULL, 0)

static int open_ncp_socket()
{
  struct t_bind bind;
  ncp_fd=t_open("/dev/ipx", O_RDWR, NULL);
  if (ncp_fd < 0) {
    if (nw_debug) t_error("t_open !Ok");
    return(-1);
  }
  U16_TO_BE16(SOCK_NCP, my_addr.sock); /* NCP_SOCKET */
  bind.addr.len    = sizeof(ipxAddr_t);
  bind.addr.maxlen = sizeof(ipxAddr_t);
  bind.addr.buf    = (char*)&my_addr;
  bind.qlen        = 0; /* immer */
  if (t_bind(ncp_fd, &bind, &bind) < 0){
    if (nw_debug) t_error("t_bind in open_ncp_socket !OK");
    close(ncp_fd);
    return(-1);
  }
  return(0);
}

typedef struct {
   int        fd;           /* writepipe             */
   int        pid;          /* pid from son          */
   ipxAddr_t  client_adr;   /* address client        */
   uint32     object_id;    /* logged object         */
   	      		    /* 0 = not logged in     */
   uint8      crypt_key[8]; /* password generierung  */
   uint8      message[60];  /* saved BCastmessage    */
   int        sequence;     /* previous sequence     */
   int        retry;	    /* one reply being serviced is sent */
   time_t     last_access;  /* time of last 0x2222 request */
   time_t     t_login;      /* login time            */
} CONNECTION;

static CONNECTION connections[MAX_CONNECTIONS];
static int 	  anz_connect=0;   /* actual anz connections */

static int new_conn_nr(void)
{
  int  j = -1;
  int  not_logged=-1;
  if (!anz_connect){ /* init all */
    j = MAX_CONNECTIONS;
    while (j--) {
      connections[j].fd       	= -1;
      connections[j].message[0] = '\0';
    }
    anz_connect++;
    return(1);
  }
  j = -1;
  while (++j < MAX_CONNECTIONS) {
    CONNECTION *c=&(connections[j]);
    if (c->fd < 0) {
      c->message[0] = '\0';
      if (++j > anz_connect) anz_connect=j;
      return(j);
    }
  }
  /* nothing free */
  j=MAX_CONNECTIONS;
  while (j--)  {
    CONNECTION *c=&(connections[j]);
    if (!c->object_id) {  /* NOT LOGGED IN */
      /* makes wdog test faster */
      nwserv_handle_wdog(j+1, 1);
      return(0);
    }
  }
  j=0;
  while (j++ < MAX_CONNECTIONS) nwserv_handle_wdog(j, 2);
  return(0); /* nothing free */
}

int free_conn_nr(int nr)
{
  if (nr && --nr < anz_connect) {
    connections[nr].fd = -1;
    return(0);
  }
  return(-1);
}

int find_conn_nr(ipxAddr_t *addr)
{
  int j = -1;
  while (++j < anz_connect) {
    if (connections[j].fd > -1 &&
      !memcmp((char*)&(connections[j].client_adr),
	      (char*)addr, sizeof(ipxAddr_t))) return(++j);
  }
  return(0);
}

void clear_connection(int conn)
{
  nwserv_close_wdog(conn);
  if (conn > 0 && --conn < anz_connect) {
    CONNECTION *c = &connections[conn];
    if (c->fd > -1) {
      close(c->fd);
      c->fd = -1;
      if (c->pid > -1) {
        kill(c->pid, SIGTERM); /* hier nochmal's killen */
        c->pid = -1;
      }
    }
    c->object_id = 0;
    conn = anz_connect;
    while (conn--) {
      CONNECTION *c = &connections[conn];
      if (c->fd < 0) anz_connect--;
      else break;
    }
  }
}

int find_get_conn_nr(ipxAddr_t *addr)
{
  int connection=find_conn_nr(addr);
  if (!connection){
    if ((connection = new_conn_nr()) > 0){
      CONNECTION *c=&(connections[connection-1]);
      int fds[2];
      memcpy((char*) &(c->client_adr), (char *)addr, sizeof(ipxAddr_t));
      if (pipe(fds) < 0) {
	errorp(0, "find_get_conn_nr, pipe", NULL);
	free_conn_nr(connection);
	return(0);
      } else {
	int akt_pid = getpid();
	int pid     = fork();
	if (pid < 0) {
	  errorp(0, "find_get_conn_nr, fork", NULL);
	  free_conn_nr(connection);
	  close(fds[0]);
	  close(fds[1]);
	  return(0);
	}
	if (pid == 0) {
         /* new process */
	  char *progname="nwconn";
	  char pathname[300];
	  char pidstr[20];
	  char connstr[20];
	  char addrstr[100];
	  int j = 2;
	  close(fds[1]);   /* no writing    */
	  dup2(fds[0], 0); /* becomes stdin */
	  close(fds[0]);   /* not needed    */

	  while (j++ < 100) close(j);  /* close all > stderr */

	  sprintf(pidstr, "%d", akt_pid);
	  sprintf(connstr, "%d", connection);
	  ipx_addr_to_adr(addrstr, addr);

	  execl(get_exec_path(pathname, progname), progname,
	          pidstr, addrstr, connstr, NULL);
	  exit(1);  /* normaly not reached */
        }
	c->pid = pid;
	c->fd  = fds[1];
	close(fds[0]);   /* no need to read */
        XDPRINTF((5,0, "AFTER FORK new PROCESS =%d, connection=%d", pid, connection));
      }
    }
  }
  if (connection) nwserv_insert_wdog(connection, (char*)addr);
  return(connection);
}

static void sent_down_message(void)
{
  int k = -1;
  server_goes_down++;
  while (++k < anz_connect) {
    CONNECTION *cn=&connections[k];
    if (cn->fd > -1) {
      strmaxcpy(cn->message, "SERVER IS GOING DOWN", 58);
      nwserv_handle_msg(k+1);
    }
  } /* while */
}

static void get_login_time(uint8 login_time[], CONNECTION *cx)
{
  struct tm *s_tm = localtime(&(cx->t_login));

  login_time[0] = s_tm->tm_year;
  login_time[1] = s_tm->tm_mon+1;
  login_time[2] = s_tm->tm_mday;

  login_time[3] = s_tm->tm_hour;
  login_time[4] = s_tm->tm_min;
  login_time[5] = s_tm->tm_sec;
  login_time[6] = s_tm->tm_wday;
}


static int handle_fxx(CONNECTION *c, int gelen, int func)
/* here are handled the global 0x15, 0x17 functions */
{
  IPX_DATA     ipxoutdata;
  NCPRESPONSE  *ncpresponse  = (NCPRESPONSE*)&ipxoutdata;
  uint8        *responsedata = ((uint8*)&ipxoutdata)+sizeof(NCPRESPONSE);
  uint8        *requestdata  = ((uint8*)ncprequest)+sizeof(NCPREQUEST);

  uint8        len           = *(requestdata+1);
  uint8        ufunc         = *(requestdata+2);
  uint8        *rdata        = requestdata+3;
  uint8        completition  = 0;
  uint8        connect_status= 0;
  int          data_len      = 0;

  if (nw_debug > 1){
    int j = gelen - sizeof(NCPREQUEST);
    if (nw_debug){
      XDPRINTF((1, 0, "NCP 0x%x REQUEST:ufunc:0x%x", func, ufunc));
      if (j > 0){
	uint8  *p=requestdata;
	XDPRINTF((1, 2, "len %d, DATA:", j));
	while (j--) {
	  int c = *p++;
	  if (c > 32 && c < 127)  XDPRINTF((1, 3, ",\'%c\'", (char) c));
	  else XDPRINTF((1, 3, ",0x%x", c));
	}
	XDPRINTF((1, 1, NULL));
      }
    }
  }

  if (0x15 == func) {
    switch (ufunc) {  /* Messages */
      case 0x0 :  {   /* Send Broadcast Message (old) */
        int anz_conns   = (int)*(rdata);        /* Number of connections */
        uint8  *conns   = rdata+1;        	/* connectionslist */
        int msglen      = *(conns+anz_conns);
        uint8 *msg      = conns+anz_conns+1;
        uint8 *p        = responsedata;
        int   one_found = 0;
        int   k         = -1;
        *p++            = (uint8) anz_conns;
        while (++k < anz_conns) {
          int connr   =  (int) (*conns++);
          int result  =  0xff; /* target not ok */
          CONNECTION *cn;
          if (connr > 0 && --connr < anz_connect
            && ((cn = &connections[connr]))->fd > -1 ) {
            if (!cn->message[0]) {
              strmaxcpy(cn->message, msg, min(58, msglen));
              result      = 0;    /* Ok */
            } else result = 0xfc; /* server holds message */
            nwserv_handle_msg(connr+1);
            one_found++;
          }
          *p++ = (uint8)result;
        }
        if (one_found) data_len = anz_conns+1;
        else completition=0xff;
      }
      break;

      case 0x01:  { /* Get Broadcast Message (old) */
        *responsedata = (uint8) strmaxcpy(responsedata+1, c->message, 58);
        c->message[0] = '\0';
        data_len = (int)(*responsedata) + 1;
      }
      break;

      case 0x03:  { /* Enable Broadcasts */
        ;;;
        XDPRINTF((2, 0, "TODO: enable Broadcasts"));
      }
      break;

      case 0x09:  { /* Broadcast to CONSOLE */
        char message[60];
        strmaxcpy(message, rdata+1, min(59, *rdata));
        fprintf(stderr, "\n:%s\n", message);
      }
      break;

      case 0xa:  /* Send Broadcast Message (new) */
      case 0xb:  /* Get Broadcast Message (new) */
      default : return(-1); /* not handled  */
    } /* switch */
  } else if (0x17 == func) { /* Fileserver Enviro */
    switch (ufunc) {
     case 0x01 :  { /* Change User Password OLD */
                     completition=0xff;
   	     	  }
                  break;

     case 0x0c :  { /* Verify Serialization */
                     completition=0xff;
   	     	  }
                  break;

     case 0x0e :  { /* Get Disk Utilization */
                     completition=0xff;
   	     	  }
                  break;

     case 0x11 :  { /* Get FileServer Info */
	             struct XDATA {
	               uint8 servername[48];
	               uint8 version;    /* 2  or   3 */
	               uint8 subversion; /* 15 or  11 */
	               uint8 maxconnections[2];
	               uint8 connection_in_use[2];
	               uint8 max_volumes[2];
	               uint8 os_revision;
	               uint8 sft_level;
	               uint8 tts_level;
	               uint8 peak_connection[2];
	               uint8 accounting_version;
	               uint8 vap_version;
	               uint8 queuing_version;
	               uint8 print_server_version;
	               uint8 virtual_console_version;
	               uint8 security_level;
	               uint8 internet_bridge_version;
	               uint8 reserved[60];
	             } *xdata = (struct XDATA*) responsedata;
                     int k, i;
	             memset(xdata, 0, sizeof(struct XDATA));
	             strcpy(xdata->servername, my_nwname);

                     if (!tells_server_version) {
	               xdata->version    =  2;
	               xdata->subversion = 15;
                     } else {
	               xdata->version    =  3;
	               xdata->subversion = 11;
                     }

                     i=0;
                     for (k=0; k < anz_connect; k++) {
                       if (connections[k].fd > -1) i++;
                     }
	             U16_TO_BE16(i, xdata->connection_in_use);
	             U16_TO_BE16(MAX_CONNECTIONS, xdata->maxconnections);
	             U16_TO_BE16(anz_connect,     xdata->peak_connection);
	             U16_TO_BE16(MAX_NW_VOLS,     xdata->max_volumes);
	             data_len = sizeof(struct XDATA);
	           } break;

     case 0x12 :  { /* Get Network Serial Number */
	             struct XDATA {
	               uint8 serial_number[4];
                       uint8 appl_number[2];
	             } *xdata = (struct XDATA*) responsedata;
                     /* serial-number 4-Byte */
	             U32_TO_BE32(0x44444444, xdata->serial_number);
                     /* applikation-number 2-Byte */
	             U16_TO_BE16(0x2222,     xdata->appl_number);
	             data_len = sizeof(struct XDATA);
   	     	  }
                  break;

     case 0x13 :  { /* Get Connection Internet Address */
	            int conn  = (int)*(rdata);  /* Connection Nr */
	            if (conn && --conn < anz_connect
	              && connections[conn].fd > -1 ) {
	              CONNECTION *cx=&(connections[conn]);
	              data_len = sizeof(ipxAddr_t);
	              memcpy(responsedata, (char*)&(cx->client_adr), data_len);
	            } else completition = 0xff;
	          } break;

     case 0x14 :  { /* Login Objekt, unencrypted passwords */
	            uint8  *p    =  rdata;
                    uint8  *p1   =  p+3 + *(p+2);  /* here is password */
	            int    result;
	            NETOBJ obj;
                    char   password[80];
	            obj.type     =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)(p+3), (int) *(p+2));
                    upstr(obj.name);
	            strmaxcpy(password, (char*)(p1+1),
	                max(sizeof(password)-1, (int) *p1));
                    XDPRINTF((1, 0, "TODO:LOGIN unencrypted PW NAME='%s', PASSW='%s'",
                             obj.name, password));
	            if (0 == (result = find_obj_id(&obj, 0))) {
                       /* TODO: check password  !!!!!!! */
                      ;;
                      result = 0xff;
                    }
	            if (!result) {
	              c->object_id = obj.id;     /* actuell Object ID  */
                      c->t_login   = akttime;    /* u. login Time      */
                      get_guid(rdata+2, rdata+2+sizeof(int),  obj.id);
                      in_len=12 + 2*sizeof(int);
                      return(-1); /* nwconn must do the rest */
	            } else completition = (uint8) -result;
	          } break;


     case 0x16 :  { /* Get Connection Info, OLD */
	            struct XDATA {
	              uint8 object_id[4];
	              uint8 object_type[2];
	              uint8 object_name[48];
	              uint8 login_time[7];
                      uint8 reserved;
	            } *xdata = (struct XDATA*) responsedata;
	            int conn = (uint16)*(rdata);  /* Connection Nr */
                    memset(xdata, 0, sizeof(struct XDATA));
                    data_len = sizeof(struct XDATA);
	            if (conn && conn <= anz_connect
	                     && connections[conn-1].fd > -1 ) {
	              CONNECTION *cx=&(connections[conn-1]);
	              NETOBJ    obj;
	              int       result;
	              obj.id =  cx->object_id;
	              result =  nw_get_obj(&obj);
	              if (!result) {
	                memset(xdata, 0, sizeof(struct XDATA));
	                U32_TO_BE32(obj.id,   xdata->object_id);
	                U16_TO_BE16(obj.type, xdata->object_type);
	                strncpy(xdata->object_name, obj.name, 48);
                        get_login_time(xdata->login_time, cx);
	              } /*  else completition = (uint8)(-result); */
	            } else if (!conn || conn > MAX_CONNECTIONS) {
                      data_len     = 0;
                      completition = 0xfd;
                    }
	          } break;

     case 0x17 :  { /* get crypt key */
                    int    k   = sizeof(c->crypt_key);
                    uint8 *p   = c->crypt_key;
                    uint8 *pp  = responsedata;
                    data_len   = k;
                    while (k--) *pp++ = *p++ = (uint8) rand();
                    /* if all here are same (1 or 2) then the resulting key is */
                    /* 00000000  */
                  }
                  break;

     case 0x18 :  { /* crypt_keyed LOGIN */
                    uint8 *p     =  rdata+sizeof(c->crypt_key);
	            NETOBJ obj;
                    int    result;
	            obj.type     =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
                    XDPRINTF((2, 0, "LOGIN CRYPTED PW NAME='%s'",obj.name));
	            if (0 == (result = find_obj_id(&obj, 0)))
                      result=nw_test_passwd(obj.id, c->crypt_key, rdata);
	            if (result > -1) {
	              c->object_id = obj.id;        /* actuell Object */
                      c->t_login   = akttime;       /* and login time */
                      get_guid(rdata+2, rdata+2+sizeof(int),  obj.id);
                      in_len=12 + 2*sizeof(int);
                      return(-1); /* nwconn must do the rest */
	            } else completition = (uint8) -result;
     	       	  }
                  break;

     case 0x1c :  { /* Get Connection Info, new */
	            struct XDATA {
	              uint8 object_id[4];
	              uint8 object_type[2];
	              uint8 object_name[48];
	              uint8 login_time[7];
                      uint8 reserved;
	            } *xdata = (struct XDATA*) responsedata;
	            int conn   = (uint16)*(rdata);  /* Connection Nr */
	            if (conn && --conn < anz_connect){
	              CONNECTION *cx=&(connections[conn]);
	              NETOBJ    obj;
	              int       result;
	              obj.id =  cx->object_id;
	              result =  nw_get_obj(&obj);
	              if (!result) {
	                memset(xdata, 0, sizeof(struct XDATA));
	                U32_TO_BE32(obj.id,   xdata->object_id);
	                U16_TO_BE16(obj.type, xdata->object_type);
	                strncpy(xdata->object_name, obj.name, 48);
                        get_login_time(xdata->login_time, cx);
	                data_len = sizeof(struct XDATA);
	              } else completition = (uint8)(-result);
	            } else completition = 0xff;
	          } break;


     case 0x32 :  {  /* Create Bindery Object */
	            NETOBJ obj;
	            int    result;
	            uint8  *p           =  rdata;
	            obj.flags           =  *p++;
	            obj.security        =  *p++;
	            obj.type            =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)p+3, (int) *(p+2));
	            result = nw_create_obj(&obj, 0);
	            if (result < 0) completition = (uint8) -result;
	          } break;

     case 0x33 :  {  /* delete OBJECT */
	            uint8  *p           =  rdata;
	            int    result;
	            NETOBJ obj;
	            obj.type            =  GET_BE16(p);
	            strmaxcpy((char*)obj.name,  (char*)(p+3), (int) *(p+2));
	            result = nw_delete_obj(&obj);
	            if (result < 0) completition = (uint8) -result;
	          } break;

     case 0x34 :  {  /* rename OBJECT, only SU */
	            int result=-0xff;
                    if (1 == c->object_id) {
	              uint8  *p           =  rdata;
	              NETOBJ obj;
                      uint8  newname[256];
                      uint8  *p1  =  p+3 + *(p+2); /* new Name Length */
	              obj.type    =  GET_BE16(p);
	              strmaxcpy((char*)obj.name, (char*)(p+3),  (int) *(p+2));
	              strmaxcpy((char*)newname,  (char*)(p1+1), (int) *(p1));
	              result = nw_rename_obj(&obj, newname);
                    }
	            if (result) completition = (uint8) -result;
	          } break;


     case 0x35 :  {  /* get Bindery Object ID */
	            struct XDATA {
	              uint8 object_id[4];
	              uint8 object_type[2];
	              uint8 object_name[48];
	            } *xdata = (struct XDATA*) responsedata;
	            uint8  *p           =  rdata;
	            int    result;
	            NETOBJ obj;
	            obj.type            =  GET_BE16(p);
	            strmaxcpy((char*)obj.name,  (char*)(p+3), (int) *(p+2));
	            result = find_obj_id(&obj, 0);
	            if (!result){
	              U32_TO_BE32(obj.id,   xdata->object_id);
	              U16_TO_BE16(obj.type, xdata->object_type);
	              strncpy(xdata->object_name, obj.name, 48);
	              data_len          = sizeof(struct XDATA);
	            } else completition = (uint8) -result;
	          } break;

     case 0x36 :  {  /* get Bindery Object Name */
	            struct XDATA {
	              uint8 object_id[4];
	              uint8 object_type[2];
	              uint8 object_name[48];
	            } *xdata = (struct XDATA*) responsedata;
	            uint8 *p = rdata;
	            int    result;
	            NETOBJ obj;
	            obj.id = GET_BE32(p);
	            result = nw_get_obj(&obj);
	            if (!result){
	              U32_TO_BE32(obj.id,   xdata->object_id);
	              U16_TO_BE16(obj.type, xdata->object_type);
	              strncpy(xdata->object_name, obj.name, 48);
	              data_len = sizeof(struct XDATA);
	            } else completition = (uint8) -result;
	          } break;

     case 0x37 :  {  /* Scan Bindery Object */
	            struct XDATA {
	              uint8 object_id[4];
	              uint8 object_type[2];
	              uint8 object_name[48];
	              uint8 object_flag;
	              uint8 object_security;
	              uint8 object_has_properties;
	            } *xdata = (struct XDATA*) responsedata;
	            uint32 last_obj_id  =  GET_BE32(rdata);
	            uint8  *p           =  rdata+4;
	            int    result;
	            NETOBJ obj;
	            obj.type            =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)(p+3),(int) *(p+2));
	            result = find_obj_id(&obj, last_obj_id);
	            if (!result){
	              U32_TO_BE32(obj.id,    xdata->object_id);
	              U16_TO_BE16(obj.type,  xdata->object_type);
	              strncpy(xdata->object_name, obj.name, 48);
	              xdata->object_flag       = obj.flags;
	              xdata->object_security   = obj.security;
	              if (nw_obj_has_prop(&obj) > 0)
	                 xdata->object_has_properties = 0xff;
	              else xdata->object_has_properties = 0;
	              data_len = sizeof(struct XDATA);
	            } else completition = (uint8) -result;
	          }
	          break;

     case 0x38 :  {  /* change Bindery Objekt Security */
                     /* only SU ! */
                    int result= -0xff;
                    if (1 == c->object_id) {
	              uint8  *p           =  rdata;
	              NETOBJ obj;
	              obj.type            =  GET_BE16(p+1);
	              strmaxcpy((char*)obj.name,  (char*)(p+4), (int) *(p+3));
	              result = nw_change_obj_security(&obj, (int)*p);
                    }
                    if (result < 0) completition = (uint8) -result;
	          } break;

     case 0x39 :  {  /* create Property */
	            uint8  *p             = rdata;
	            int object_type       = GET_BE16(p);
	            int object_namlen     = (int) *(p+=2);
	            uint8 *object_name    = ++p;
	            int prop_flags        = (int) *(p+=object_namlen);
	            int prop_security     = (int) *(++p);
	            int prop_namlen       = (int) *(++p);
	            uint8 *prop_name      = ++p;
	            int result = nw_create_prop( object_type,
	                         object_name, object_namlen,
	                         prop_name,   prop_namlen,
	                         prop_flags,  prop_security);
	            if (result) completition = (uint8) -result;
	          } break;


     case 0x3a :  {  /* delete property */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+2);
	            uint8 *object_name=  p+3;
	            int prop_namlen   =  (int) *(p+3+object_namlen);
	            uint8 *prop_name  =  p+4+object_namlen;
	            int result = nw_delete_property( object_type,
	                         object_name, object_namlen,
	                         prop_name, prop_namlen);
	            if (result < 0) completition = (uint8) -result;
	          } break;



     case 0x3b :  {  /* Change Prop Security */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+=2);
	            uint8 *object_name=  ++p;
	            int prop_security =  (int) *(p+=object_namlen);
	            int prop_namlen   =  (int) *(++p);
	            uint8 *prop_name  =  ++p;
	            int result = nw_change_prop_security(object_type,
	                         object_name, object_namlen,
	                         prop_name, prop_namlen, prop_security);
	            if (result) completition = (uint8) -result;
	          } break;

     case 0x3c :  {  /* Scan Property */
	            struct XDATA {
	              uint8 prop_name[16];
	              uint8 flags;       /* set=2, dynamic=1 */
	              uint8 security;
                      uint8 akt_scan[4];
	              uint8 has_value;   /* ff, if there are Prop's Values */
	              uint8 weisnicht;
	            } *xdata = (struct XDATA*) responsedata;
	            uint8  *p = rdata;
	            int object_type    =  GET_BE16(p);
	            int object_namlen  =  (int) *(p+2);
	            uint8 *object_name =  (p+=3);
	            uint32 last_scan   =  GET_BE32((p+object_namlen));
	            uint8  prop_namlen =  (int)*   (p+=object_namlen+4);
	            uint8 *prop_name   =  ++p;
                    NETPROP prop;
	            int result = nw_scan_property(&prop,
	                         object_type, object_name, object_namlen,
	                         prop_name, prop_namlen, &last_scan);
	            if (result > -1) {
                      strncpy(xdata->prop_name,
                                     prop.name, sizeof(xdata->prop_name));
                      U32_TO_BE32(last_scan, xdata->akt_scan);
	              xdata->flags      = prop.flags;
	              xdata->security   = prop.security;
	              xdata->has_value  = (uint8) result;
	              xdata->weisnicht  = 0x0;
	              data_len 	        = sizeof(struct XDATA);
	            } else completition = (uint8) -result;
	          } break;

     case 0x3d :  {  /* read Bindery Property Value */
	            struct XDATA {
	              uint8 property_value[128];
	              uint8 more_segments;
	              uint8 property_flags;
	            } *xdata = (struct XDATA*) responsedata;
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+2);
	            uint8 *object_name=  p+3;
	            int segment_nr    =  (int) *(p+3+object_namlen);
	            int prop_namlen   =  (int) *(p+4+object_namlen);
	            uint8 *prop_name  =  p+5+object_namlen;
	            int result = nw_get_prop_val( object_type,
	                         object_name, object_namlen,
	                         segment_nr,
	                         prop_name, prop_namlen,
	                         xdata->property_value,
	                         &(xdata->more_segments),
	                         &(xdata->property_flags));
	            if (!result){
	              data_len = sizeof(struct XDATA);
	            } else completition = (uint8) -result;
	          } break;

     case 0x3e :  {  /* write Bindery Property Value */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+2);
	            uint8 *object_name=  p+3;
	            int segment_nr    = (int) *(p+3+object_namlen);
	            int erase_segment = (int) *(p+4+object_namlen);
	            int prop_namlen   = (int) *(p+5+object_namlen);
	            uint8 *prop_name  =  p+6+object_namlen;
	            uint8 *valdata    =  p+6+object_namlen+prop_namlen;
	            int result = nw_write_prop_value( object_type,
	                         object_name, object_namlen,
	                         segment_nr, erase_segment,
	                         prop_name, prop_namlen,
	                         valdata);
	            if (result) completition = (uint8) -result;
	          } break;

     case 0x40:  {  /* change object password  */
                    uint8    *p  =  rdata;
	            NETOBJ   obj;
                    int      result;
	            obj.type     =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
	            if (0 == (result = find_obj_id(&obj, 0))) {
                      ;;
                    }
	            if (result < 0) completition = (uint8) -result;
                    XDPRINTF((1, 0, "TODO: Change Obj PW from OBJECT='%s', result=%d",
                       obj.name, result));
                    completition=0xff;
	         } break;

     case 0x41 :  {  /* add Bindery Object to Set */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+=2);
	            uint8 *object_name=  ++p;
	            int prop_namlen   =  (int) *(p+=object_namlen);
	            uint8 *prop_name  =  ++p;
	            int member_type   =  GET_BE16(p+prop_namlen);
	            int member_namlen =  (int) *(p+=(prop_namlen+2));
	            uint8 *member_name=  ++p;
	            int result = nw_add_obj_to_set( object_type,
	                         object_name, object_namlen,
	                         prop_name, prop_namlen,
	                         member_type,
	                         member_name, member_namlen);
	            if (result) completition = (uint8) -result;
	          } break;

     case 0x42 :  {  /* delete Bindery Object from Set */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+=2);
	            uint8 *object_name=  ++p;
	            int prop_namlen   =  (int) *(p+=object_namlen);
	            uint8 *prop_name  =  ++p;
	            int member_type   =  GET_BE16(p+prop_namlen);
	            int member_namlen =  (int) *(p+=(prop_namlen+2));
	            uint8 *member_name=  ++p;
	            int result = nw_delete_obj_from_set( object_type,
	                         object_name, object_namlen,
	                         prop_name, prop_namlen,
	                         member_type,
	                         member_name, member_namlen);
	            if (result) completition = (uint8) -result;
	          } break;

     case 0x43 :  {  /* is Bindery Object in Set */
	            uint8  *p         =  rdata;
	            int object_type   =  GET_BE16(p);
	            int object_namlen =  (int) *(p+=2);
	            uint8 *object_name=  ++p;
	            int prop_namlen   =  (int) *(p+=object_namlen);
	            uint8 *prop_name  =  ++p;
	            int member_type   =  GET_BE16(p+prop_namlen);
	            int member_namlen =  (int) *(p+=(prop_namlen+2));
	            uint8 *member_name=  ++p;
	            int result = nw_is_obj_in_set( object_type,
	                         object_name, object_namlen,
	                         prop_name, prop_namlen,
	                         member_type,
	                         member_name, member_namlen);
	            if (result) completition = (uint8) -result;
	          } break;


     case 0x44 :  { /* CLOSE BINDERY */
	            ;
	          } break;

     case 0x45 :  { /* OPEN BINDERY */
	            ;
	          } break;


     case 0x46 :  {   /* GET BINDERY ACCES LEVEL */
#if 0
	            struct XDATA {
	              uint8 acces_level;
	              uint8 object_id[4];
	            } *xdata = (struct XDATA*) responsedata;
#else
	            uint8    *xdata = responsedata;
#endif

	            NETOBJ    obj;
	            obj.id    = c->object_id;
                    if (0 != obj.id) {
	              int result = nw_get_obj(&obj);
	              if (!result) {
	                *xdata  = obj.security;
	                U32_TO_BE32(obj.id, (xdata+1));
                        XDPRINTF((2,0, "ACCESS LEVEL:=0x%x, obj=0x%lx",
                                  (int) obj.security, obj.id));
	                data_len = 5;
	              } else completition = (uint8)-result;
                    } else {
                      *xdata = 0;
                      memset(xdata+1, 0xff, 4);
                      data_len = 5;
                    }
	          }
	          break;


     case 0x47 :  { /* SCAN BINDERY OBJECT TRUSTEE PATH */
                    /* TODO !!! */
                    completition = (uint8)0xff;
   	          }
                  break;

     case 0x48 :  { /* GET BINDERY ACCES LEVEL from OBJECT ??? */
	            struct XDATA {
	              uint8 acces_level;
	            } *xdata = (struct XDATA*) responsedata;
	            NETOBJ    obj;
	            int       result;
	            obj.id =  GET_BE32(rdata);
	            result =  nw_get_obj(&obj);
	            if (!result) {
	              xdata->acces_level = obj.security;
	              data_len = sizeof(struct XDATA);
	            } else completition = (uint8)-result;
	          }
	          break;

     case 0x49 :  { /* IS CALLING STATION A MANAGER */
	            NETOBJ    obj;
	            int       result;
	            obj.id =  GET_BE32(rdata);
                    /* TODO !! */
                    completition = 0;  /* here allways Manager  */
                    /* not manager, then completition = 0xff */
   	     	  }
                  break;

     case 0x4a :  { /* keyed verify password  */
                    uint8    *p  =  rdata+sizeof(c->crypt_key);
	            NETOBJ   obj;
                    int      result;
	            obj.type     =  GET_BE16(p);
	            strmaxcpy((char*)obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
	            if (0 == (result = find_obj_id(&obj, 0)))
                      result=nw_test_passwd(obj.id, c->crypt_key, rdata);
	            if (result < 0) completition = (uint8) -result;
                    XDPRINTF((2,0, "Keyed Verify PW from OBJECT='%s', result=%d",
                       obj.name, result));
                  }
                  break;
#if 0
     case 0x4b :  { /* keyed change pasword  */
                    uint8  *p     =  rdata+sizeof(c->crypt_key);
	            NETOBJ obj;
                    int    result;
	            obj.type =  GET_BE16(p);
                    p+=2;
	            strmaxcpy((char*)obj.name, (char*)(p+1), *p);
                    upstr(obj.name);
                    p += (*p+1);  /* here is now password-type ?? 0x60,0x66 */

	            if (0 == (result = find_obj_id(&obj, 0)))
                      /*
	              result=nw_test_passwd(obj.id, c->crypt_key, rdata);
                    if (result > -1)
                      */
	              result=nw_set_enpasswd(obj.id, p+1);

	            if (result< 0) completition = (uint8) -result;
                    XDPRINTF((1, 0, "Keyed Change PW from OBJECT='%s', result=0x%x",
                      obj.name, result));
                  }
                  break;
#endif

     case 0x4c :  { /* List Relations of an Object  */
                   XDPRINTF((1, 0, "TODO:List Relations of an Object"));
                   completition=0xfb;
                  } break;

     case 0x64 :  {   /* Create Queue */
                   XDPRINTF((1, 0, "TODO:Create QUEUE ??"));
	          } break;

     case 0x66 :  {   /* Read Queue Current Status */
	           /*  !!!!!! TO DO */
	           NETOBJ    obj;
	           obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO:READ QUEUE STATUS von Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
	          }break;

     case 0x6B :  {   /* Get Queue Job List, old */
	           /*  !!!!!! TO DO */
	           NETOBJ    obj;
	           obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO:GET QUEUE JOB LIST von Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
	          }break;

     case 0x6C :  {   /* Get Queue Job Entry */
	           /*  !!!!!! TODO */
	           NETOBJ    obj;
	           obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO: GET QUEUE JOB ENTRY von Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
	          }break;

     case 0x68:     /* creat queue job and file old */
     case 0x79:   { /* creat queue job and file */
                    uint32 q_id      = GET_BE32(rdata);
                    uint8  *dir_name = rdata+4+280+1;
                    int result       = nw_get_q_dirname(q_id, dir_name);
                    if (result > -1) {
                      *(dir_name-1)  = result;
                      in_len         = 295 + result;
                      return(-1);   /* nwconn must do the rest !!!!! */
                    } else completition = (uint8) -result;
                  }
                  break;


     case 0x69:      /* close file and start queue old ?? */
     case 0x7f:   {  /* close file and start queue */
                    uint32 q_id      = GET_BE32(rdata);
                    uint8  *prc      = rdata+4+4+1;
                    int result       = nw_get_q_prcommand(q_id, prc);
                    if (result > -1) {
                      *(prc-1)       = result;
                      in_len         = 19 + result;
                      return(-1);   /* nwconn must do the rest !!!!! */
                    } else completition = (uint8) -result;
                  }
                  break;

     case 0xc8 :  { /* CHECK CONSOLE PRIVILEGES */
                   XDPRINTF((1, 0, "TODO: CHECK CONSOLE PRIV"));
	           /*  !!!!!! TODO completition=0xc6 (no rights) */
	          } break;

     case 0xc9 :  { /* GET FILE SERVER DESCRIPTION STRINGs */
	           char *company      = "Mars :-)";
	           char *revision     = "Version %d.%d";
	           char *revision_date= "24-Dec-95";
	           char *copyright    = "(C)opyright Martin Stover";
	           int  k=strlen(company)+1;

	           memset(responsedata, 0, 512);
	           strcpy(responsedata,   company);
                   k += (1+sprintf(responsedata+k, revision,
                             _VERS_H_, _VERS_L_ ));
	           strcpy(responsedata+k, revision_date);
	           k += (strlen(revision_date)+1);
	           strcpy(responsedata+k, copyright);
	           k += (strlen(copyright)+1);
	           data_len = k;
	          } break;

     case 0xcd :  { /* GET FILE SERVER LOGIN STATUS  */
	            struct XDATA {
                      uint8 login_allowed; /* 0 NO , 1 YES */
	            } *xdata = (struct XDATA*) responsedata;
                    xdata->login_allowed = 1;
	            data_len 		 = 1;
                  }
                  break;

     case 0xd1 :  /* Send Console Broadcast (old) */
                  {
                    uint8      *p = rdata;
                    int anz_conns = (int) *p++;
                    uint8    *co  = p;
                    int msglen    = (int) *(p+anz_conns);
                    char  *msg    = p+anz_conns+1;
                    int k = -1;
                    if (anz_conns) {
                      while (++k < anz_conns) {
                        int conn= (int) *co++;
                        if (conn == ncprequest->connection) {
                          strmaxcpy(c->message, msg, min(58, msglen));
                          connect_status = 0x40;  /* don't know why */
                        } else if (conn && --conn < anz_connect) {
                          CONNECTION *cc= &(connections[conn]);
                          if (cc->object_id) {  /* if logged */
                            strmaxcpy(cc->message, msg, min(58, msglen));
      			    nwserv_handle_msg(conn+1);
                          }
                        }
                      }
                    } else {
                      strmaxcpy(c->message, msg, min(58, msglen));
                      connect_status = 0x40;  /* don't know why */
                    }
                  }
                  break;
#if 0
     case 0xfd :  /* Send Console Broadcast (new) */
                  return(-1); /* nicht erkannt */
                  break;
#endif

     case 0xd3 :  { /* down File Server */
                    if (c->object_id == 1) { /* only SUPERVISOR */
                       /* inform nwserv */
                       nwserv_down_server();
                    } else completition = 0xff;
                  }
                  break;

       default : return(-1); /* not known here */
    }  /* switch */
  } else return(-1); /* not kwown here */

  U16_TO_BE16(0x3333,           ncpresponse->type);
  ncpresponse->sequence       = ncprequest->sequence;
  ncpresponse->connection     = ncprequest->connection;
  ncpresponse->reserved       = 0;
  ncpresponse->completition   = completition;
  ncpresponse->connect_status = connect_status;
  data_len=write(c->fd, (char*)ncpresponse,
                         sizeof(NCPRESPONSE) + data_len);
  XDPRINTF((2, 0, "0x%x 0x%x compl:0x%x, write to %d, anz = %d",
      func, (int)ufunc, (int) completition, c->fd, data_len));
  return(0);  /* ok */
}

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
  ncpresponse->reserved       = 0;
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

static void sig_child(int isig)
{
  int k=-1;
  int status;
  int pid=wait(&status);
  if (pid > -1) {
    while (++k < anz_connect) {
      CONNECTION *c = &connections[k];
      if (c->pid == pid) {
        clear_connection(k+1);
        break;
      }
    }
  }
  signal(SIGCHLD, sig_child);
}

static void close_all(void)
{
  int k=0;
  while (k++ < anz_connect) clear_connection(k);
  if (ncp_fd > -1) {
    t_unbind(ncp_fd);
    t_close(ncp_fd);
    XDPRINTF((2,0, "LEAVE ncpserv"));
    ncp_fd = -1;
  }
}

static void sig_quit(int isig)
{
  close_all();
  exit(0);
}


static void set_sig(void)
{
  signal(SIGTERM,  sig_quit);
  signal(SIGQUIT,  sig_quit);
  signal(SIGINT,   SIG_IGN);
  signal(SIGPIPE,  SIG_IGN);
  signal(SIGCHLD,  sig_child);
  signal(SIGHUP,   SIG_IGN);
}

static void handle_bind_calls(uint8 *p)
{
  int func = (int) *p;
  p       += 2;
  switch (func) {
     case  0x01 :
       {  /* insert NET_ADDRESS */
         NETOBJ obj;
         obj.type = GET_BE16(p);
         p += 2;
         strmaxcpy(obj.name, p+1, *p);
         p += (*p+1);
         nw_new_create_prop(0, obj.name, obj.type, O_FL_DYNA, 0x40,
                           "NET_ADDRESS", P_FL_DYNA|P_FL_ITEM,  0x40,
                           (char *)p, sizeof(ipxAddr_t));
       }
       break;

     case  0x02 :
       {  /* delete complete Object */
         NETOBJ obj;
         obj.type = GET_BE16(p);
         p += 2;
         strmaxcpy(obj.name, p+1, *p);
         nw_delete_obj(&obj); /* also deletes all properties */
       }
       break;

    default : break;
  }
}

static int handle_ctrl(void)
/* reads stdin pipe */
{
  int   what;
  int   conn;
  int   result   = 0;
  int   data_len = read(0, (char*)&what, sizeof(what));
  if (data_len  == sizeof(what)) {
    XDPRINTF((2, 0, "GOT CTRL what=0x%x", what));
    switch (what) {
      case 0x5555 : /* clear_connection */
        data_len = read(0, (char*)&conn, sizeof(conn));
        if (sizeof(int) == data_len) clear_connection(conn);
        break;

      case 0x3333 : /* 'bindery' calls */
        if (sizeof(conn) == read(0, (char*)&conn, sizeof(conn))) {
          uint8 *buff = xmalloc(conn+10);
          XDPRINTF((2,0, "0x3333 len=%d", conn));
          if (conn == read(0, (char*)buff, conn))
             handle_bind_calls(buff);
          else
            XDPRINTF((1, 1, "0x3333 protokoll error:len=%d", conn));
          xfree(buff);
        }
        break;

      case 0xeeee:
        get_ini_debug(NCPSERV);
        nw_fill_standard(NULL, NULL);
        break;

      case 0xffff : /* server down */
        data_len = read(0, (char*)&conn, sizeof(conn));
        if (sizeof(int) == data_len && conn == what)
           sent_down_message();
      break;

      default : break;
    } /* switch */
    result++;
  } else XDPRINTF((2, 0, "GOT CTRL size=%d", data_len));
  return(result);
}

int main(int argc, char *argv[])
{
  int     result;
  int     type;
  if (argc != 3) {
    fprintf(stderr, "usage ncpserv address nwname\n");
    exit(1);
  }
  init_tools(NCPSERV);
  strncpy(my_nwname, argv[1], 48);
  my_nwname[47] = '\0';
  adr_to_ipx_addr(&my_addr, argv[2]);
  nw_init_dbm(my_nwname, &my_addr);
#ifdef LINUX
  set_emu_tli();
#endif
  if (open_ncp_socket()) exit(1);
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

  polls[0].events  = polls[1].events = POLLIN|POLLPRI;
  polls[0].revents = polls[1].revents= 0;
  polls[0].fd      = ncp_fd;
  polls[1].fd      = 0;        /* stdin */
  while (1) {
    int anz_poll = poll(polls, 2, 60000);
    time(&akttime);
    if (anz_poll > 0) { /* i have to work */
      struct pollfd *p = &polls[0];
      int    j = -1;
      while (++j < 2) {

        if (p->revents){
          if (!j) {  /* ncp-socket */
            XDPRINTF((99,0, "POLL revents=%d", p->revents));
            if (p->revents & ~POLLIN)
              errorp(0, "STREAM error", "revents=0x%x", p->revents );
            else {
              if ((result = t_rcvudata(ncp_fd, &ud, &rcv_flags)) > -1){
                in_len = ud.udata.len;
                XDPRINTF((10, 0, "NCPSERV-LOOP von %s", visable_ipx_adr(&from_addr)));
                if ((type = GET_BE16(ncprequest->type)) == 0x2222 || type == 0x5555) {
                  int connection = (int)ncprequest->connection;
                  XDPRINTF((10,0, "GOT 0x%x in NCPSERV connection=%d", type, connection));
                  if ( connection > 0 && connection <= anz_connect) {
	            CONNECTION *c = &(connections[connection-1]);
                    if (!memcmp(&from_addr, &(c->client_adr), sizeof(ipxAddr_t))) {
                      if (c->fd > -1){
                        if (type == 0x2222) {
                          int sent_here  = 1;
                          int func       = ncprequest->function;
                          int diff_time  = akttime - c->last_access;
                          c->last_access = akttime;

                          if (diff_time > 50) /* after max. 50 seconds */
                             nwserv_reset_wdog(connection);
                             /* tell the wdog there's no need to look */
                          if (ncprequest->sequence == c->sequence
                              && !c->retry++) {
                            /* perhaps nwconn is busy  */
                            ncp_response(0x9999, ncprequest->sequence,
			                         connection, 0, 0x0, 0, 0);
                            XDPRINTF((2, 0, "Send Request being serviced to connection:%d", connection));
                            continue;
                          }

                          switch (func) {
                            case 0x15 : /* Messages */
                            case 0x17 : /* File Server Environment */
                                        sent_here = handle_fxx(c, in_len, func);
                                        break;
                            default :   break;
                          } /* switch */

  	                  if (sent_here) {
	                    int anz=write(c->fd, (char*)ncprequest, in_len);
	                    XDPRINTF((10,0, "write to %d, anz = %d", c->fd, anz));
                            if (func == 0x19) {  /* logout */
                              c->object_id  = 0; /* not LOGGED  */
                            }
	                  }
                          c->sequence    = ncprequest->sequence; /* save last sequence */
                          c->retry       = 0;
	                  continue;
                        } else {  /* 0x5555, conection beenden */
                          if ( (uint8) (c->sequence+1) == (uint8) ncprequest->sequence) {
                            clear_connection(ncprequest->connection);
	                    ncp_response(0x3333,
	                                 ncprequest->sequence,
	                                 connection,
	             	                 1, 0x0, 0, 0);
	                    continue;
                          }
                        }
                      }
                      XDPRINTF((10,0, "c->fd = %d", c->fd));
                    }
	          }
                  /* here someting is wrong */
                  XDPRINTF((1,0, "GOT 0x%x connection=%d of %d conns not OK",
                      type, ncprequest->connection, anz_connect));

	          ncp_response(0x3333, ncprequest->sequence,
			               ncprequest->connection,
			               0, 0xff, 0x08, 0);

                } else if (type == 0x1111) {
	          /* GIVE CONNECTION Nr connection */
	          int connection = (server_goes_down) ? 0 : find_get_conn_nr(&from_addr);
                  XDPRINTF((2, 0, "GIVE CONNECTION NR=%d", connection));
	          if (connection) {
	            CONNECTION *c = &(connections[connection-1]);
	            int anz;
                    c->message[0] = '\0';
	            c->object_id  = 0; /* firsttime set 0 for NOT LOGGED */
                    c->sequence   = 0;
	            anz=write(c->fd, (char*)ncprequest, in_len);
	            XDPRINTF((10, 0, "write to oldconn %d, anz = %d", c->fd, anz));
	          } else  /* no free connection */
	            ncp_response(0x3333, 0, 0, 0, 0xf9, 0, 0);
                } else {
	          int connection     = (int)ncprequest->connection;
	          int sequence       = (int)ncprequest->sequence;
	          XDPRINTF((1,0, "Got UNKNOWN TYPE: 0x%x", type));
	          ncp_response(0x3333, sequence, connection,
	                       1, 0xfb, 0, 0);
                }
              }
            }
          } else if (p->fd==0)  {  /* fd_ncpserv_in */
            XDPRINTF((2,0,"POLL %d, fh=%d", p->revents, p->fd));
            if (p->revents & ~POLLIN)
              errorp(0, "STREAM error", "revents=0x%x", p->revents );
            else handle_ctrl();
          }
          if (! --anz_poll) break;
        } /* if */
        p++;
      } /* while */
    } else {
      XDPRINTF((3,0,"POLLING ..."));
    }
  }
  close_all();
  return(0);
}



