/* netcall.c: 05-Apr-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

int neterrno=0;

int ipx_init(void)
{
static int ipx_is_init=0;
static int ipx_is_ok  =0;
  if (!ipx_is_init) {
    ipx_is_init++;
    ipx_is_ok = IPXinit();
  }
  return(ipx_is_ok);
}

static void *get_shell_ptr(uint16 func)
{
  if (ipx_init()) {
    void *p = NULL;
    REGS    regs;
    SREGS   sregs;
    regs.x.ax = func;
    if (!(intdosx(&regs, &regs, &sregs) & 0xff))
      p = MK_FP(sregs.es, regs.x.si);
    return(p);
  } else return(NULL);
}

int detach(int servernmbr)
{
  REGS regsin, regsout;
  regsin.x.ax = 0xf101;
  regsin.x.dx = servernmbr;
  return(intdos(&regsin, &regsout) & 0xff);
}

int logout(void)
{
  REGS regsin, regsout;
  regsin.x.ax = 0xd700;
  return(intdos(&regsin, &regsout) & 0xff);
}

int redir_device_drive(int devicetyp, uint8 *devname, uint8 *remotename)
/* if devicetyp == -1, the redir is canceled */
/* devicetyp 3 = printer    */
/* devicetyp 4 = disk drive */
{
   REGS    regs;
   SREGS   sregs;
   int     result;
   uint8   buff1[16];
   uint8   buff2[128];
   uint8   *ldevname    = buff1;
   uint8   *lremotename = buff2;
   strncpy(ldevname,    devname,    16);
   regs.x.ax = (devicetyp == -1) ? 0x5f04 : 0x5f03;
   regs.h.bl = (uint8)devicetyp;
   regs.x.cx = 0x574e; /* user sign 'NW' */
   sregs.ds  = FP_SEG(ldevname);
   regs.x.si = FP_OFF(ldevname);
   if (devicetyp > -1) {
     strncpy(lremotename, remotename, 128);
     sregs.es  = FP_SEG(lremotename);
     regs.x.di = FP_OFF(lremotename);
   }
   result = intdosx(&regs, &regs, &sregs);
   return(regs.x.cflag ? -result : 0);
}

int list_redir(int index, int *devicetyp, uint8 *devname, uint8 *remotename)
{
   REGS    regs;
   SREGS   sregs;
   int     result;
   uint8   buff1[16];
   uint8   buff2[128];
   uint8   *ldevname    = buff1;
   uint8   *lremotename = buff2;
   memset(ldevname,    0, sizeof(buff1));
   memset(lremotename, 0, sizeof(buff2));
   regs.x.ax = 0x5f02;
   regs.x.bx = index;
   regs.x.cx = 0x574e; /* user sign 'NW' */
   sregs.ds  = FP_SEG(ldevname);
   regs.x.si = FP_OFF(ldevname);
   sregs.es  = FP_SEG(lremotename);
   regs.x.di = FP_OFF(lremotename);
   result = intdosx(&regs, &regs, &sregs);
   if (!regs.x.cflag) {
     if (devname)    strcpy(devname,    ldevname);
     if (remotename) strcpy(remotename, lremotename);
     if (devicetyp)  *devicetyp = (int)regs.h.bl;
     return((int)regs.h.bh);
   } else return(-result);
}

int get_drive_info(uint8 drivenumber, uint8 *connid,
                   uint8 *dhandle,    uint8 *statusflags)

/* drivenumber 0 .. 31 */
{
  uint8 *drive_handle_table = get_shell_ptr(0xef00);
  uint8 *drive_flag_table   = get_shell_ptr(0xef01);
  uint8 *drive_conn_table   = get_shell_ptr(0xef02);

  if (  !drive_handle_table
     || !drive_flag_table
     || !drive_conn_table
     || drivenumber > 31) {
     char path[100];
     if (drivenumber < 2 || !getcurdir(drivenumber+1, path)) {
       *dhandle     = 0;
       *connid      = 0;
       *statusflags = 0x80;
       return(0);
     }
     return(-1);
  }
  *dhandle     = *(drive_handle_table+ drivenumber);
  *connid      = *(drive_conn_table  + drivenumber);
  *statusflags = *(drive_flag_table  + drivenumber);
  return(0);
}

