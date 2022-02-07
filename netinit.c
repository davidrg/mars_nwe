/* netinit.c 11-Sep-95 */
/* Initialisierung VON IPX u. SPX  unter USL 1.1 */
/* 'emuliert' Teil von NPSD */

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
#include <sys/dlpi.h>
#define NET_DEBUG  1
#define MY_NETWORK 0x10
#if NET_DEBUG
void print_adaptinfo(FILE *fout, ipxAdapterInfo_t *ai, char *s)
{
   if (s != NULL)
     fprintf(fout, "\n---- %s ----\n", s);
   fprintf(fout,
"dl_primitive             = %lu  \n\
dl_max_sdu               = %lu  \n\
dl_min_sdu               = %lu  \n\
dl_addr_length           = %lu  \n\
dl_mac_type              = %lu  \n\
dl_reserved              = %lu  \n\
dl_current_state         = %lu  \n\
dl_sap_length            = %ld  \n\
dl_service_mode          = %lu  \n\
dl_qos_length            = %lu  \n\
dl_qos_offset            = %lu  \n\
dl_qos_range_length      = %lu  \n\
dl_qos_range_offset      = %lu  \n\
dl_provider_style        = %lu  \n\
dl_addr_offset           = %lu  \n\
dl_version               = %lu  \n\
dl_brdcst_addr_length    = %lu  \n\
dl_brdcst_addr_offset    = %lu  \n\
dl_growth                = %lu  \n",
  ai->dl_primitive,
  ai->dl_max_sdu,
  ai->dl_min_sdu,
  ai->dl_addr_length,
  ai->dl_mac_type,
  ai->dl_reserved,
  ai->dl_current_state,
  ai->dl_sap_length,
  ai->dl_service_mode,
  ai->dl_qos_length,
  ai->dl_qos_offset,
  ai->dl_qos_range_length,
  ai->dl_qos_range_offset,
  ai->dl_provider_style,
  ai->dl_addr_offset,
  ai->dl_version,
  ai->dl_brdcst_addr_length,
  ai->dl_brdcst_addr_offset,
  ai->dl_growth);
  fflush(fout);
}

void print_netinfo(FILE *fout, netInfo_t *ni, char *s)
{
   ipxAdapterInfo_t *ai = &(ni->adapInfo);
   if (s != NULL)
     fprintf(fout, "\n---- %s ----\n", s);
   fprintf(fout, "Lan:%lx, state:%lx, err:%lx, netw:%lx, mux:%lx, node:%x.%x.%x.%x.%x.%x\n",
	  ni->lan, ni->state, ni->streamError, ni->network,
	  ni->muxId,
	  (int)ni->nodeAddress[0],
	  (int)ni->nodeAddress[1],
	  (int)ni->nodeAddress[2],
	  (int)ni->nodeAddress[3],
	  (int)ni->nodeAddress[4],
	  (int)ni->nodeAddress[5]);
   print_adaptinfo(fout, ai, NULL);
}
#else
#define print_adaptinfo(fout, ai, s)
#define print_netinfo(fout, ni, s)
#endif


