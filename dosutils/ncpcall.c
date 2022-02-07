/* ncpcall.c 14-Mar-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

/* ----------------  0x16 ----------------------------------- */
int ncp_16_02(int dirhandle,
              uint8  *path,
              int    *sub_dir,
              uint8  *resultpath,
              uint32 *creattime,
              uint32 *owner_id)

/* returns max. rights or -1 if failed */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   dirhandle;
    uint8   sub_dir[2];
    uint8   pathlen;
    uint8   path[256];
  } req;
  struct {
   uint16 len;
   uint8  sub_dir_name[16];
   uint8  create_date_time[4];
   uint8  owner_id[4];       /* HI LOW */
   uint8  max_right_mask;
   uint8  reserved;          /* Reserved by Novell */
   uint8  sub_dir_nmbr[2];   /* HI LOW */
  } repl = { sizeof(repl) - sizeof(uint16) };
  req.func      = 0x02;
  U16_TO_BE16((sub_dir) ? *sub_dir : 1, req.sub_dir);
  req.dirhandle = (uint8) dirhandle;
  req.pathlen   = (uint8) ((path) ? strlen(path) : 0);
  req.len       = 5 + req.pathlen;
  strmaxcpy(req.path, path, req.pathlen);
  neterrno      = Net_Call(0xE200, &req, &repl);
  if (neterrno)   return(-1);
  if (resultpath) strmaxcpy(resultpath, repl.sub_dir_name, 16);
  if (sub_dir)    *sub_dir   = GET_BE16(repl.sub_dir_nmbr);
  if (creattime)  *creattime = GET_BE32(repl.create_date_time);
  if (owner_id)   *owner_id  = GET_BE32(repl.owner_id);
  return((int) repl.max_right_mask);
}

/* ----------------  0x17 ----------------------------------- */
int ncp_17_02(int module, int debuglevel)
/* debuglevel fuer module setzen */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   module;
    uint8   debug;
  } req = { sizeof(req) - sizeof(uint16) };
  struct {
   uint16   len;
   uint8    olddebug;
  } repl     = { sizeof(repl) - sizeof(uint16) };
  req.func   = 0x2;
  req.module = (uint8) module;
  req.debug  = (uint8) debuglevel;
  neterrno   = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  return((int) repl.olddebug);
}

int ncp_17_14(uint8 *objname, uint16 objtyp, uint8 *password)
/* login unencreypted */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   typ[2];
    uint8   namlen;
    uint8   buff[48+1+128];
  } req;
  struct {
   uint16   len;
  } repl= { 0 };
  uint8 *p=req.buff;
  req.func     = 0x14;
  U16_TO_BE16(objtyp, req.typ);
  req.namlen = min(47, strlen(objname));
  memcpy(p,   objname, req.namlen);
  p       += req.namlen;
  *p = (uint8) min(128, strlen(password));
  req.len      = 4 + req.namlen + 1 + *p;
  memcpy(p+1, password, (int) *p);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  return(0);
}

int ncp_17_17(uint8 *key)
/* get crypt key */
{
  struct {
    uint16  len;
    uint8   func;
  } req;
  struct {
   uint16   len;
   uint8    key[8];
  } repl;
  req.len      = 1;
  req.func     = 0x17;
  repl.len     = 8;
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  else {
    memcpy(key, repl.key, 8);
    return(0);
  }
}

int ncp_17_18(uint8 *cryptkey, uint8 *objname, uint16 objtyp)
/* keyed login */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   key[8];
    uint8   typ[2];
    uint8   namlen;
    uint8   name[48];
  } req;
  struct {
   uint16   len;
  } repl={ 0 };
  req.len      = sizeof(req) - sizeof(uint16);
  req.func     = 0x18;
  U16_TO_BE16(objtyp, req.typ);
  req.namlen   = min(sizeof(req.name), strlen(objname));
  memcpy(req.key,  cryptkey, 8);
  memcpy(req.name, objname, (int) req.namlen);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  return(0);
}