typedef struct {
  char fsname[8][48];
} SERVER_NAME_TABLE;

int get_fs_name(int connid, char *name)
/* Connection 1 .. 8 */
{
  SERVER_NAME_TABLE *sf_t = get_shell_ptr(0xef04);
  if (sf_t && connid > 0 && connid-- < 8){
    strmaxcpy(name, sf_t->fsname[connid], 48);
    return(0);
  }
  name[0] = '\0';
  return(-11);
}

static char *path_env_name="PATH";

int get_search_drive_vektor(SEARCH_VECTOR_ENTRY *vec)
/* maximal 16 Entries */
{
  char *path=getglobenv(path_env_name);
  SEARCH_VECTOR_ENTRY *v = vec;
  int anz=0;
  v->drivenummer = 0xff;
  if (path){
    while (*path && anz++ < 16){
      char *p1 = path;
      int  len = 0;
      while (*path && *path++ !=';') len++;
      if (*(p1+1) == ':' && *p1 >= 'A' &&  *p1 <= 'Z') {
        v->drivenummer =  *p1 - 'A';
        get_drive_info(v->drivenummer, &(v->connid),
           &(v->dhandle), &(v->flags));
        strmaxcpy(v->dospath, p1+2, min(len-2, sizeof(v->dospath)-1));
      } else {
        v->flags       = 0;
        v->drivenummer = 0xfe;  /* ergibt ? */
        strmaxcpy(v->dospath, p1, min(len, sizeof(v->dospath)-1));
      }
      (++v)->drivenummer =  0xff;
      if (*path == ';') path++;
    }
  }
  return(0);
}

int set_search_drive_vektor(SEARCH_VECTOR_ENTRY *vec)
{
  char path[256];
  char *p=path;
  SEARCH_VECTOR_ENTRY *v;
  int  plen=strlen(path_env_name);
  int  maxcount=16;
  strcpy(path, path_env_name);
  path[plen]   = '=';
  path[++plen] = '\0';

  while (maxcount-- && (NULL != (v = vec++)) && v->drivenummer != 0xff){
    if (v->drivenummer < 26 || *(v->dospath)) {
      if (p > path) *p++=';';
      else p+=plen;
      if (v->drivenummer < 26) {
        *p++ = (char) v->drivenummer + 'A';
        *p++ = ':';
        if (*v->dospath) {
          strcpy(p,  v->dospath);
          p+= strlen(v->dospath);
        } else {
          *p++='.';
          *p  ='\0';
        }
      } else {
        strcpy(p,  v->dospath);
        p+= strlen(v->dospath);
      }
    }
  }
  return(putglobenv(path));
}

int alloc_dir_handle(int func,
                     int dhandle,
                     char *path,
                     int driveletter,
                     uint8 *effrights)
{
  int pathlen = (path == NULL) ? 0 : strlen(path);
  struct {
    uint16  len;
    uint8   func;
    uint8   dhandle;
    uint8   drive;
    uint8   pathlen;
    uint8   path[256];
  } req;
  struct {
   uint16  len;
   uint8   newhandle;
   uint8   effrights;
  } repl;
  req.func    = (uint8)func;
  req.dhandle = (uint8)dhandle;
  req.drive   = (uint8)driveletter;
  req.pathlen = (uint8)pathlen;
  xmemmove(req.path, path, pathlen);
  req.len     = 4 + pathlen;
  repl.len    = 2;
/*
  printf("alloc_dir_handle, path=%s, len=%d, disk=%c\n", path, pathlen, driveletter);
*/
  neterrno    =  Net_Call(0xE200, &req, &repl);
  fprintf(stderr, "neterrno=%d\n", neterrno);
  if (neterrno && neterrno != 0xff)  return(-1);

  if (effrights) *effrights = repl.effrights;
  return((int)repl.newhandle);
}