int main(int argc, char **argv)
{
 int ipx0fd=open("/dev/ipx0",   O_RDWR);
 if (ipx0fd > -1) {
   int lan0fd=open("/dev/lan0", O_RDWR);
   if (lan0fd > -1) {
     struct strioctl str1, str2, str3, str4;
     int ipxfd;
     int j = -1;
     long max_adapter=0;
     netInfo_t netinfo;
     long    info_req = DL_INFO_REQ;
     ipxAdapterInfo_t *ai = &(netinfo.adapInfo);
     dl_bind_req_t    bind_req;
     struct {
       dl_bind_ack_t  b;
       uint8          addr[8];   /* Adresse */
     }                bind_ack;
     int muxid;
     int flagsp=0;
     int ilen;
     struct strbuf cntr1;
#if NET_DEBUG
     FILE *fout = fopen("xyz", "w+");
#endif
     /* DL_INFO */
     cntr1.maxlen = 4;
     cntr1.len    = 4;
     cntr1.buf    = (char *)&info_req;
     putmsg(lan0fd, &cntr1, NULL, 0);
     cntr1.maxlen = sizeof(ipxAdapterInfo_t);
     cntr1.len    = 0;
     cntr1.buf    = (char*)ai;
     if ((ilen=getmsg(lan0fd, &cntr1, NULL, &flagsp)) > 0) {
       char dummy[100];
       cntr1.maxlen = sizeof(dummy);
       cntr1.len    = 0;
       cntr1.buf    = dummy;
       flagsp = 0;
       getmsg(lan0fd, &cntr1, NULL, &flagsp);
       fprintf(stderr, "DL_INFO getmsg=%d bzw. %d > 0\n", ilen, cntr1.len);
     }
     print_adaptinfo(fout, ai, "nach DL_INFO");
     /* -----------------------------------------------  */
     bind_req.dl_primitive     = DL_BIND_REQ;
     bind_req.dl_sap           = 0x8137; /* SAP Type fr NetWare  */
     bind_req.dl_max_conind    = 1;
     bind_req.dl_service_mode  = DL_CLDLS;  /* 2  */
     bind_req.dl_conn_mgmt     = 0;
     bind_req.dl_xidtest_flg   = 1;

     cntr1.maxlen = sizeof(dl_bind_req_t);
     cntr1.len    = sizeof(dl_bind_req_t);
     cntr1.buf    = (char*)&bind_req;
     putmsg(lan0fd, &cntr1, NULL, 0);

     memset(&bind_ack, 0, sizeof(bind_ack));
     bind_ack.b.dl_primitive  = DL_BIND_REQ;
     cntr1.maxlen = sizeof(bind_ack);
     cntr1.len    = 0;
     cntr1.buf    = (char*)&bind_ack;
     flagsp = 0;
     getmsg(lan0fd, &cntr1, NULL, &flagsp);
     fprintf(stderr, "BIND ACK:sap 0x%x, addr_len %d, addr_offs %d\n \
	         addr=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\n" ,
	 bind_ack.b.dl_sap, bind_ack.b.dl_addr_length, bind_ack.b.dl_addr_offset,
	 (int)bind_ack.addr[0],
	 (int)bind_ack.addr[1],
	 (int)bind_ack.addr[2],
	 (int)bind_ack.addr[3],
	 (int)bind_ack.addr[4],
	 (int)bind_ack.addr[5],
	 (int)bind_ack.addr[6],
	 (int)bind_ack.addr[7]);


     /* DL_INFO */
     cntr1.maxlen = 4;
     cntr1.len    = 4;
     cntr1.buf    = (char *)&info_req;
     putmsg(lan0fd, &cntr1, NULL, 0);
     cntr1.maxlen = sizeof(ipxAdapterInfo_t);
     cntr1.len    = 0;
     cntr1.buf    = (char*)ai;
     if ((ilen=getmsg(lan0fd, &cntr1, NULL, &flagsp)) > 0) {
       char dummy[100];
       cntr1.maxlen = sizeof(dummy);
       cntr1.len    = 0;
       cntr1.buf    = dummy;
       flagsp = 0;
       getmsg(lan0fd, &cntr1, NULL, &flagsp);
       fprintf(stderr, "DL_INFO getmsg=%d bzw. %d > 0\n", ilen, cntr1.len);
     }
     print_adaptinfo(fout, ai, "nach DL_INFO 2");
     /* -----------------------------------------------  */

     str1.ic_cmd     = IPX_GET_MAX_CONNECTED_LANS;
     str1.ic_timout  = 0;
     str1.ic_len     = 4;
     str1.ic_dp      = (char*)&max_adapter;
     ioctl(ipx0fd, I_STR, &str1);

     printf("Max Adapter %ld\n", max_adapter);
     muxid           = ioctl(ipx0fd, I_LINK, lan0fd);  /* LINK */

/*-----------------------------------------------*/

   /*
     str3.ic_cmd     = IPX_SET_FRAME_TYPE_8023;
     str3.ic_len     = 0;
     str3.ic_timout  = 5;
     str3.ic_dp      = 0;
     ioctl(ipx0fd, I_STR, &str3);
    */
     /*
     str2.ic_cmd     = IPX_SET_FRAME_TYPE_SNAP;
      */
     /*
     str2.ic_cmd     = IPX_SET_FRAME_TYPE_8022;
     */
     str2.ic_timout         = 0;
     str2.ic_len            = sizeof(netinfo);
     str2.ic_dp             = (char*)&netinfo;
     netinfo.lan            = 0;
     netinfo.state          = 0;
     netinfo.network        = MY_NETWORK;
     netinfo.muxId          = muxid;
     netinfo.nodeAddress[0] = bind_ack.addr[0];    /*  0x00  */
     netinfo.nodeAddress[1] = bind_ack.addr[1];    /*  0x80  */
     netinfo.nodeAddress[2] = bind_ack.addr[2];    /*  0x48  */
     netinfo.nodeAddress[3] = bind_ack.addr[3];    /*  0x83  */
     netinfo.nodeAddress[4] = bind_ack.addr[4];    /*  0x14  */
     netinfo.nodeAddress[5] = bind_ack.addr[5];    /*  0x3f  */
     /*
     ai->dl_primitive       = DL_INFO_REQ ;
     ioctl(ipx0fd, I_STR, &str2);
     print_netinfo(fout, &netinfo, "nach SET_FRAME");
      */

     str3.ic_cmd     = IPX_SET_LAN_INFO;
     str3.ic_len     = sizeof(netinfo);
     str3.ic_timout  = 5;
     str3.ic_dp      = (char*)&netinfo;
     ioctl(ipx0fd, I_STR, &str3);
     print_netinfo(fout, &netinfo, "nach IPX_SET_LAN_INFO");

#if 0
     if ((ipxfd = open("/dev/ipx", O_RDWR)) > -1){
       int spxfd = open("/dev/nspxd", O_RDWR);
       if (spxfd > -1){
	 int pid=-1;
	 int akt_pid     = getpid();
	 char *progname  = "nwserv";
	 muxid = ioctl(spxfd, I_LINK, ipxfd);
	 str4.ic_cmd     = IPX_SET_SPX;
	 str4.ic_len     = 0;
	 str4.ic_timout  = 5;
	 str4.ic_dp      = (char*) NULL;
	 ioctl(spxfd, I_STR, &str4);
	 close(ipxfd);
	 pid=fork();
	 if (pid == 0) {  /* Child */
	   close(spxfd);
	   close(ipx0fd);
	   close(lan0fd);
	   execl(progname, progname, (argc > 1) ? *(argv+1) : NULL,  NULL);
	   /* Falls nicht OK Calling Prozess killen */
	   kill(akt_pid, SIGTERM);
	   kill(akt_pid, SIGQUIT);
	   exit (1);
	 }
	 if (pid > -1){
	    pause();
	    kill(pid, SIGTERM);  /* T”chter killen */
	    kill(pid, SIGQUIT);
	  } else perror("nwserv not running");
	 close(spxfd);
       } else {
	 perror("spx not open");
	 close(ipxfd);
       }
     } else perror("ipx not open");
#else
     if ((ipxfd = open("/dev/ipx", O_RDWR)) > -1){
       int pid=-1;
       int akt_pid     = getpid();
       char *progname  = "nwserv";
       close(ipxfd);
       pid=fork();
       if (pid == 0) {  /* Child */
	 close(ipx0fd);
	 close(lan0fd);
	 execl(progname, progname, (argc > 1) ? *(argv+1) : NULL,  NULL);
	 /* Falls nicht OK Calling Prozess killen */
	 kill(akt_pid, SIGTERM);
	 kill(akt_pid, SIGQUIT);
	 exit (1);
       }
       if (pid > -1){
	  pause();
	  kill(pid, SIGTERM);  /* T”chter killen */
	  kill(pid, SIGQUIT);
	} else perror("nwserv not running");
     } else perror("ipx not open");
#endif

     close(lan0fd);
#if NET_DEBUG
     fclose(fout);
#endif
   } else perror("lan0 not open");
   close(ipx0fd);
  } else perror("ipx0 not open");
}
