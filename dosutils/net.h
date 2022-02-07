/* net.h: 20-May-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <conio.h>
#include <io.h>
#include <bios.h>
#include <dos.h>
#include <process.h>
#include <stdarg.h>

typedef unsigned int       UI;
typedef unsigned int       uint;
typedef unsigned char      UC;
typedef unsigned char      uint8;
typedef unsigned short int uint16;
typedef unsigned long  int uint32;

typedef union  REGS  REGS;
typedef struct SREGS SREGS;

typedef void  (*FUNC_VOID)();
typedef int   (*FUNC_INT)();


typedef struct {
   uint8  checksum[2];
   uint16  packetlen;
   uint8  tcontrol;
   uint8  ptype;
   uint8  dest_net[4];
   uint8  dest_node[6];
   uint16  dest_sock;        /* HI LOW */
   uint8  source_net[4];
   uint8  source_node[6];
   uint16  source_sock;      /* HI LOW */
} IPX_HEADER;

typedef struct {
  uint8        *link_address;
  FUNC_VOID    esr_routine;
  uint8        in_use_flag;
  uint8        completition_code;
  uint16       socket;               /* HI LOW 		        */
  uint8        ipx_workspace[4];     /* interner Gebrauch 	*/
  uint8        drv_workspace[4];     /* interner Gebrauch 	*/
  uint8        immediate_address[6]; /* HI LOW Node Address 	*/
  uint16       fragment_count;       /* Anzahl Fragment Buffers */
  uint8        *fragment_1;
  uint16       fragment_1_size;
  /* K”nnen auch mehr sein */
} ECB;

#include "kern.h"

#define UI2NET(i)  ( ( (i) << 8)  |  ( ((i)>>8) & 0xFF) )
#define NET2UI(i)  ( ( (i) << 8)  |  ( ((i)>>8) & 0xFF) )

#define U16_TO_BE16(u, b) { uint16 a=(u); \
               *(  (uint8*) (b) )    = *( ((uint8*) (&a)) +1); \
               *( ((uint8*) (b)) +1) = *(  (uint8*) (&a)); }


#define U32_TO_BE32(u, ar) { uint32 a= (u); uint8 *b= ((uint8*)(ar))+3; \
               *b-- = (uint8)a; a >>= 8;  \
               *b-- = (uint8)a; a >>= 8;  \
               *b-- = (uint8)a; a >>= 8;  \
               *b   = (uint8)a; }

#define U16_TO_16(u, b) { uint16 a=(u); memcpy(b, &a, 2); }
#define U32_TO_32(u, b) { uint32 a=(u); memcpy(b, &a, 4); }

#define GET_BE16(b)  (     (int) *(((uint8*)(b))+1)  \
                     | ( ( (int) *( (uint8*)(b)   )  << 8) ) )

#define GET_BE32(b)  (   (uint32)   *(((uint8*)(b))+3)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 16) \
                   | (  ((uint32)   *( (uint8*)(b)   ) ) << 24) )


#define GET_16(b)    (     (int) *( (uint8*)(b)   )  \
                     | ( ( (int) *(((uint8*)(b))+1)  << 8) ) )

#define GET_32(b)    (   (uint32)   *( (uint8*)(b)   )  \
                   | (  ((uint32)   *(((uint8*)(b))+1) ) << 8)  \
                   | (  ((uint32)   *(((uint8*)(b))+2) ) << 16) \
                   | (  ((uint32)   *(((uint8*)(b))+3) ) << 24) )

#define MAX_U32    ((uint32)0xffffffffL)
#define MAX_U16    ((uint16)0xffff)

#define NWSERV   1
#define NCPSERV  2
#define NWCONN   3
#define NWCLIENT 4
#define NWBIND   5

/* net.c */
extern char  *funcname;
extern char   prgpath[];

extern int   call_func_entry(int argc, char *argv[]);