uint32 ncp_17_35(uint8 *objname, uint16 objtyp)
/* get bindery object id */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   typ[2];
    uint8   namlen;
    uint8   name[48];
  } req;
  struct {
   uint16   len;
   uint8    object_id[4];
   uint8    object_type[2];
   uint8    object_name[48];
  } repl;
  req.len      = sizeof(req)  - sizeof(uint16);
  repl.len     = sizeof(repl) - sizeof(uint16);
  req.func     = 0x35;
  U16_TO_BE16(objtyp, req.typ);
  req.namlen   = min(sizeof(req.name), strlen(objname));
  memcpy(req.name, objname, (int) req.namlen);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(0L);
  strmaxcpy(objname, repl.object_name, 47);
  return(GET_BE32(repl.object_id));
}

int ncp_17_36(uint32 obj_id, uint8 *objname, uint16 *objtyp)
/* get bindery object name */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   id[4];
  } req;
  struct {
   uint16   len;
   uint8    object_id[4];
   uint8    object_type[2];
   uint8    object_name[48];
  } repl;
  req.len      = sizeof(req)  - sizeof(uint16);
  repl.len     = sizeof(repl) - sizeof(uint16);
  req.func     = 0x36;
  U32_TO_BE32(obj_id, req.id);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  if (objname) strmaxcpy(objname, repl.object_name, 47);
  if (objtyp)  *objtyp = GET_BE16(repl.object_type);
  return(0);
}


int ncp_17_40(uint8 *objname, uint16 objtyp,
                               uint8 *password, uint8 *newpassword)
/* change password unencreypted */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   typ[2];
    uint8   namlen;
    uint8   buff[48+1+128+1+128];
  } req;
  struct {
   uint16   len;
  } repl = { 0 };
  uint8 *p=req.buff;
  req.func     = 0x40;
  U16_TO_BE16(objtyp, req.typ);
  req.namlen = min(47, strlen(objname));
  memcpy(p,   objname, req.namlen);
  p       += req.namlen;
  *p = (uint8) min(128, strlen(password));
  req.len      = 4 + req.namlen + 1 + *p;
  memcpy(p+1, password, (int) *p);
  p            += (1 +  *p);
  *p = (uint8) min(128, strlen(newpassword));
  req.len      += (1 + *p);
  memcpy(p+1, newpassword, (int) *p);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  return(0);
}

int ncp_14_46(uint32 *obj_id)
/* get bindery access level & actual ID */
{
  struct {
    uint16  len;
    uint8   func;
  } req;
  struct {
   uint16   len;
   uint8    access;
   uint8    id[4];
  } repl;
  req.len      = 1;
  req.func     = 0x46;
  repl.len     = 5;
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  else {
    if (obj_id) *obj_id = GET_BE32(repl.id);
    return(repl.access);
  }
}


int ncp_17_4b(uint8 *cryptkey, uint8 *objname, uint16 objtyp,
                   int passwx, uint8 *newpassword)
/* keyed change password */
{
  struct {
    uint16  len;
    uint8   func;
    uint8   key[8];
    uint8   typ[2];
    uint8   namlen;
    uint8   buff[48+1+16];
  } req;
  struct {
   uint16   len;
  } repl = { 0 };
  uint8 *p     = req.buff;
  req.func     = 0x4b;
  memcpy(req.key,  cryptkey, 8);
  U16_TO_BE16(objtyp, req.typ);
  req.namlen   = (uint8) min(48, strlen(objname));
  req.len      = 12 + req.namlen + 1 + 16;
  memcpy(p, objname, (int) req.namlen);
  p   += req.namlen;
  *p++ = (uint8) passwx;
  memcpy(p, newpassword, 16);
  neterrno     = Net_Call(0xE300, &req, &repl);
  if (neterrno) return(-1);
  return(0);
}
