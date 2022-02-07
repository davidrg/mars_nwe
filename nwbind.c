/* nwbind.c */
#define REVISION_DATE "12-Jul-96"
/* NCP Bindery SUB-SERVER */
/* authentification and some message handling */

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
#include "nwdbm.h"
#include "unxlog.h"

/* next should be '1', is for testing only */
#define USE_PERMANENT_OUT_SOCKET  1

static  char       my_nwname[50];
static  ipxAddr_t  my_addr;
static  time_t     akttime;
static  int        ipx_out_fd=-1;

static  int        ncp_fd      = 0; /* stdin */
static  struct     t_unitdata ud;
static  uint8      ipx_in_data[IPX_MAX_DATA];
static  uint8      ipx_pack_typ = 17;
static  int        rcv_flags    =  0;
static  ipxAddr_t  from_addr;    /* actual calling address  */
static  NCPREQUEST *ncprequest  = (NCPREQUEST*)&ipx_in_data;
static  int         server_goes_down=0;

static void write_to_nwserv(int what, int connection, int mode,
                                char *data, int size)
{
  switch (what) {
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

#define nwserv_handle_msg(connection) \
   write_to_nwserv(0x6666, (connection), 0, NULL, 0)

#define nwserv_down_server() \
   write_to_nwserv(0xffff, 0, 0, NULL, 0)

static int open_ipx_sockets(void)
{
  struct t_bind bind;
  bind.addr.len    = sizeof(ipxAddr_t);
  bind.addr.maxlen = sizeof(ipxAddr_t);
  bind.addr.buf    = (char*)&my_addr;
  bind.qlen        = 0; /* immer */
#if 0
  ncp_fd=t_open("/dev/ipx", O_RDWR, NULL);
  if (ncp_fd < 0) {
    t_error("t_open !Ok");
    return(-1);
  }
  U16_TO_BE16(SOCK_BINDERY, my_addr.sock);
  if (t_bind(ncp_fd, &bind, &bind) < 0){
    t_error("t_bind in open_ipx_sockets !OK");
    close(ncp_fd);
    return(-1);
  }
#endif

#if USE_PERMANENT_OUT_SOCKET
  ipx_out_fd=t_open("/dev/ipx", O_RDWR, NULL);
  if (ipx_out_fd > -1) {
    U16_TO_BE16(SOCK_AUTO, my_addr.sock); /* dynamic socket */
    if (t_bind(ipx_out_fd, &bind, &bind) < 0) {
      if (nw_debug) t_error("2. t_bind in open_ipx_sockets !OK");
      t_close(ipx_out_fd);
      ipx_out_fd = -1;
    }
  } else {
    if (nw_debug) t_error("2. t_open !Ok");
  }

#endif
  return(0);
}

typedef struct {
   ipxAddr_t  client_adr;      /* address remote client */
   uint32     object_id;       /* logged object         */
                               /* 0 = not logged in     */
   uint8      crypt_key[8];    /* password generation   */
   time_t     t_login;         /* login time            */
   uint8      message[60];     /* saved BCastmessage    */
   int        active;          /* 0=closed, 1= active   */
   int        send_to_sock;    /* this is the receiving sock */
   int        pid_nwconn;      /* pid of user process nwconn */
} CONNECTION;

static CONNECTION connections[MAX_CONNECTIONS];
static CONNECTION *act_c=(CONNECTION*)NULL;
static int        act_connection;
static int        internal_act=0;

int b_acc(uint32 obj_id, int security, int forwrite)
{
  /* security levels
   * 0 = anyone have access.
   * 1 = all logged  have access
   * 2 = object logged have access
   * 3 = only supervisor has access
   * 4 = only internal access.
   */
  char *acc_what=NULL;
  char *acc_typ=NULL;
  int  errcode =0;
  XDPRINTF((5,0, "b_acc for id=0x%lx, security=0x%x, forwrite=0x%x",
          obj_id, security, forwrite));
  if (internal_act || !act_c) return(0); /* allways full access to internal routines */
  if (forwrite & 0xf) security >>= 4; /* writesecurity */
  security &= 0xf;
  if (!security) return(0);     /* rights for all */
  else if (security == 1) {
    if (act_c->object_id > 0) return(0);  /* rights for all logged */
  } else if (security == 2) {
    if (  act_c->object_id == obj_id
       || act_c->object_id == 1 ) return(0); /* rights for the user */
  } else if (security == 3 && act_c->object_id == 1) return(0);

  switch (forwrite&0xf) {
    case 0 : acc_what = "read";   break;
    case 1 : acc_what = "write";  break;
    case 2 : acc_what = "creat";  break;
    case 3 : acc_what = "delete"; break;
    case 4 : acc_what = "rename"; break;
    case 5 : acc_what = "change security"; break;
    default : acc_what = "?" ; break;
  }

  switch ( (forwrite >> 4) & 0xf) {
    case 0  : acc_typ  = "obj" ;   break;
    case 1  : acc_typ  = "prop";   break;
    default : acc_typ  = "?";      break;
  }

  switch (forwrite) {
    case 0x00 : errcode = -0xf2; break;
    case 0x01 : errcode = -0xf8; break;  /* should be changed */
    case 0x02 : errcode = -0xf5; break;
    case 0x03 : errcode = -0xf4; break;
    case 0x04 : errcode = -0xf3; break;

    case 0x10 : errcode = -0xf9; break;
    case 0x11 : errcode = -0xf8; break;
    case 0x12 : errcode = -0xf7; break;
    case 0x13 : errcode = -0xf6; break;

    default   : errcode = -0xff; break;
  }
  XDPRINTF((1, 0, "b_acc no rights for 0x%x to %s %s",
                  act_c->object_id, acc_what, acc_typ));
  return(errcode);
}

static void sent_down_message(void)
{
  int k = -1;
  server_goes_down++;
  while (++k < MAX_CONNECTIONS) {
    CONNECTION *cn=&connections[k];
    if (cn->active) {
      strmaxcpy(cn->message, "SERVER IS GOING DOWN", 58);
      nwserv_handle_msg(k+1);
    }
  } /* while */
}

static void open_clear_connection(int conn, int activate, uint8 *addr)
{
  if (conn > 0 && --conn < MAX_CONNECTIONS) {
    CONNECTION *c = &connections[conn];
    c->active     = activate;
    c->message[0] = '\0';
    c->t_login    = 0;
    if (activate && addr) {
      memcpy(&(c->client_adr), addr, sizeof(ipxAddr_t));
      c->send_to_sock = GET_BE16(addr+sizeof(ipxAddr_t));
      c->pid_nwconn   = GET_BE32(addr+sizeof(ipxAddr_t)+sizeof(uint16));
    } else {
      if (c->object_id)
        write_utmp(0, conn+1, c->pid_nwconn,  &(c->client_adr), NULL);
    }
    c->object_id  = 0;
  }
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

static void handle_fxx(int gelen, int func)
{
  IPX_DATA     ipxoutdata;
  NCPRESPONSE  *ncpresponse  = (NCPRESPONSE*)&ipxoutdata;
  uint8        *responsedata = ((uint8*)&ipxoutdata)+sizeof(NCPRESPONSE);
  uint8        *requestdata  = ((uint8*)ncprequest)+sizeof(NCPREQUEST);
#if 0
  uint8        len           = *(requestdata+1);
#endif

  uint8        ufunc         = *(requestdata+2);
  uint8        *rdata        = requestdata+3;
  uint8        completition  = 0;
  uint8        connect_status= 0;
  int          data_len      = 0;

  if (nw_debug > 1){
    int j = gelen - sizeof(NCPREQUEST);
    if (nw_debug){
      if (func == 0x19) ufunc=0;
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
        uint8  *conns   = rdata+1;              /* connectionslist */
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
          if (connr > 0 && --connr < MAX_CONNECTIONS
            && ((cn = &connections[connr]))->active ) {
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
        *responsedata = (uint8) strmaxcpy(responsedata+1, act_c->message, 58);
        act_c->message[0] = '\0';
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
      default :   completition=0xfb; /* not handled  */
    } /* switch */
  } else if (0x17 == func) { /* Fileserver Enviro */
    switch (ufunc) {
     case 0x01 :  { /* Change User Password OLD */
                     completition=0xff;
                  }
                  break;

#if FUNC_17_02_IS_DEBUG
     case 0x02 :  { /* I hope this is call isn't used    */
                    /* now missused as a debug switch :) */

                     struct XDATA {
                       uint8  nw_debug;   /* old level */
                     } *xdata = (struct XDATA*) responsedata;

                     if (*rdata == NWBIND) {
                       xdata->nw_debug = (uint8) nw_debug;
                       nw_debug = (int) *(rdata+1);
                       data_len = 1;
                     } else completition=0xff;
                  }
                  break;
#endif

     case 0x0c :  { /* Verify Serialization */
                     completition=0xff;
                  }
                  break;

     case 0x0e :  { /* Get Disk Utilization */
                       completition=0xff;
                    }
                    break;
#if 0
     case 0x10 :  set file information. handled in nwconn.
#endif

     case 0x11 :  { /* Get FileServer Info */
                     struct XDATA {
                       uint8 servername[48];
                       uint8 version;    /* 2  or   3 */
                       uint8 subversion; /* 15 or  11 */
                       uint8 maxconnections[2];
                       uint8 connection_in_use[2];
                       uint8 max_volumes[2];
                       uint8 os_revision;  /* 0 */
                       uint8 sft_level;    /* 2 */
                       uint8 tts_level;    /* 1 */
                       uint8 peak_connection[2];
                       uint8 accounting_version;  /* 1 */
                       uint8 vap_version;         /* 1 */
                       uint8 queuing_version;     /* 1 */
                       uint8 print_server_version;  /* 0 */
                       uint8 virtual_console_version; /* 1 */
                       uint8 security_level;          /* 1 */
                       uint8 internet_bridge_version;  /* 1 */
                       uint8 reserved[60];
                     } *xdata = (struct XDATA*) responsedata;
                     int k, i, h;
                     memset(xdata, 0, sizeof(struct XDATA));
                     strcpy(xdata->servername, my_nwname);
                     if (!tells_server_version) {
                       xdata->version    =  2;
                       xdata->subversion = 15;
                     } else {
                       xdata->version    =  3;
                       xdata->subversion = (tells_server_version == 2)
                                              ? 12
                                              : 11;
                     }

                     i=0;
                     h=0;
                     for (k=0; k < MAX_CONNECTIONS; k++) {
                       if (connections[k].active) {
                         i++;
                         h = k+1;
                       }
                     }
                     U16_TO_BE16(i, xdata->connection_in_use);
                     U16_TO_BE16(MAX_CONNECTIONS, xdata->maxconnections);
                     U16_TO_BE16(h,               xdata->peak_connection);
                     U16_TO_BE16(MAX_NW_VOLS,     xdata->max_volumes);
#ifdef _MAR_TESTS_1
                     xdata->security_level=1;
                     xdata->sft_level=2;
                     xdata->tts_level=1;

                     xdata->accounting_version=1;
                     xdata->vap_version=1;
                     xdata->queuing_version=1;

                     xdata->virtual_console_version=1;
                     xdata->security_level=1;
                     xdata->internet_bridge_version=1;
#endif
                     data_len = sizeof(struct XDATA);
                   }
                   break;

     case 0x12 :  { /* Get Network Serial Number */
                     struct XDATA {
                       uint8 serial_number[4];
                       uint8 appl_number[2];
                     } *xdata = (struct XDATA*) responsedata;
                     /* serial-number 4-Byte */
                     U32_TO_BE32(NETWORK_SERIAL_NMBR, xdata->serial_number);
                     /* applikation-number 2-Byte */
                     U16_TO_BE16(NETWORK_APPL_NMBR,  xdata->appl_number);
                     data_len = sizeof(struct XDATA);
                    }
                  break;

     case 0x13 :  { /* Get Connection Internet Address */
                    int conn  = (int)*(rdata);  /* Connection Nr */
                    if (conn && --conn < MAX_CONNECTIONS
                      && connections[conn].active ) {
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
                    xstrmaxcpy(obj.name, p+3, (int) *(p+2));
                    upstr(obj.name);
                    xstrmaxcpy(password, p1+1, (int) *p1);
                    XDPRINTF((10, 0, "LOGIN unencrypted PW NAME='%s', PASSW='%s'",
                             obj.name, password));
                    if (0 == (result = find_obj_id(&obj, 0))) {
                      if (password_scheme & PW_SCHEME_LOGIN) {
#if 0
                        if (obj.id == 1) {
                          result=-0xff; /* SUPERVISOR ever encryted !! */
                          XDPRINTF((1, 0, "Supervisor tried unencrypted LOGIN"));
                        } else
#endif
                        {
                          internal_act = 1;
                          result=nw_test_unenpasswd(obj.id, password);
                          internal_act = 0;
                        }
                      } else {
                        XDPRINTF((1, 0, "unencryted logins are not enabled"));
                        result=-0xff;
                      }
                    }
                    if (!result) {
                      uint8 pw_name[40];
                      act_c->object_id = obj.id;     /* actuell Object ID  */
                      act_c->t_login   = akttime;    /* u. login Time      */
                      get_guid((int*) responsedata, (int*)(responsedata+sizeof(int)), obj.id, pw_name);
                      result = get_home_dir(responsedata + 2*sizeof(int)+1, obj.id);
                      *(responsedata+ 2 * sizeof(int)) = (uint8) result;
                      data_len = 2 * sizeof(int) + 1 + (int) *(responsedata+2* sizeof(int));
                      write_utmp(1, act_connection, act_c->pid_nwconn, &(act_c->client_adr), pw_name);
                    } else completition = (uint8) -result;
                  } break;

     case 0x15 :  { /* Get Object Connection List */
                    uint8  *p           =  rdata;
                    int    result;
                    NETOBJ obj;
                    obj.type            =  GET_BE16(p);
                    p+=2;
                    strmaxcpy((char*)obj.name,  (char*)(p+1), (int) *(p));
                    upstr(obj.name);
                    result = find_obj_id(&obj, 0);
                    if (!result){
                      int k=-1;
                      int anz  = 0;
                      p = responsedata+1;
                      while (++k < MAX_CONNECTIONS && anz < 255) {
                        CONNECTION *cn= &connections[k];
                        if (cn->active && cn->object_id == obj.id) {
                          *p++=(uint8)k+1;
                          anz++;
                        }
                      } /* while */
                      *responsedata = anz;
                      data_len = 1 + anz;
                    } else completition=(uint8)-result;
                  }
                  break;

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
                    if (conn && conn <= MAX_CONNECTIONS
                             && connections[conn-1].active ) {
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
                    int    k   = sizeof(act_c->crypt_key);
                    uint8 *p   = act_c->crypt_key;
                    uint8 *pp  = responsedata;
                    data_len   = k;
                    while (k--) *pp++ = *p++ = (uint8) rand();

                    /* if all here are same (1 or 2) then the resulting key is */
                    /* 00000000  */
                    if (password_scheme & PW_SCHEME_GET_KEY_FAIL)
                       completition=0xfb;
                  }
                  break;

     case 0x18 :  { /* crypt_keyed LOGIN */
                    uint8 *p     =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ obj;
                    int    result;
                    obj.type     =  GET_BE16(p);
                    obj.id       =  0;
                    xstrmaxcpy(obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
                    XDPRINTF((2, 0, "LOGIN CRYPTED PW NAME='%s'",obj.name));
                    if (0 == (result = find_obj_id(&obj, 0))) {
                      internal_act = 1;
                      result=nw_test_passwd(obj.id, act_c->crypt_key, rdata);
                      internal_act = 0;
                    }
                    if (result > -1) {
                      uint8 pw_name[40];
                      act_c->object_id = obj.id;        /* actuell Object */
                      act_c->t_login   = akttime;       /* and login time */
                      get_guid((int*)responsedata, (int*)(responsedata+sizeof(int)), obj.id, pw_name);
                      result = get_home_dir(responsedata + 2*sizeof(int)+1, obj.id);
                      *(responsedata+ 2 * sizeof(int)) = (uint8) result;
                      data_len = 2 * sizeof(int) + 1 + (int) *(responsedata+2* sizeof(int));
                      write_utmp(1, act_connection, act_c->pid_nwconn,
                                    &(act_c->client_adr), pw_name);
                    } else {
#if 0
                      /* this is not ok */
                      if ((password_scheme & PW_SCHEME_LOGIN) &&
                         result == -0xff && obj.id != 1) /* not supervisor */
                        completition = 0xfb; /* We lie here, to force LOGIN */
                      else                   /* to use the old call         */
#endif
                        completition = (uint8) -result;
                    }
                    /* completition = 0xde means login time has expired */
                    /* completition = 0xdf means good login, but */
                    /* login time has expired 	      	     	 */
                    /* perhaps I will integrate it later         */
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
                    if (conn && --conn < MAX_CONNECTIONS){
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
                    if (1 == act_c->object_id) {
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
                    upstr(obj.name);
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
                    upstr(obj.name);
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
                    if (1 == act_c->object_id) {
                      uint8  *p           =  rdata;
                      NETOBJ obj;
                      obj.type            =  GET_BE16(p+1);
                      xstrmaxcpy(obj.name,  (char*)(p+4), (int) *(p+3));
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
                      data_len          = sizeof(struct XDATA);
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

     case 0x40:   {  /* change object password  */
                    if (password_scheme & PW_SCHEME_CHANGE_PW) {
                      uint8    *p    =  rdata;
                      uint8    oldpassword[50];
                      uint8    newpassword[50];
                      NETOBJ   obj;
                      int      result;
                      obj.type        =  GET_BE16(p);
                      p+=2;
                      xstrmaxcpy(obj.name,     p+1, (int) *p);
                      upstr(obj.name);
                      p +=   ((*p)+1);
                      xstrmaxcpy(oldpassword,  p+1, (int) *p);
                      p +=   ((*p)+1);
                      xstrmaxcpy(newpassword,  p+1, (int) *p);
                      if (0 == (result = find_obj_id(&obj, 0))) {
                        XDPRINTF((6, 0, "CHPW: OLD=`%s`, NEW=`%s`", oldpassword,
                                         newpassword));

                        internal_act = 1;
                        if (act_c->object_id == 1 ||
                           0 == (result=nw_test_unenpasswd(obj.id, oldpassword))){
                          if ( (act_c->object_id != 1)
                            || *newpassword
                            || !(password_scheme & PW_SCHEME_LOGIN))
                             result=nw_set_passwd(obj.id, newpassword, 0);
                          else result = -0xff;
                        }
                        internal_act = 0;
                      }
                      if (result < 0) completition = (uint8) -result;
                    } else {
                      XDPRINTF((1, 0, "Change object password unencryted not enabled"));
                      completition=0xff;
                    }
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
                    }
#endif
                    uint8    *xdata = responsedata;
                    if (!act_c->object_id) {
                      *xdata  = (uint8) 0;
                      memset(xdata+1, 0xff, 4);
                    } else {
                      *xdata  = (act_c->object_id == 1) ? (uint8) 0x33
                                                        : (uint8) 0x22;
                      U32_TO_BE32(act_c->object_id, (xdata+1));
                    }
                    data_len = 5;
                    XDPRINTF((2,0, "ACCESS LEVEL:=0x%x, obj=0x%lx",
                                  (int) *xdata, act_c->object_id));
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
                    completition = (act_c->object_id == 1) ? 0 : 0xff;
                     /* here only SU = Manager  */
                    /* not manager, then completition = 0xff */
                  }
                  break;

     case 0x4a :  { /* keyed verify password  */
                    uint8    *p  =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ   obj;
                    int      result;
                    obj.type     =  GET_BE16(p);
                    strmaxcpy((char*)obj.name, (char*)(p+3), *(p+2));
                    upstr(obj.name);
                    if (0 == (result = find_obj_id(&obj, 0)))  {
                      internal_act = 1;
                      result=nw_test_passwd(obj.id, act_c->crypt_key, rdata);
                      internal_act = 0;
                    }
                    if (result < 0) completition = (uint8) -result;
                    XDPRINTF((2,0, "Keyed Verify PW from OBJECT='%s', result=%d",
                       obj.name, result));
                  }
                  break;

     case 0x4b :  { /* keyed change pasword  */
                    uint8  *p     =  rdata+sizeof(act_c->crypt_key);
                    NETOBJ obj;
                    int    result;
                    obj.type =  GET_BE16(p);
                    p+=2;
                    strmaxcpy((char*)obj.name, (char*)(p+1), *p);
                    upstr(obj.name);

                    /* from Guntram Blohm  */
                    p += (*p+1); /* here is crypted password length */
                    if (0 == (result = find_obj_id(&obj, 0)))  {
		      internal_act=1;
		      result=nw_keychange_passwd(obj.id, act_c->crypt_key,
				rdata, (int)*p, p+1, act_c->object_id);
                      if (!result) test_ins_unx_user(obj.id);
		      internal_act = 0;
		    }

                    if (result< 0) completition = (uint8) -result;
                    XDPRINTF((2, 0, "Keyed Change PW from OBJECT='%s', result=0x%x",
                      obj.name, result));
                  }
                  break;

     case 0x4c :  { /* List Relations of an Object  */
                   XDPRINTF((1, 0, "TODO:List Relations of an Object"));
                   completition=0xfb;
                  } break;

     case 0x64 :  {   /* Create Queue */
                   XDPRINTF((1, 0, "TODO:Create QUEUE ??"));
                   completition=0xfb;
                  } break;

     case 0x65 :  {   /* Delete Queue */
                   XDPRINTF((1, 0, "TODO:Delete QUEUE ??"));
                   completition=0xfb;
                  } break;


     case 0x66 :   { /* Read Queue Current Status,old */
                   /*  !!!!!! TO DO */
                   NETOBJ    obj;
                   struct XDATA {
                      uint8 queue_id[4];
                      uint8 status;
                      uint8 entries;
                      uint8 servers;
                      uint8 data[1];  /* server_id + server_station list */
                   } *xdata = (struct XDATA*) responsedata;
                   obj.id =  GET_BE32(rdata);
                   memset(xdata, 0, sizeof(struct XDATA));
                   U32_TO_BE32(obj.id, xdata->queue_id);
                   data_len = sizeof(struct XDATA);
                   XDPRINTF((1, 0, "TODO:READ QUEUE STATUS,old of Q=0x%lx", obj.id));
                  }
                  break;

     case 0x6A :    /* Remove Job from Queue OLD */
     case 0x80 :  { /* Remove Job from Queue NEW */
                   NETOBJ      obj;
                   uint32 jobnr  = (ufunc == 0x6A)
                                      ? GET_BE16(rdata+4)
                                      : GET_BE32(rdata+4);
                   obj.id        = GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO:Remove Job=%ld from Queue Q=0x%lx", jobnr, obj.id));
                   completition=0xd5; /* no Queue Job */
                  }break;

     case 0x6B :  {   /* Get Queue Job List, old */
                   /*  !!!!!! TO DO */
                   NETOBJ    obj;
                   obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO:GET QUEUE JOB LIST,old of Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
                  }
                  break;

     case 0x6C :  {   /* Get Queue Job Entry */
                   /*  !!!!!! TODO */
                   NETOBJ    obj;
                   obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO: GET QUEUE JOB ENTRY of Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
                  }
                  break;

     case 0x68:     /* creat queue job and file old */
     case 0x79:   { /* creat queue job and file new */
                    uint32 q_id      = GET_BE32(rdata);
                    uint8  *dir_name = responsedata+1;
                    int result       = nw_get_q_dirname(q_id, dir_name);
                    if (result > -1) {
                      *(dir_name-1)  = result;
                      data_len       = result+1;
                    } else completition = (uint8) -result;
                  }
                  break;

     case 0x69:      /* close file and start queue old ?? */
     case 0x7f:   {  /* close file and start queue */
                    uint32 q_id      = GET_BE32(rdata);
                    uint8  *prc      = responsedata+1;
                    int result       = nw_get_q_prcommand(q_id, prc);
                    if (result > -1) {
                      *(prc-1)       = result;
                      data_len       = result+1;
                    } else completition = (uint8) -result;
                  }
                  break;


     case 0x7d :  { /* Read Queue Current Status, new */
                   NETOBJ    obj;
                   obj.id =  GET_BE32(rdata);
                   XDPRINTF((1, 0, "TODO:READ QUEUE STATUS NEW of Q=0x%lx", obj.id));
                   completition=0xd5; /* no Queue Job */
                  }break;

     case 0xc8 :  { /* CHECK CONSOLE PRIVILEGES */
                   XDPRINTF((1, 0, "TODO: CHECK CONSOLE PRIV"));
                   /*  !!!!!! TODO completition=0xc6 (no rights) */
                    if (act_c->object_id != 1) completition=0xc6; /* no rights */
                  } break;

     case 0xc9 :  { /* GET FILE SERVER DESCRIPTION STRINGs */
                   char *company       = "Mars :-)";
                   char *revision      = "Version %d.%d.pl%d";
                   char *revision_date = REVISION_DATE;
                   char *copyright     = "(C)opyright Martin Stover";
                   int  k=strlen(company)+1;
                   int  l;
                   memset(responsedata, 0, 512);
                   strcpy(responsedata,   company);

                   l = 1 + sprintf(responsedata+k, revision,
                                     _VERS_H_, _VERS_L_, _VERS_P_ );
#if 0
                   k+=l;
#else
                   /* BUG in LIB */
                   k += (1 + strlen(responsedata+k));
#endif
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
                    data_len             = 1;
                  }
                  break;

     case 0xd1 :  /* Send Console Broadcast (old) */
                  {
                    uint8      *p = rdata;
                    int anz_conns = (int) *p++;
                    uint8    *co  = p;
                    int msglen    = (int) *(p+anz_conns);
                    char  *msg    = (char*) p+anz_conns+1;
                    int k = -1;
                    if (anz_conns) {
                      while (++k < anz_conns) {
                        int conn= (int) *co++;
                        if (conn == ncprequest->connection) {
                          strmaxcpy(act_c->message, msg, min(58, msglen));
                          connect_status = 0x40;  /* don't know why */
                        } else if (conn && --conn < MAX_CONNECTIONS) {
                          CONNECTION *cc= &(connections[conn]);
                          if (cc->object_id) {  /* if logged */
                            strmaxcpy(cc->message, msg, min(58, msglen));
                              nwserv_handle_msg(conn+1);
                          }
                        }
                      }
                    } else {
                      strmaxcpy(act_c->message, msg, min(58, msglen));
                      connect_status = 0x40;  /* don't know why */
                    }
                  }
                  break;

     case 0xd3 :  { /* down File Server */
                    if (act_c->object_id == 1) { /* only SUPERVISOR */
                       /* inform nwserv */
                       nwserv_down_server();
                    } else completition = 0xff;
                  }
                  break;

#if 0
     case 0xfd :  /* Send Console Broadcast (new) */
                  return(-1); /* nicht erkannt */

                  break;
#endif
       default : completition = 0xfb; /* not known here */
    }  /* switch */
  } else if (func == 0x19) {  /* logout */
    write_utmp(0, act_connection, act_c->pid_nwconn, &(act_c->client_adr), NULL);
    act_c->object_id  = 0; /* not LOGIN  */
  } else completition = 0xfb;

  U16_TO_BE16(0x3333,           ncpresponse->type);
  ncpresponse->sequence       = ncprequest->sequence;
  ncpresponse->connection     = ncprequest->connection;
  ncpresponse->reserved       = 0;
  ncpresponse->completition   = completition;

  if (act_c->message[0]) connect_status |= 0x40;
  ncpresponse->connect_status = connect_status;
  data_len+=sizeof(NCPRESPONSE);
  U16_TO_BE16(act_c->send_to_sock, my_addr.sock);

  send_ipx_data(ipx_out_fd, 17, data_len, (char*)ncpresponse,
                 &my_addr, NULL);

  XDPRINTF((2, 0, "func=0x%x ufunc=0x%x compl:0x%x, written anz = %d",
             (int)func, (int)ufunc, (int) completition, data_len));
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
         nw_new_obj_prop(0, obj.name, obj.type, O_FL_DYNA, 0x40,
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

static void reinit_nwbind(void)
{
  get_ini_debug(NWBIND);
  (void)nw_fill_standard(NULL, NULL);
  sync_dbm();
}

static int xread(IPX_DATA *ipxd, int *offs, uint8 *data, int size)
{
  if (*offs == 0) memcpy(ipxd, &ipx_in_data, ud.udata.len);
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

static void handle_ctrl(void)
/* reads stdin pipe or packets from nwserv/ncpserv */
{
  IPX_DATA ipxd;
  int   what;
  int   conn;
  int   offs=0;
  int   data_len =  xread(&ipxd, &offs, (uint8*)&(what), sizeof(int));

  if   (data_len == sizeof(int)) {
    XDPRINTF((2, 0, "GOT CTRL what=0x%x, len=%d",
                     what, ipxd.owndata.d.size));
    switch (what) {

      case  0x2222 :  /* insert connection */
        if (sizeof (int) ==
          xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int))) {
          int size  = 0;
          if (sizeof (int) ==
             xread(&ipxd, &offs, (uint8*)&(size), sizeof(int))
             && size == sizeof(ipxAddr_t)
                      + sizeof(uint16) + sizeof(uint32) ) {
           uint8 buf[100];
           if (size == xread(&ipxd, &offs, buf, size))
             open_clear_connection(conn, 1, buf);
          }
        }
        break;

      case 0x3333 : /* special 'bindery' calls */
        if (sizeof (int) ==
            xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int))) {
          uint8 *buff = (uint8*)  xmalloc(conn+10);
          XDPRINTF((2,0, "0x3333 len=%d", conn));
          if (conn == xread(&ipxd, &offs, buff, conn))
             handle_bind_calls(buff);
          else
            XDPRINTF((1, 1, "0x3333 protokoll error:len=%d", conn));
          xfree(buff);
        }
        break;

      case 0x5555 : /* clear connection */
        if (sizeof (int) ==
            xread(&ipxd, &offs, (uint8*)&(conn), sizeof(int)))
          open_clear_connection(conn, 0, NULL);
        break;

      case 0xeeee:
        reinit_nwbind();
        break;

      case 0xffff : /* server down */
        data_len = xread(&ipxd, &offs, (char*)&conn, sizeof(int));
        if (sizeof(int) == data_len && conn == what)
           sent_down_message();
        break;

      default : break;
    } /* switch */
  } else {
    errorp(1, "handle_ctrl", "wrong data len=%d", data_len);
  }
}

static int got_sig=0;
static void sig_handler(int isig)
{
  got_sig=isig;
  signal(isig, sig_handler);
}

static void set_sig(void)
{
  signal(SIGQUIT,  sig_handler);
  signal(SIGHUP,   sig_handler);
  signal(SIGTERM,  SIG_IGN);
  signal(SIGINT,   SIG_IGN);
  signal(SIGPIPE,  SIG_IGN);
}

int main(int argc, char *argv[])
{
  int sock_nwbind;
  if (argc != 4) {
    fprintf(stderr, "usage nwbind nwname address nwbindsock\n");
    exit(1);
  }

  init_tools(NWBIND, 0);

  memset(connections, 0, sizeof(connections));

  strmaxcpy(my_nwname, argv[1], 47);
  adr_to_ipx_addr(&my_addr, argv[2]);

  sscanf(argv[3], "%x", &sock_nwbind);

  internal_act = 1;
  if (nw_init_dbm(my_nwname, &my_addr) <0) {
    errorp(1, "nw_init_dbm", NULL);
    exit(1);
  }
  internal_act = 0;

#ifdef LINUX
  set_emu_tli();
#endif

  if (open_ipx_sockets()) {
    errorp(1, "open_ipx_sockets", NULL);
    exit(1);
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

  while (got_sig != SIGQUIT) {
    act_c        = (CONNECTION*)NULL;
    if (t_rcvudata(ncp_fd, &ud, &rcv_flags) > -1){
      time(&akttime);
      XDPRINTF((10, 0, "NWBIND-LOOP from %s", visable_ipx_adr(&from_addr)));
      if ( ncprequest->type[0] == 0x22
        && ncprequest->type[1] == 0x22) {
        act_connection = ((int)ncprequest->connection);
        if (act_connection > 0  && act_connection <= MAX_CONNECTIONS) {
          act_c = &(connections[act_connection-1]);
          internal_act = 0;
          if (act_c->active && IPXCMPNODE(from_addr.node, my_addr.node)
                        && IPXCMPNET (from_addr.net,  my_addr.net)) {
            handle_fxx(ud.udata.len, (int)ncprequest->function);
          } else {
            XDPRINTF((1, 0, "NWBIND-LOOP addr of connection=%d is wrong",
              act_connection));
          }
        } else {
          XDPRINTF((1, 0, "NWBIND-LOOP connection=%d is wrong",
            act_connection));
        }
      } else if ( ncprequest->type[0] == 0xee
               && ncprequest->type[1] == 0xee
               && IPXCMPNODE(from_addr.node, my_addr.node)
               && IPXCMPNET (from_addr.net,  my_addr.net)) {
        /* comes from nwserv, i hope :)  */

        handle_ctrl();

      } else {
        XDPRINTF((1, 0, "NWBIND-LOOP got wrong type 0x%x func=0x%x",
           (int) GET_BE16(ncprequest->type), (int) ncprequest->function));
      }
    }
    if (got_sig == SIGHUP) {
     /* here I update some Bindery stuff from nwserv.conf */
      reinit_nwbind();
      got_sig = 0;
    }
  }
  if (ncp_fd > -1) {
    t_unbind(ncp_fd);
    t_close(ncp_fd);
    XDPRINTF((2,0, "LEAVE nwbind"));
    if (ipx_out_fd > -1) {
      t_unbind(ipx_out_fd);
      t_close(ipx_out_fd);
    }
  }
  sync_dbm();
  return(0);
}