int dealloc_dir_handle(int dhandle)
{
  struct {
    uint16  len;
    uint8   func;
    uint8   dhandle;
  } req;
  struct {
   uint16  len;
  } repl;
  req.len     = 2;
  req.func    = 0x14;
  req.dhandle = (uint8)dhandle;
  repl.len    = 0;
  neterrno    = Net_Call(0xE200, &req, &repl);
  if (neterrno) return(-1);
  else return(0);
}

int set_dir_path(uint8 desthandle, uint8 handle, char *path)
/* Set Directory Handle */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   desth;
    uint8   sourceh;
    uint8   pathlen;
    uint8   path[256];
  } req;
  struct {
    uint16  len;
  } repl;
  req.pathlen = (path) ? strlen(path) : 0;
  if (req.pathlen) memcpy(req.path, path, (int)req.pathlen);
  req.len     = 4+req.pathlen;
  req.func    = 0;
  req.sourceh = handle;
  req.desth   = desthandle;
  repl.len    = 0;
  neterrno    = Net_Call(0xE200, &req, &repl);
  return( (neterrno) ? -1 : 0);
}

int get_dir_path(uint8 dhandle, char *path)
{
  struct {
    uint16  len;
    uint8   data[2];
  } req;
  struct {
   uint16  len;
   uint8   pathlen;
   uint8   path[255];
  } repl;
  req.len     = 2;
  req.data[0] = 0x1;
  req.data[1] = dhandle;
  repl.len    = 256;
  neterrno    = Net_Call(0xE200, &req, &repl);
  if (neterrno) return(-1);
  else {
    strmaxcpy(path, repl.path, (int)repl.pathlen);
    return(0);
  }
}

int get_volume_name(uint8 nr, char *name)
{
  struct {
    uint16  len;
    uint8   func;
    uint8   nr;
  } req;
  struct {
   uint16  len;
   uint8   namlen;
   uint8   name[16];
  } repl;
  req.len     = 2;
  req.func    = 0x6;
  req.nr      = nr;
  repl.len    = 17;
  neterrno    = Net_Call(0xE200, &req, &repl);
  if (neterrno) return(-1);
  else {
    strmaxcpy(name, repl.name, (int)repl.namlen);
    return(0);
  }
}


int save_dir_handle(uint8 dhandle, uint8 *savebuffer)
{
  struct {
    uint16  len;
    uint8   func;
    uint8   dhandle;
  } req;
  struct {
   uint16  len;
   uint8   savebuffer[16];
  } repl;
  req.len     = 2;
  req.func    = 0x17;
  req.dhandle = dhandle;
  repl.len    = 16;
  neterrno    = Net_Call(0xE200, &req, &repl);
  if (neterrno) return(-1);
  else {
    xmemmove(savebuffer, repl.savebuffer, 16);
    return(0);
  }
}

int restore_dir_handle(uint8 *savebuffer, uint8 *dhandle, uint8 *effrights)
{
  struct {
    uint16  len;
    uint8   func;
    uint8   savebuffer[16];
  } req;
  struct {
   uint16   len;
   uint8    dhandle;
   uint8    effrights;
  } repl;
  req.len      = 17;
  req.func     = 0x18;
  repl.len     = 2;
  xmemmove(req.savebuffer, savebuffer, 16);
  neterrno     = Net_Call(0xE200, &req, &repl);
  if (neterrno) return(-1);
  else {
    *dhandle   = repl.dhandle;
    *effrights = repl.effrights;
    return(0);
  }
}










int mapdrive(uint8 connection,   uint8 dhandle, char *path,
             uint8 searchflag,   uint8 searchorder,
             uint8 *driveletter, uint8 *newdhandle, uint8 *effrights)

/* Searchorder 0 normal Drive; 1..16 Searchdrives   */
/* searchflag only if Searchdrive  :                */
/* DRIVE_ADD, DRIVE_INSERT, DRIVE_DELETE            */
{


  return(-1);
}