/* tools.c */
extern void  clear_kb(void);
extern int   key_pressed(void);
extern int   ask_user(char *p, ...);
#define xfree(p)      x_x_xfree((char **)&(p))
extern  void x_x_xfree(char **p);
extern char  *xmalloc(uint  size);
extern char  *xcmalloc(uint size);

extern int   strmaxcpy(char *dest, char *source, int len);
extern char  *xadd_char(char *s, int c, int maxlen);
extern uint8 *upstr(uint8 *s);
extern void korrpath(char *s);
extern void get_path_fn(char *s, char *p, char *fn);

#define reb(s) deb((s)),leb((s))

extern void  deb(uint8 *s);
extern void  leb(uint8 *s);


#define add_char(s, c) xadd_char((s), (c), -1)

extern char *getglobenv(char *option);
extern int  putglobenv(char  *option);

/* NETCALLS */
#define DRIVE_ADD     1
#define DRIVE_INSERT  2
#define DRIVE_DELETE  3

typedef struct {
  uint8  drivenummer;   /* 0xff=last of list,  0xfe only DOSPATH */
  uint8  flags;         /* 0x80 = local drive */
  char   dospath[65];
  uint8  connid;
  uint8  dhandle;
} SEARCH_VECTOR_ENTRY;

typedef SEARCH_VECTOR_ENTRY  SEARCH_VECTOR[17];

extern int neterrno;

#define alloc_permanent_dir_handle(dhandle, path, drive, rights) \
  alloc_dir_handle(0x12, (dhandle), (path), (drive), (rights))

#define alloc_temp_dir_handle(dhandle, path, drive, rights) \
  alloc_dir_handle(0x13, (dhandle), (path), (drive), (rights))

extern int ipx_init(void);

extern int alloc_dir_handle(int func, int dhandle, char *path,
                            int driveletter, uint8 *effrights);

extern int dealloc_dir_handle(int dhandle);

extern int get_dir_path(uint8 dhandle, char *path);
extern int get_volume_name(uint8 nr, char *name);

extern int get_search_drive_vektor(SEARCH_VECTOR_ENTRY *vec);
extern int set_search_drive_vektor(SEARCH_VECTOR_ENTRY *vec);

/********* ncpcall.h  ***********/
extern  int   ncp_16_02(int dirhandle,
              uint8  *path,
              int    *sub_dir,
              uint8  *resultpath,
              uint32 *creattime,
              uint32 *owner_id);

extern int    ncp_17_02(int   module, int debuglevel);
extern int    ncp_17_14(uint8 *objname, uint16 objtyp, uint8 *password);
extern int    ncp_17_17(uint8 *key);
extern int    ncp_17_18(uint8 *cryptkey, uint8 *objname, uint16 objtyp);
extern uint32 ncp_17_35(uint8 *objname, uint16 objtyp);
extern int    ncp_17_40(uint8 *objname, uint16 objtyp, uint8 *password,
                                                       uint8 *newpassword);

extern int    ncp_17_4b(uint8 *cryptkey, uint8 *objname, uint16 objtyp,
                               int passwx, uint8 *newpassword);

/* map.c */
extern int func_map   (int argc, char *argv[], int mode);
extern int func_path  (int argc, char *argv[], int mode);

/* login.c */
extern int func_login  (int argc, char *argv[], int mode);
extern int func_logout (int argc, char *argv[], int mode);
extern int func_passwd (int argc, char *argv[], int mode);
extern int func_profile(int argc, char *argv[], int mode);
extern int func_cwd    (int argc, char *argv[], int mode);
extern int func_echo   (int argc, char *argv[], int mode);
extern int func_exec   (int argc, char *argv[], int mode);
extern int read_command_file(char *fstr);

/* slist.c */
extern int func_slist (int argc, char *argv[], int mode);

/* nwdebug.c */
extern int func_debug (int argc, char *argv[], int mode);

/* nwtests.c */
extern int func_tests (int argc, char *argv[], int mode);

/* capture.c */
extern int func_capture(int argc, char *argv[], int mode);



