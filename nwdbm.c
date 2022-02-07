/* nwdbm.c  20-Jan-97  data base for mars_nwe */
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

/*
 * This code is only called from the process 'nwbind'
 * So, there is no need for locking or something else.
 */

#include "net.h"
#include "nwdbm.h"
#include "nwcrypt.h"
#ifdef LINUX
#  include <ndbm.h>
#  ifndef  SHADOW_PWD
#    define SHADOW_PWD  0
#  endif
#else
#  include </usr/ucbinclude/ndbm.h>
#  undef  SHADOW_PWD
#  define SHADOW_PWD  1
#endif

#if SHADOW_PWD
#  include <shadow.h>
#endif

#define DBM_REMAINS_OPEN  1

int tells_server_version=1; /* default 1 since 12-Jan-97 */
int password_scheme=0; /* PW_SCHEME_CHANGE_PW; */

static int entry8_flags = 0;

static datum key;
static datum data;
static DBM   *my_dbm=NULL;


#define FNPROP  0
#define FNVAL   1
#define FNOBJ   2

#define FNIOBJ  3     /* Index for  Object Names */

#define COUNT_DBM_FILES  4

static char  *dbm_fn[COUNT_DBM_FILES]  = {
 "nwprop", "nwval", "nwobj"
#if COUNT_DBM_FILES > 3
 ,"nwiobj"
#endif
 };

#if DBM_REMAINS_OPEN
static DBM   *my_dbms[COUNT_DBM_FILES] = {
  NULL, NULL, NULL
#if COUNT_DBM_FILES > 3
, NULL
#endif
};
#endif

static int x_dbminit(char *s)
{
  char buff[256];
  sprintf(buff, "%s/%s", PATHNAME_BINDERY, s);
  my_dbm = dbm_open(buff, O_RDWR|O_CREAT, 0600);
  return( (my_dbm == NULL) ? -1 : 0);
}

static int dbminit(int what_dbm)
{
#if DBM_REMAINS_OPEN
  int result = 0;
  if (NULL == my_dbms[what_dbm]) {
    result = x_dbminit(dbm_fn[what_dbm]);
    if (!result)  my_dbms[what_dbm] = my_dbm;
  } else my_dbm = my_dbms[what_dbm];
  return(result);
#else
  return(x_dbminit(dbm_fn[what_dbm]));
#endif
}

static int dbmclose()
{
  if (my_dbm != NULL) {
#if !DBM_REMAINS_OPEN
    dbm_close(my_dbm);
#endif
    my_dbm = NULL;
  }
  return(0);
}

void sync_dbm()
{
#if DBM_REMAINS_OPEN
  int k = COUNT_DBM_FILES;
  while (k--) {
    if (NULL != my_dbms[k]) {
      dbm_close(my_dbms[k]);
      my_dbms[k] = NULL;
    }
  }
#endif
}


#define firstkey()          dbm_firstkey(my_dbm)
#define nextkey(key)        dbm_nextkey(my_dbm)
#define delete(key)         dbm_delete(my_dbm, key)
#define fetch(key)          dbm_fetch(my_dbm,  key)
#define store(key, content) dbm_store(my_dbm,  key, content, DBM_REPLACE)


static int handle_iobj(int mode, NETOBJ *o)
/* modes:
 * 0  = search/read
 * 1  = rewrite  ( not needed yet )
 * 2  = rewrite/creat
 * 3  = delete
 */
{
  int result=-0xff;
  if (!dbminit(FNIOBJ)){
    NETIOBJ  iobj;
    strncpy(iobj.name, o->name, sizeof(iobj.name));
    iobj.type = o->type;
    key.dsize = NETIOBJ_KEY_SIZE;
    key.dptr  = (char*)&iobj;
    result    = -0xfc; /* no Object */
    if (mode == 3) {
      if (!delete(key)) result=0;
    } else  {
      data = fetch(key);
      if (data.dptr != NULL) {
        NETIOBJ *piobj=(NETIOBJ*)data.dptr;
        XDPRINTF((3,0, "got index of OBJ name=%s, type=0x%x, id = 0x%x",
               piobj->name, (int)piobj->type, piobj->id));
        if (!mode) {
          o->id  = piobj->id;
          result = 0;
        } else {  /* write back */
          piobj->id    = o->id;
          result=(store(key, data)) ? -0xff : 0;
        }
      } else if (mode == 2) { /* creat */
        data.dsize = sizeof(NETIOBJ);
        data.dptr  = (char*)&iobj;
        iobj.id    = o->id;
        result= (store(key, data)) ? -0xff : 0;
      }
    }
  }
  dbmclose();
  XDPRINTF((3, 0,"handle_iobj mode=%d, result=0x%x, OBJ=%s, type=0x%x",
      mode, -result,
      o->name,(int)o->type));
  return(result);
}

int find_obj_id(NETOBJ *o)
/* no wildcards allowed */
{
  int result;
  XDPRINTF((2, 0,"findobj_id OBJ=%s, type=0x%x", o->name,(int)o->type));
  if ((result=handle_iobj(0, o)) == 0) {
    if (!dbminit(FNOBJ)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      result    = -0xff;
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((3,0, "got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
        if ( (!strncmp(obj->name, o->name, sizeof(obj->name)))
            && obj->type == o->type) {
          memcpy(o, data.dptr, sizeof(NETOBJ));
          result=0;
        } else {
          XDPRINTF((1,0, "OBJ Index '%s',0x%x, clashes OBJ data '%s', 0x%x",
                       o->name,   (int)o->type,
                       obj->name, (int)obj->type));
        }
      } else {
        XDPRINTF((1,0, "OBJ Index '%s',0x%x, id=0x%x not found in OBJ data",
                       o->name, (int)o->type, o->id));
      }
    } else result = -0xff;
    dbmclose();
    if (!result)
      return(0);
  }
  result=scan_for_obj(o, 0);
  if (!result) { /* was ok, we will rewrite/creat iobj record */
    XDPRINTF((1, 0,"findobj_id OBJ='%s', type=0x%x, id=0x%x not in Index File",
       o->name,(int)o->type,o->id));
    handle_iobj(2, o);
  }
  return(result);
}

int scan_for_obj(NETOBJ *o, uint32 last_obj_id)
/*
 * scans for object,
 * wildcards in objectname allowed
 * wildcard (MAX_U16) in  objecttype allowed
 */
{
  int result = -0xfc; /* no Object */
  XDPRINTF((2, 0,"scan_for_obj OBJ=%s, type=0x%x, lastid=0x%x",
	      o->name, (int)o->type, (int)last_obj_id));

  if (!dbminit(FNOBJ)){
    key = firstkey();
    if (last_obj_id && (last_obj_id != MAX_U32)){
      int  flag = 0;
      while (key.dptr != NULL && !flag) {
	if ( ((NETOBJ*)(key.dptr))->id == last_obj_id) flag++;
	key = nextkey(key);
      }
    }
    while (key.dptr != NULL && result) {
      data = fetch(key);
      if (data.dptr != NULL){
	NETOBJ *obj = (NETOBJ*)data.dptr;
	if ( ( ((int)obj->type == (int)o->type) || o->type == MAX_U16) &&
	   name_match(obj->name, o->name))  {
	  XDPRINTF((2, 0, "found OBJ=%s, id=0x%x", obj->name, (int)obj->id));
	  result = 0;
	  memcpy((char *)o, (char*)obj, sizeof(NETOBJ));
	} else {
	  XDPRINTF((3,0,"not found,but NAME=%s, type=0x%x, id=0x%x",
	                 obj->name, (int)obj->type, (int)obj->id));
	}
      }
      if (result) key = nextkey(key);
    } /* while */
  } else result = -0xff;
  dbmclose();
  return(result);
}

static int loc_delete_property(uint32 obj_id,
                               uint8 *prop_name,
                               uint8 prop_id,
                               int   ever)  /* ever means no access tests */
/* deletes Object property or properties */
/* wildcards allowed in property name  */
{
  uint8 xset[256];
  int result = -0xfb; /* no property */
  memset(xset, 0, sizeof(xset));
  if (!prop_id) {
    XDPRINTF((2,0, "loc_delete_property obj_id=0x%x, prop=%s", obj_id, prop_name));
    if (!dbminit(FNPROP)){
      for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
	NETPROP *p=(NETPROP*)key.dptr;
	if (p->obj_id == obj_id) {
	  data = fetch(key);
	  p = (NETPROP*)data.dptr;
          if (p != NULL && name_match(p->name, prop_name)){
	    XDPRINTF((2,0, "found prop: %s, id=%d for deleting", p->name, (int)p->id));
            if (ever || !b_acc(obj_id, p->security, 0x13)) {
	      if ((int)(p->id) > result) result = (int)(p->id);
	      xset[p->id]++;
            } else if (result < 0) result = -0xf6; /* no delete priv. */
	  }
	}
      } /* for */
    } else result = -0xff;
    dbmclose();
  } else {
    XDPRINTF((2,0, "loc_delete_property obj_id=0x%x, prop_id=%d", obj_id, (int)prop_id));
    xset[prop_id]++;
    result = prop_id;
  }
  if (result > 0) {
    if (!dbminit(FNVAL)){
      int k;
      NETVAL val;
      key.dptr   = (char*)&val;
      key.dsize  = NETVAL_KEY_SIZE;
      val.obj_id = obj_id;
      for (k = 1; k <= result; k++){
	if (xset[k]){
	  int l   = 0;
	  val.prop_id  = (uint8)k;
	  while (l++ < 255) {
	    val.segment = (uint8)l;
	    if (delete(key)) break;
	  }
	}
      } /* for */
    } else result=-0xff;
    dbmclose();
    if (result > 0) {
      if (!dbminit(FNPROP)){  /* now delete properties */
	int k;
	NETPROP prop;
	key.dptr   = (char*)&prop;
	key.dsize  = NETPROP_KEY_SIZE;
	prop.obj_id = obj_id;
	for (k = (prop_id) ? prop_id : 1; k <= result; k++){
	  if (xset[k]){
	    prop.id  = (uint8)k;
	    if (delete(key)) {
	      result = -0xf6;
	      break;
	    }
	  }
	} /* for */
	if (result > 0) result=0;
      } else result=-0xff;
      dbmclose();
    }
  }
  return(result);
}

static int prop_delete_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = 0; /* we lie insteed of -0xea;  no such member */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)0;
    data        = fetch(key);
    while (1) {
      val.segment++;
      data        = fetch(key);
      if (data.dptr != NULL) {
	NETVAL  *v = (NETVAL*)data.dptr;
	uint8   *p = v->value;
	int      k = 0;
	while (k++ < 32){
	  if (GET_BE32(p) == member_id) {
	    memset(p, 0, 4);
	    memcpy(&val, v, sizeof(NETVAL));
	    data.dptr = (char*)&val;
	    if (store(key, data)) result=-0xff;
	    else result=0;
	    goto L1;
	  } else p += 4;
	}
      } else break;
    }
  } else result = -0xff;
L1:
  dbmclose();
  return(result);
}

#define LOC_MAX_OBJS  10000  /* should be big enough ;) */

static int loc_delete_obj(uint32 objid, int security)
/* delete's obj completely from bindery */
{
  int result = b_acc(objid, 0x33, 0x03); /* only supervisor or intern */
  if (result)
    return(result); /* no object delete priv */

  /* now we delete all properties of this object */
  (void)loc_delete_property(objid, (uint8*)"*", 0, 1);

  /* and now we delete all references of object in other set properties */
  if (!dbminit(FNPROP)){
    int     anz=0;
    uint32  objs[LOC_MAX_OBJS];
    uint8   props[LOC_MAX_OBJS];
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETPROP *prop=(NETPROP*)data.dptr;
	if (prop->flags & P_FL_SET) { /* is set property */
	  objs[anz]    = prop->obj_id;
	  props[anz++] = prop->id;
	  if (anz == LOC_MAX_OBJS) break;
	}
      }
    }
    while (anz--) /* now try to delete obj members */
      prop_delete_member(objs[anz], props[anz], objid);
  } else
    result=-0xff;
  dbmclose();
  if (!result) {
    NETOBJ obj;
    int filled=0;
    if (!dbminit(FNOBJ)){
      key.dptr  = (char*)&objid;
      key.dsize = NETOBJ_KEY_SIZE;
      data=fetch(key);
      if (data.dptr) {
        filled++;
        memcpy(&obj, data.dptr, sizeof(NETOBJ));
      }
      if (delete(key)) result = -0xff;
    } else result = -0xff;
    dbmclose();
    if (filled)
      handle_iobj(3, &obj); /* now delete iobj */;
  }
  return(result);
}

int nw_delete_obj(NETOBJ *obj)
{
  int result = find_obj_id(obj);
  XDPRINTF((2,0, "nw_delete_obj obj_id=%d, obj_name=%s", obj->id, obj->name));
  if (!result)
    result=loc_delete_obj(obj->id, obj->security);
  return(result);
}

int nw_rename_obj(NETOBJ *o, uint8 *newname)
/* rename object */
{
  int result = find_obj_id(o);
  if (!result) {
    result = b_acc(0, 0x33, 0x04); /* only supervisor */
    if (result) return(result); /* no obj rename priv */
    else result=-0xff;
    handle_iobj(3, o); /* delete old iobj */
    if (!dbminit(FNOBJ)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2,0, "rename_obj:got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
        strncpy(obj->name, newname, 48);
        if (!store(key, data)) {
          memcpy(o, obj, sizeof(NETOBJ));  /* for handle_iobj */
          result=0;
        }
      }
    }
    dbmclose();
    handle_iobj(2, o); /* creat new iobj */
  }
  return(result);
}

int nw_change_obj_security(NETOBJ *o, int newsecurity)
/* change Security of Object */
{
  int result = find_obj_id(o);
  if (!result) {
    result = b_acc(o->id, o->security, 0x05);
    if (result) return(result);
    else result=-0xff;
    if (!dbminit(FNOBJ)){
      key.dsize = NETOBJ_KEY_SIZE;
      key.dptr  = (char*)o;
      data      = fetch(key);
      if (data.dptr != NULL){
        NETOBJ *obj=(NETOBJ*)data.dptr;
        XDPRINTF((2,0, "change_obj_security:got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
        obj->security = (uint8) newsecurity;
        if (!store(key, data)) result=0;
      }
    }
    dbmclose();
  }
  return(result);
}

int nw_get_obj(NETOBJ *o)
{
  int result = -0xfc; /*  no Object */
  if (!dbminit(FNOBJ)){
    key.dsize = NETOBJ_KEY_SIZE;
    key.dptr  = (char*)o;
    data      = fetch(key);
    if (data.dptr != NULL){
      NETOBJ *obj=(NETOBJ*)data.dptr;
      result = b_acc(o->id, obj->security, 0x0);
      XDPRINTF((2,0, "got OBJ name=%s, id = 0x%x", obj->name, (int)obj->id));
      if (!result) memcpy(o, data.dptr, sizeof(NETOBJ));
    }
  } else result = -0xff;
  dbmclose();
  XDPRINTF((2,0, "nw_get_obj von OBJ id = 0x%x, result=0x%x",
           (int)o->id, result));
  return(result);
}

static int find_prop_id(NETPROP *p, uint32 obj_id, int last_prop_id)
{
  int result = -0xfb; /* no Property */
  XDPRINTF((2,0, "find Prop id of name=0x%x:%s, lastid=%d",
           obj_id, p->name, last_prop_id));
  if (!dbminit(FNPROP)){
    int  flag = (last_prop_id) ? 0 : 1;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
        if (!flag) flag = (last_prop_id == prop->id);
        else {
	  data = fetch(key);
	  prop = (NETPROP*)data.dptr;
	  if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	    XDPRINTF((2,0, "found PROP %s, id=0x%x", prop->name, (int) prop->id));
	    result = 0;
	    memcpy(p, prop, sizeof(NETPROP));
	    break;
	  }
        }
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

#define find_first_prop_id(p, obj_id)  \
        find_prop_id((p), (obj_id), 0)


static int loc_change_prop_security(NETPROP *p, uint32 obj_id)
{
  int result = -0xfb; /* no Property */
  XDPRINTF((2,0, "loc_change_prop_security prop id of name=0x%x:%s", obj_id, p->name));
  if (!dbminit(FNPROP)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *prop=(NETPROP*)key.dptr;
      if (prop->obj_id == obj_id) {
	data = fetch(key);
	prop = (NETPROP*)data.dptr;
	if (data.dptr != NULL  && name_match(prop->name, p->name) )  {
	  uint8 security = p->security;
	  XDPRINTF((2,0, "found PROP %s, id=0x%x", prop->name, (int) prop->id));
          result = b_acc(obj_id, prop->security, 0x15);
          if (!result) {
	    memcpy(p, prop, sizeof(NETPROP));
	    p->security = security;
	    data.dptr  = (char*)p;
	    data.dsize = sizeof(NETPROP);
	    key.dptr  = (char *)p;
	    key.dsize = NETPROP_KEY_SIZE;
	    if (store(key, data)) result=-0xff;
          }
	  break;
	}
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}


static int loc_get_prop_val(uint32 obj_id, int prop_id, int segment,
	         uint8 *property_value, uint8 *more_segments)
{
  int result = -0xec; /* no such Segment */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)segment;
    data        = fetch(key);
    if (data.dptr != NULL){
      NETVAL  *v = (NETVAL*)data.dptr;
      if (NULL != property_value) memcpy(property_value, v->value, 128);
      XDPRINTF((2,0, "found VAL 0x%x, %d, %d", obj_id, prop_id, segment));
      result = 0;
      val.segment++;
      data  = fetch(key);
      if (NULL != more_segments)
        *more_segments = (data.dptr != NULL) ? 0xff : 0;
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}


int prop_find_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = -0xea; /* no such member */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)1;
    data        = fetch(key);
    if (data.dptr != NULL){
      NETVAL  *v = (NETVAL*)data.dptr;
      uint8   *p=v->value;
      int     k=0;
      XDPRINTF((2,0, "found VAL 0x%x, %d", obj_id, prop_id));
      while (k++ < 32){
	uint32 id = GET_BE32(p);
	if (id == member_id) {
	  result = 0;
	  break;
	} else p += 4;
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int prop_add_member(uint32 obj_id, int prop_id, uint32 member_id)
{
  int result = 0; /* OK */
  NETVAL  val;
  if (!dbminit(FNVAL)){
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop_id;
    val.segment = (uint8)0;
    while (!result) {
      val.segment++;
      data        = fetch(key);
      if (data.dptr != NULL){
	NETVAL  *v = (NETVAL*)data.dptr;
	uint8   *p = v->value;
	int      k = 0;
	while (k++ < 32){
	  uint32 null_id = 0;
	  if (!memcmp(p, (char*)&null_id, 4)) {
	    U32_TO_BE32(member_id, p);
	    memcpy(&val, v, sizeof(NETVAL));
	    data.dptr = (char*)&val;
	    key.dptr  = (char*)&val;
	    if (store(key, data)) result=-0xff;
	    goto L1;
	  } else p += 4;
	}
      } else {
	memset(val.value, 0, 128);
	U32_TO_BE32(member_id, val.value);
	data.dptr  = (char*)&val;
	data.dsize = sizeof(NETVAL);
	if (store(key, data)) result=-0xff;
	goto L1;
      }
    } /* while */
  } else result = -0xff;
L1:
  dbmclose();
  return(result);
}


static int ins_prop_val(uint32 obj_id, NETPROP *prop, int segment,
	         uint8 *property_value, int erase_segments)
{
  int result = b_acc(obj_id, prop->security, 0x11);
  if (result) return(result);
  if (!dbminit(FNVAL)){
    NETVAL  val;
    int flag    = 1;
    key.dsize   = NETVAL_KEY_SIZE;
    key.dptr    = (char*)&val;
    val.obj_id  = obj_id;
    val.prop_id = (uint8)prop->id;
    result = -0xec; /* no such Segment */
    if (segment > 1) {
      val.segment = segment-1;
      data        = fetch(key);
      if (data.dptr == NULL) flag = 0;
    }
    if (flag){
      val.segment = segment;
      memcpy(val.value, property_value, 128);
      data.dsize = sizeof(NETVAL);
      data.dptr  = (char*)&val;
      if (!store(key, data)) {
	result = 0;
	if (erase_segments == 0xff){
	  val.segment++;
	  while (!delete(key)) val.segment++;
	}
      }
    }
  } else result = -0xff;
  dbmclose();
  return(result);
}

int nw_get_prop_val_by_obj_id(uint32 obj_id,
                              int segment_nr,
	        	      uint8 *prop_name, int prop_namlen,
	        	      uint8 *property_value,
	        	      uint8 *more_segments,
	        	      uint8 *property_flags)
{
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)prop.name, (char*)prop_name, prop_namlen);
  XDPRINTF((5,0, "nw_get_prop_val_by_obj_id,id=0x%x, prop=%s, segment=%d",
            obj_id, prop.name, segment_nr));

  if ((result=find_first_prop_id(&prop, obj_id))==0){
    if ((result=loc_get_prop_val(obj_id, prop.id, segment_nr,
	          property_value, more_segments)) == 0){
      *property_flags = prop.flags;
    }
  }
  return(result);
}

int nw_get_prop_val(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value,
	        uint8 *more_segments,
	        uint8 *property_flags)
{
  NETOBJ obj;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj)) == 0){
    result = nw_get_prop_val_by_obj_id(obj.id,
                              segment_nr,
	        	      prop_name, prop_namlen,
	        	      property_value,
	        	      more_segments,
	        	      property_flags);
  }
  return(result);
}

int nw_delete_property(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name,   int prop_namlen)
{
  NETOBJ  obj;
  uint8   prop_name_x[20];
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop_name_x, (char*)prop_name, prop_namlen);
  XDPRINTF((2,0, "nw_delete_property obj=%s, prop=%s, type=0x%x",
      obj.name, prop_name_x, object_type));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj)) == 0){
    result = loc_delete_property(obj.id, prop_name_x, 0, 0);
  }
  return(result);
}

int nw_is_obj_in_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_is_obj_in_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj);
    if (!result)
      result = prop_find_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}

int nw_add_obj_to_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_add_obj_to_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj);
    if (!result)
      result = prop_add_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}

int nw_delete_obj_from_set(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   member_type,
	        uint8 *member_name, int member_namlen)
{
  NETOBJ  obj;
  NETOBJ  mobj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)mobj.name,  (char*)member_name, member_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_delete_obj_from_set obj=%s,0x%x, member=%s,0x%x, prop=%s",
      obj.name, object_type, mobj.name, member_type, prop.name));
  obj.type    = (uint16) object_type;
  mobj.type   = (uint16) member_type;
  if ((result = find_obj_id(&obj)) == 0){
    result=find_first_prop_id(&prop, obj.id);
    if (!result)
      result = find_obj_id(&mobj);
    if (!result)
      result = prop_delete_member(obj.id, (int)prop.id,  mobj.id);
  }
  return(result);
}


int nw_write_prop_value(int object_type,
	        uint8 *object_name, int object_namlen,
	        int   segment_nr, int erase_segments,
	        uint8 *prop_name, int prop_namlen,
	        uint8 *property_value)
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_write_prop_value obj=%s, prop=%s, type=0x%x, segment=%d",
      obj.name, prop.name, object_type, segment_nr));
  obj.type    = (uint16) object_type;

  if ((result = find_obj_id(&obj)) == 0){
    if ((result=find_first_prop_id(&prop, obj.id))==0){
       result=ins_prop_val(obj.id, &prop, segment_nr,
	    property_value, erase_segments);

    }
  }
  return(result);
}


int nw_change_prop_security(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int   prop_security)
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  prop.security = (uint8)prop_security;
  XDPRINTF((2,0, "nw_change_prop_security obj=%s,0x%x, prop=%s",
      obj.name, object_type, prop.name));
  obj.type    = (uint16) object_type;
  if ((result = find_obj_id(&obj)) == 0)
    return(loc_change_prop_security(&prop, obj.id));
  return(-0xff);
}

int nw_scan_property(NETPROP *prop,
                     int      object_type,
    		     uint8    *object_name,
    		     int      object_namlen,
    		     uint8    *prop_name,
    		     int      prop_namlen,
    		     uint32   *last_scan)
{
  NETOBJ  obj;
  int     result;
  strmaxcpy((char*)obj.name,   (char*)object_name, object_namlen);
  strmaxcpy((char*)prop->name, (char*)prop_name,   prop_namlen);
  XDPRINTF((2,0, "nw_scan_property obj=%s, prop=%s, type=0x%x, last_scan=0x%x",
      obj.name, prop->name, object_type, (int)*last_scan));
  obj.type    = (uint16) object_type;

  if ((result = find_obj_id(&obj)) == 0){
    int last_prop_id;
    if (*last_scan == MAX_U32) *last_scan = 0;
    last_prop_id =  *last_scan;
    if ((result=find_prop_id(prop, obj.id, last_prop_id))==0){
      *last_scan = prop->id;
      if (!loc_get_prop_val(obj.id, prop->id,  1,
	                   NULL, NULL))
        result = 0xff; /* Has prop Values */
    }
  }
  return(result);
}

int nw_get_prop_val_str(uint32 q_id, char *propname, uint8 *buff)
/* for simple prop val strings */
{
  uint8  more_segments;
  uint8  property_flags;
  uint8  loc_buff[200];
  int    result;
  if (NULL == buff) buff=loc_buff;
  result=nw_get_prop_val_by_obj_id(q_id, 1, propname, strlen(propname),
                  buff, &more_segments, &property_flags);

  if (result > -1) {
    result=strlen(buff);
    XDPRINTF((5,0, "nw_get_prop_val_str:%s strlen=%d", propname, result));
  } else
    XDPRINTF((5,0, "nw_get_prop_val_str:%s, result=-0x%x", propname, -result));
  return(result);
}

int nw_create_obj(NETOBJ *obj, uint32 wanted_id)
/*
 * Routine creates object
 * wants OBJ name and OBJ-Type,  returns  obj.id.
 * if wanted_id > 0 and object don't exist then
 * wanted_id should be taken as obj_id.
*/
{
  int result = b_acc(0, 0x33, 0x02);
  XDPRINTF((2,0, "creat OBJ=%s,type=0x%x", obj->name, (int)obj->type));
  if (result) return(result); /* no object creat rights */
  if (!dbminit(FNOBJ)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr){
	NETOBJ *o=(NETOBJ*)data.dptr;
	if (o->type == obj->type && !strcmp(o->name, obj->name)){
	  obj->id =  o->id; /* fill ID       */
	  result  = -0xee;  /* already exist */
	  break;
	}
      }
    }
    if (!result){
      obj->id    = (wanted_id) ? wanted_id -1 : (obj->type << 16) + 1;
         /* 1 is reserved for supervisor !!!! */
      key.dsize  = NETOBJ_KEY_SIZE;
      key.dptr   = (char*)obj;
      while(1) {
	obj->id++;
	data = fetch(key);
	if (data.dptr == NULL) break;
      }
      data.dsize = sizeof(NETOBJ);
      data.dptr = (char*)obj;
      if (store(key, data)) result = -0xff;
    }
  } else result = -0xff;
  dbmclose();
  if (!result)
    handle_iobj(2, obj);
  return(result);
}

int nw_obj_has_prop(NETOBJ *obj)
{
  int result = (dbminit(FNPROP)) ? -0xff : 0;
  if (!result){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *p=(NETPROP*)key.dptr;
      if (p->obj_id == obj->id) {
	result = 1;
	break;
      }
    }
  }
  dbmclose();
  return(result);
}

static int nw_create_obj_prop(uint32 obj_id, NETPROP *prop)
{
  int result=0;
  if (!dbminit(FNPROP)){
    uint8   founds[256];
    memset((char*)founds, 0, sizeof(founds) );
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      NETPROP *p=(NETPROP*)key.dptr;
      if (p->obj_id == obj_id) {
	data = fetch(key);
	p  = (NETPROP*)data.dptr;
	if (data.dptr != NULL  && !strcmp(prop->name, p->name)){
	  prop->id     = p->id;
	  prop->obj_id = obj_id;
	  result       = -0xed;  /* Property exists */
	  break;
	} else founds[p->id]++;
      }
    }
    if (!result){
      int k = 0;
      while (++k < sizeof(founds) ) {
	if (!founds[k]) break;     /* free slot */
      }
      key.dsize     = NETPROP_KEY_SIZE;
      key.dptr      = (char *)prop;
      data.dsize    = sizeof(NETPROP);
      data.dptr     = (char *)prop;
      prop->obj_id   = obj_id;
      prop->id       = (uint8)k;
      if (store(key, data)) result = -0xff;
    }
  } else result = -0xff;
  dbmclose();
  XDPRINTF((3, 0, "create property='%s' objid=0x%x, result=0x%x",
          prop->name, obj_id, result));
  return(result);
}

int nw_create_prop(int object_type,
	        uint8 *object_name, int object_namlen,
	        uint8 *prop_name, int prop_namlen,
	        int prop_flags, int prop_security)
/* creats property for an object */
{
  NETOBJ  obj;
  NETPROP prop;
  int result=-0xff;
  strmaxcpy((char*)obj.name,  (char*)object_name, object_namlen);
  strmaxcpy((char*)prop.name, (char*)prop_name,   prop_namlen);
  obj.type    = (uint16) object_type;
  if (   0 == (result = find_obj_id(&obj))
     &&  0 == (result = b_acc(obj.id, obj.security, 0x12)) ) {
    prop.flags    = (uint8)prop_flags;
    prop.security = (uint8)prop_security;
    result        = nw_create_obj_prop(obj.id, &prop);
  }
  XDPRINTF((2,0, "nw_create_prop obj=%s, prop=%s, type=0x%x, result=0x%x",
      obj.name, prop.name, object_type, result));
  return(result);
}

static int nw_new_obj(uint32 *wanted_id,
                      char *objname, int objtype,
           	      int objflags, int objsecurity)
{
  NETOBJ obj;
  int result;
  xstrcpy(obj.name, objname);
  obj.type      =  (uint16) objtype;
  obj.flags     =  (uint8)  objflags;
  obj.security  =  (uint8)  objsecurity;
  obj.id        =  0L;
  result = nw_create_obj(&obj, *wanted_id);
  *wanted_id = obj.id;
  return(result);
}

uint32 nw_new_obj_prop(uint32 wanted_id,
                  char *objname, int objtype, int objflags, int objsecurity,
	          char *propname, int propflags, int propsecurity,
	          char *value, int valuesize, int ever)
/*
 * creats new property value, if needed creats Object
 * and the property,
 * if propname == NULL only object will be created.
 * if valuesize == 0, then only obj or property
 * will be created, returns obj->id
 */
{
  NETOBJ  obj;
  NETPROP prop;
  if (objname && *objname)
    nw_new_obj(&wanted_id, objname, objtype,
                         objflags, objsecurity);
  obj.id       = wanted_id;
  obj.security = objsecurity;
  if (propname && *propname && !b_acc(obj.id, obj.security, 0x12)) {
    int result;
    strmaxcpy(prop.name, propname, sizeof(prop.name));
    prop.flags    =   (uint8)propflags;
    prop.security =   (uint8)propsecurity;
    result=nw_create_obj_prop(obj.id, &prop);
    if (valuesize && (!result || (result == -0xed && ever)) ){
      uint8  locvalue[128];
      memset(locvalue, 0, sizeof(locvalue));
      memcpy(locvalue, value, min(sizeof(locvalue), valuesize));
      ins_prop_val(obj.id, &prop, 1, locvalue, 0xff);
    }
  }
  return(obj.id);
}

typedef struct {
  int    pw_uid;
  int    pw_gid;
  char   pw_passwd[80];
  uint8  pw_dir[257];
  uint8  pw_name[20];
} MYPASSWD;


static MYPASSWD *nw_getpwnam(uint32 obj_id)
{
  static MYPASSWD pwstat;
  char buff[200];
  if (nw_get_prop_val_str(obj_id, "UNIX_USER", buff) > 0){
    struct passwd *pw = getpwnam(buff);
    if (NULL != pw) {
      if (obj_id != 1 && !pw->pw_uid)
        return(NULL);  /* only supervisor -> root */
      pwstat.pw_uid = pw->pw_uid;
      pwstat.pw_gid = pw->pw_gid;
      xstrcpy(pwstat.pw_passwd, pw->pw_passwd);
      xstrcpy(pwstat.pw_name,   pw->pw_name);
      xstrcpy(pwstat.pw_dir,    pw->pw_dir);
#if SHADOW_PWD
      if (pwstat.pw_passwd[0] == 'x' && pwstat.pw_passwd[1]=='\0') {
        struct spwd *spw=getspnam(buff);
        if (spw) xstrcpy(pwstat.pw_passwd, spw->sp_pwdp);
      }
#endif
      XDPRINTF((2,0, "FOUND obj_id=0x%x, pwnam=%s, gid=%d, uid=%d",
                 obj_id, buff, pw->pw_gid, pw->pw_uid));
      return(&pwstat);
    }
  }
  XDPRINTF((2,0, "NOT FOUND PWNAM of obj_id=0x%x",  obj_id));
  return(NULL);
}

int get_guid(int *gid, int *uid, uint32 obj_id, uint8 *name)
/* searched for gid und uid of actual obj */
{
  MYPASSWD *pw = nw_getpwnam(obj_id);
  if (NULL != pw) {
    *gid    = pw->pw_gid;
    *uid    = pw->pw_uid;
    if (name) strmaxcpy(name, pw->pw_name, 20);
    return(0);
  } else {
    *gid = -1;
    *uid = -1;
    if (name) strcpy(name, "UNKNOWN");
    return(-0xff);
  }
}

int get_home_dir(uint8 *homedir,  uint32 obj_id)
/* searches for UNIX homedir of actual obj */
{
  MYPASSWD *pw = nw_getpwnam(obj_id);
  if (NULL != pw) {
    int len=strlen(pw->pw_dir);
    if (!len) {
      *homedir++ = '/';
      *homedir   = '\0';
      len =1;
    } else
      strmaxcpy(homedir, pw->pw_dir, min(255, len));
    return(len);
  } else {
    *homedir='\0';
    return(0);
  }
}

static int crypt_pw_ok(uint8 *password, char *passwd)
/* returns 0 if not ok */
{
  char pnul[2] = {'\0', '\0'};
  char *pp    = (password) ? (char*)password : pnul;
  char *p     =  crypt(pp, passwd);
  return( (strcmp(p, passwd)) ? 0 : 1 );
}

static int loc_nw_test_passwd(uint8 *keybuff, uint8 *stored_passwd,
                              uint32 obj_id, uint8 *vgl_key, uint8 *akt_key)
{
  if (nw_get_prop_val_str(obj_id, "PASSWORD", stored_passwd) > 0) {
    nw_encrypt(vgl_key, stored_passwd, keybuff);
    return (memcmp(akt_key, keybuff, 8) ? -0xff : 0);
  } else { /* now we build an empty password */
    uint8 buf[8];
    uint8 s_uid[4];
    U32_TO_BE32(obj_id, s_uid);
    shuffle(s_uid, buf, 0, stored_passwd);
    nw_encrypt(vgl_key, stored_passwd, keybuff);
    return(1);
  }
}


int nw_test_passwd(uint32 obj_id, uint8 *vgl_key, uint8 *akt_key)
/* returns 0, if password ok and -0xff if not ok */
{
  uint8 keybuff[8];
  uint8 stored_passwd[200];
  int result=loc_nw_test_passwd(keybuff, stored_passwd,
                                obj_id, vgl_key, akt_key);
  if (result < 1) return(result);

  if (obj_id == 1) return(-0xff);  /* SUPERVISOR */

  if (password_scheme & PW_SCHEME_LOGIN) {
    if (!(password_scheme & PW_SCHEME_ALLOW_EMPTY_PW)) {
      MYPASSWD *pw = nw_getpwnam(obj_id);
      if (pw && *(pw->pw_passwd) && !crypt_pw_ok(NULL, pw->pw_passwd))
        return(-0xff);
    }
  }
  return(0); /* no password */
}

int nw_test_unenpasswd(uint32 obj_id, uint8 *password)
{
  uint8 passwordu[100];
  uint8 passwd[200];
  uint8 stored_passwd[200];
  MYPASSWD *pw;
  if (password && *password
     && nw_get_prop_val_str(obj_id, "PASSWORD", stored_passwd) > 0 ) {
    uint8 s_uid[4];
    U32_TO_BE32(obj_id, s_uid);
    xstrcpy(passwordu, password);
    upstr(passwordu);
    shuffle(s_uid, passwordu, strlen(passwordu), passwd);
    if (!memcmp(passwd, stored_passwd, 16)) return(0);
  }
  if (NULL != (pw = nw_getpwnam(obj_id))) {
    int pwok = crypt_pw_ok(password, pw->pw_passwd);
    if (!pwok) {
      uint8 passwordu[100];
      xstrcpy(passwordu, password);
      downstr(passwordu);
      pwok = crypt_pw_ok(passwordu, pw->pw_passwd);
    }
    return((pwok) ? 0 : -0xff);
  } else return(-0xff);
}

static int nw_set_enpasswd(uint32 obj_id, uint8 *passwd, int dont_ch)
{
  uint8 *prop_name=(uint8*)"PASSWORD";
  if (passwd && *passwd) {
    if ((!dont_ch) || (nw_get_prop_val_str(obj_id, prop_name, NULL) < 1))
       nw_new_obj_prop(obj_id, NULL, 0, 0, 0,
  	                prop_name, P_FL_STAT|P_FL_ITEM,  0x44,
	                passwd, 16, 1);
  } else if (!dont_ch)
    (void)loc_delete_property(obj_id, prop_name, 0, 1);
  return(0);
}

int nw_set_passwd(uint32 obj_id, char *password, int dont_ch)
{
  if (password && *password) {
    uint8 passwd[200];
    uint8 s_uid[4];
    U32_TO_BE32(obj_id, s_uid);
    shuffle(s_uid, password, strlen(password), passwd);
    return(nw_set_enpasswd(obj_id, passwd, dont_ch));
  } else
    return(nw_set_enpasswd(obj_id, NULL, dont_ch));
}


/* main work from Guntram Blohm
 * no chance for unix password support here - can't get real password
 * from ncp request
 */
int nw_keychange_passwd(uint32 obj_id, uint8 *cryptkey, uint8 *oldpass,
			 int cryptedlen, uint8 *newpass, uint32 act_id)
/* returns 1 if new password is zero */
{
  uint8 storedpass[200];
  uint8 keybuff[8];
  char  buf[100];
  uint8 s_uid[4];

  int   len;
  int   result = loc_nw_test_passwd(keybuff, storedpass,
                                  obj_id, cryptkey, oldpass);

  XDPRINTF((5, 0, "Crypted change password: id=0x%x, oldpresult=0x%x",
                    (int)obj_id, result));

  len=(cryptedlen ^ storedpass[0] ^ storedpass[1])&0x3f;
  XDPRINTF((5, 0, "real len of new pass = %d", len));

  XDPRINTF((5, 0, "stored:  %s", hex_str(buf, storedpass,  16)));
  XDPRINTF((5, 0, "crypted: %s", hex_str(buf, keybuff,      8)));
  XDPRINTF((5, 0, "ncp old: %s", hex_str(buf, oldpass,      8)));

  if (result < 0) {     /* wrong passwd */
    if (1 == act_id) {  /* supervisor is changing passwd   */
      U32_TO_BE32(obj_id, s_uid);
      shuffle(s_uid, buf, 0, storedpass);
      nw_encrypt(cryptkey, storedpass, keybuff);
      len=(cryptedlen ^ storedpass[0] ^ storedpass[1])&0x3f;
      XDPRINTF((5, 0, "N real len of new pass = %d", len));
      XDPRINTF((5, 0, "N stored:  %s", hex_str(buf, storedpass,  16)));
      XDPRINTF((5, 0, "N crypted: %s", hex_str(buf, keybuff,  8)));
      if (memcmp(oldpass, keybuff, 8))
         return(-0xff);  /* if not BLANK then error */
    } else return(-0xff);
  }
  XDPRINTF((5, 0, "ncp new: %s", hex_str(buf,newpass,     16)));
  nw_decrypt_newpass(storedpass,   newpass,   newpass);
  nw_decrypt_newpass(storedpass+8, newpass+8, newpass+8);
  XDPRINTF((5, 0, "realnew: %s", hex_str(buf,newpass,     16)));
  nw_set_enpasswd(obj_id, newpass, 0);
  /* testing for zero password */
  U32_TO_BE32(obj_id, s_uid);
  shuffle(s_uid, buf, 0, storedpass);
  return(memcmp(newpass, storedpass, 16) ? 0 : 1);
}

static int nw_test_time_access(uint32 obj_id)
/* Routine from Matt Paley */
{
  time_t t;
  struct tm *tm;
  uint8  more_segments;
  uint8  property_flags;
  char   *propname="LOGIN_CONTROL";
  uint8  buff[200];
  int    segment = 1;
  int    half_hours;
  int    result=nw_get_prop_val_by_obj_id(obj_id, segment,
				   propname, strlen(propname),
				   buff, &more_segments, &property_flags);
  if (result < 0)
    return(0); /* No time limits available */
  time(&t);
  tm = localtime(&t);
  half_hours = tm->tm_wday*48 + tm->tm_hour*2 + ((tm->tm_min>=30)? 1 : 0);
  if ((buff[14+(half_hours/8)] & (1<<(half_hours % 8))) != 0)
    return(0);
  XDPRINTF((1, 0, "No access for user %x at day %d %02d:%02d",
	    obj_id, tm->tm_wday, tm->tm_hour, tm->tm_min));
  return(-0xda); /* unauthorized login time */
}

static int nw_test_adr_access(uint32 obj_id, ipxAddr_t *client_adr)
{
  uint8  more_segments;
  uint8  property_flags;
  char   *propname="NODE_CONTROL";
  uint8  buff[200];
  uint8  wildnode[]={0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  int    segment = 1;
  int    result=nw_get_prop_val_by_obj_id(obj_id, segment,
                  propname, strlen(propname),
                  buff, &more_segments, &property_flags);
  if (result < 0)
    return(0); /* we have no limits */
  else {
    uint8 *p=buff;
    int    k=0;
    while (k++ < 12) {
      if (  (IPXCMPNET(client_adr->net,   p))
        && ( (IPXCMPNODE(client_adr->node, p+4) )
           || (IPXCMPNODE(wildnode,        p+4))) )
        return(0);
      p+=10;
    }
  }
  XDPRINTF((1, 0, "No access for user %x at Station %s",
               obj_id, visable_ipx_adr(client_adr)));
  return(-0xdb); /* unauthorized login station */
}

int nw_test_adr_time_access(uint32 obj_id, ipxAddr_t *client_adr)
{
  int result;
  if (obj_id==1 && (entry8_flags & 8))
    return(0); /* no limits for SU */
  result=nw_test_adr_access(obj_id, client_adr);
  if (!result)
    result=nw_test_time_access(obj_id);
  return(result);
}

#if 0
int prop_add_new_member(uint32 obj_id, int prop_id, uint32 member_id)
/* add member to set, if member not in set */
{
  int result = prop_find_member(obj_id, prop_id, member_id);
  if (-0xea == result)
    return(prop_add_member(obj_id, prop_id, member_id));
  else if (!result) result = -0xee;  /* already exist */
  return(result);
}
#endif

static int nw_new_add_prop_member(uint32 obj_id, char *propname,
                                  int propflags, int propsecurity,
                                  uint32 member_id)
/* add member to set, if member not in set */
{
  NETPROP prop;
  int result;
  strmaxcpy(prop.name, propname, sizeof(prop.name));
  prop.flags    = (uint8) (propflags | P_FL_SET); /* always SET */
  prop.security = (uint8) propsecurity;
  result = nw_create_obj_prop(obj_id, &prop);
  if (!result || result == -0xed) {  /* created or exists */
    if (-0xea == (result=prop_find_member(obj_id, prop.id, member_id)))
      return(prop_add_member(obj_id, prop.id, member_id));
    else if (!result) result = -0xee;  /* already exist */
  }
  return(result);
}

static void create_nw_db(char *fn, int always)
{
  char   fname[200];
  struct stat stbuff;
  sprintf(fname, "%s/%s.dir", PATHNAME_BINDERY, fn);
  if (always || stat(fname, &stbuff)){
    int fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd > -1) close(fd);
  }
  chmod(fname, 0600);
  sprintf(fname, "%s/%s.pag", PATHNAME_BINDERY, fn);
  if (always || stat(fname, &stbuff)){
    int fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd > -1) close(fd);
  }
  chmod(fname, 0600);
}


static void add_pr_queue(uint32 q_id,
                         char *q_name, char *q_directory,
                         char *q_command,
                         uint32 su_id, uint32 ge_id)
{
  XDPRINTF((2,0, "ADD Q=%s, V=%s, C=%s", q_name, q_directory, q_command));
  q_id =
  nw_new_obj_prop(q_id, q_name,          0x3,  O_FL_DYNA,  0x31,
	             "Q_DIRECTORY",      P_FL_ITEM,   0x33,
	              q_directory,  strlen(q_directory), 1);

  /* this is mars_nwe own property to handle the print job !!! */
  nw_new_obj_prop(q_id ,NULL,              0  ,   0  ,   0   ,
	             "Q_UNIX_PRINT",     P_FL_ITEM| P_FL_DYNA,   0x33,
	              q_command,  strlen(q_command), 1);

  nw_new_add_prop_member(q_id, "Q_USERS",      P_FL_STAT,  0x31,  ge_id);
  nw_new_add_prop_member(q_id, "Q_OPERATORS",  P_FL_STAT,  0x31,  su_id);
#if 0
  nw_new_obj_prop(q_id , NULL,             0  ,   0  ,   0   ,
	             "Q_SERVERS",             P_FL_SET,  0x31,
	              NULL,  0, 0);
#endif
}

static void add_user_to_group(uint32 u_id,  uint32 g_id)
{
  nw_new_add_prop_member(u_id, "GROUPS_I'M_IN",   P_FL_STAT,  0x31,  g_id);
  nw_new_add_prop_member(u_id, "SECURITY_EQUALS", P_FL_STAT,  0x32,  g_id);
  nw_new_add_prop_member(g_id, "GROUP_MEMBERS",   P_FL_STAT,  0x31,  u_id);
}

static void add_user_2_unx(uint32 u_id,  char  *unname)
{
  if (unname && *unname)
    nw_new_obj_prop(u_id, NULL,          0  ,   0  ,   0 ,
      	             "UNIX_USER",        P_FL_ITEM,    0x33,
	             (char*)unname,  strlen(unname), 1);
}

static void add_user_g(uint32 u_id,   uint32 g_id,
                  char   *name,  char  *unname,
                  char *password, int dont_ch)
{
                                      /*   Typ    Flags  Security */
  dont_ch = (nw_new_obj(&u_id,  name,              0x1  , 0x0,  0x31)
              && dont_ch);

  if (dont_ch) return;

  XDPRINTF((1, 0, "Add/Change User='%s', UnixUser='%s'",
     	       	  name, unname));

  add_user_to_group(u_id, g_id);
  add_user_2_unx(u_id, unname);
  if (password && *password) {
    if (*password == '-') *password='\0';
    nw_set_passwd(u_id, password, dont_ch);
  }
}

static void add_group(char *name,  char  *unname, char *password)
{
                                       /*   Typ    Flags  Security */
  uint32 g_id  = 0L;
  (void) nw_new_obj(&g_id,  name,       0x2  , 0x0,   0x31);
  if (unname && *unname)
    nw_new_obj_prop(g_id, NULL,                0  ,   0  ,   0 ,
      	             "UNIX_GROUP",         P_FL_ITEM,    0x33,
	             (char*)unname,  strlen(unname), 1);
}

static int xmkdir(char *unixname, int mode)
{
  char *p=unixname;
  while (NULL != (p=strchr(p+1, '/'))) {
    *p = '\0';
    if (!mkdir(unixname, mode))
       chmod(unixname, mode);
    *p='/';
  }
  if (!mkdir(unixname, mode)) {
    chmod(unixname, mode);
    return(0);
  }
  return(-1);
}

static int get_sys_unixname(uint8 *unixname, uint8 *sysname, uint8 *sysentry)
{
  uint8 optionstr[256];
  int   founds = sscanf((char*)sysentry, "%s %s %s",sysname, unixname, optionstr);
  if (founds > 1 && *unixname) {
    struct stat statb;
    int result = strlen(sysname);
    uint8 *pp  = unixname + strlen(unixname);
    if (*(sysname+result-1) == ':')
       *(sysname+result-1) = '\0';
    upstr(sysname);
    result=0;
    if (founds > 2) {
      uint8 *p;
      for (p=optionstr; *p; p++) {
        if (*p=='k') {
          result=1;
          break;
        }
      } /* for */
    } /* if */
    if (*(pp-1) != '/') *pp++ = '/';
    *pp     = '.';
    *(pp+1) = '\0';

    if (stat(unixname, &statb) < 0)
      xmkdir(unixname, 0777);

    if (stat(unixname, &statb) < 0 || !S_ISDIR(statb.st_mode)) {
      errorp(1, "No good SYS", "unix name='%s'", unixname);
      return(-1);
    }

    *pp  = '\0';
    return(result);
  } else return(-1);
}

static uint8 *test_add_dir(uint8 *unixname, uint8 *pp, int flags,
              int downshift, int permiss, int gid, int uid, char *fn)

/* flags & 1 = fn will be appended to unixname   */
/* flags & 2 = always modify uid/gid, permission */
{
  struct stat stb;
  strcpy((char*)pp, fn);
  if (downshift) downstr(pp);
  else upstr(pp);
  if (stat(unixname, &stb) < 0) {
    if (xmkdir(unixname, permiss)< 0)
      errorp(1, "mkdir error", "fname='%s'", unixname);
    else {
      if (uid >-1  && gid > -1)
        chown(unixname, uid, gid);
      XDPRINTF((1, 0, "Created dir '%s'", unixname));
    }
  } else if (flags&2) {
    chmod(unixname, permiss);
    if (uid >-1  && gid > -1)
      chown(unixname, uid, gid);
  }
  if (flags&1) {
    pp += strlen(pp);
    *pp++='/';
    *pp  = '\0';
  } else
    *pp='\0';
  return(pp);
}

void test_ins_unx_user(uint32 id)
{
  NETOBJ obj;
  obj.id = id;
  if ((MYPASSWD*)NULL == nw_getpwnam(id) && !nw_get_obj(&obj)){
    struct passwd *pw;
    uint8 unxname[50];
    xstrcpy(unxname, obj.name);
    downstr(unxname);
    pw = getpwnam(unxname);
    if (NULL != pw && pw->pw_uid)  /* only non root user */
       add_user_2_unx(id, unxname);
  }
}

int nw_fill_standard(char *servername, ipxAddr_t *adr)
/* fills the standardproperties */
{
  char   serverna[MAX_SERVER_NAME+2];
  uint32 su_id    = 0x00000001;
  uint32 ge_id    = 0x01000001;
  uint32 serv_id  = 0x03000001;
  uint32 q1_id    = 0x0E000001;
#ifdef _MAR_TESTS_1
  uint32 pserv_id = 0L;
#endif
  FILE *f	     = open_nw_ini();
  int  auto_ins_user = 0;
  char auto_ins_passwd[100];
  int  make_tests    = 1;
  char sysentry[256];
  sysentry[0] = '\0';
  ge_id = nw_new_obj_prop(ge_id, "EVERYONE",        0x2,   0x0,  0x31,
	                   "GROUP_MEMBERS",          P_FL_SET,  0x31,
	                      NULL, 0, 0);
  if (f){
    char buff[256];
    int  what;
    while (0 != (what =get_ini_entry(f, 0, (char*)buff, sizeof(buff)))) {
      if (1 == what && !*sysentry) {
        xstrcpy(sysentry, buff);
      } else if (6 == what) {  /* server Version */
        tells_server_version = atoi(buff);
      } else if (7 == what) {  /* password_scheme */
        int pwscheme     = atoi(buff);
        password_scheme  = 0;
        switch (pwscheme) {
          case  9 : password_scheme |= PW_SCHEME_GET_KEY_FAIL;
          case  8 : password_scheme |= PW_SCHEME_ALLOW_EMPTY_PW;
          case  7 : password_scheme |= PW_SCHEME_LOGIN;
          case  1 : password_scheme |= PW_SCHEME_CHANGE_PW;
                    break;
          default : password_scheme = 0;
                    break;
        } /* switch */

      } else if (8 == what) { /* entry8_flags */
        entry8_flags = hextoi((char*)buff);
      } else if (21 == what) {  /* QUEUES */
        char name[100];
        char directory[200];
        char command[200];
        char *p=buff;
        char *pp=name;
        char c;
        int  state=0;
        while (0 != (c = *p++)) {
          if (c == 32 || c == '\t') {
            if (!(state & 1)) {
              *pp = '\0';
              state++;
            }
          } else {
            if (state & 1){
              if (state == 1) {
                pp=directory;
                state++;
              } else {
                strcpy(command, p-1);
                if (*command) state++;
                break;
              }
            }
            *pp++ = c;
          }
        }
        *pp='\0';
        if (state == 4) {
          upstr(name);
          add_pr_queue(q1_id, name, directory, command, su_id, ge_id);
          q1_id++;
        }
      } else if (12 == what || 13 == what || 14 == what) {
        /* SUPERVISOR, OTHERS  and GROUPS*/
        char nname[100];
        char uname[100];
        char password[100];
        int  anz=sscanf((char*)buff, "%s %s %s", nname, uname, password);
        if (anz > 1) {
          upstr(nname);
          if (anz > 2) upstr(password);
          else password[0] = '\0';
          if (what == 14)
            add_group(nname, uname, password);
          else
            add_user_g((12 == what) ? su_id : 0L, ge_id, nname,
                            uname, password, 0);
        }
      } else if (15 == what) {
        char buf[100];
        int  anz=sscanf((char*)buff, "%s %s", buf, auto_ins_passwd);
        auto_ins_user = ((anz == 2) && atoi(buf) && *auto_ins_passwd);
        if (auto_ins_user) auto_ins_user = atoi(buf);
      } else if (16 == what) {
        make_tests = atoi(buff);
      }
    } /* while */
    fclose(f);
  }

  if (servername && adr) {
    strmaxcpy(serverna, servername, MAX_SERVER_NAME);
    upstr(serverna);
    nw_new_obj_prop(serv_id, serverna,       0x4,      O_FL_DYNA,  0x40,
	               "NET_ADDRESS",         P_FL_ITEM | P_FL_DYNA, 0x40,
	                (char*)adr,  sizeof(ipxAddr_t), 1);

#ifdef _MAR_TESTS_1
    nw_new_obj_prop(pserv_id, serverna,      0x47,    O_FL_DYNA, 0x31,
	               "NET_ADDRESS",     P_FL_ITEM | P_FL_DYNA, 0x40,
	                (char*)adr,  sizeof(ipxAddr_t), 1);
#endif
  }
  if (auto_ins_user) {
    /* here Unix users will be inserted automaticly as mars_nwe users */
    struct passwd *pw;
    upstr(auto_ins_passwd);
    while (NULL != (pw=getpwent())) {
      if (pw->pw_uid) {
        int do_add = ( (pw->pw_passwd[0]!= '*' && pw->pw_passwd[0]!='x')
                      || pw->pw_passwd[1] != '\0');
#if SHADOW_PWD
        /*
        tip from: Herbert Rosmanith <herp@wildsau.idv.uni-linz.ac.at>
        */
        if (!do_add) {
          struct spwd *sp=getspnam(pw->pw_name);
          if (sp) {
            if  (  ((sp->sp_pwdp[0] != '*' && sp->sp_pwdp[0] != 'x')
                      || sp->sp_pwdp[1] !='\0')
                      &&
                   ((sp->sp_pwdp[0] != 'N' && sp->sp_pwdp[1] != 'P')
                      || sp->sp_pwdp[2] != '\0') )
               do_add++;
#if 0
            XDPRINTF((1,0, "Shadow pw of %s = `%s`", pw->pw_name, sp->sp_pwdp));
#endif
          } else {
            XDPRINTF((1,0, "cannot read shadow password"));
          }
        }
#endif
        if ( do_add) {
          char nname[100];
          xstrcpy(nname, pw->pw_name);
          upstr(nname);
          add_user_g(0L, ge_id, nname, pw->pw_name, auto_ins_passwd,
            (auto_ins_user == 99) ? 0 : 99);
        } else {
          XDPRINTF((1,0, "Unix User:'%s' not added because passwd='%s'",
             pw->pw_name, pw->pw_passwd));
        }
      } else {
        XDPRINTF((1,0, "Unix User:'%s' not added because uid=0 (root)",
                         pw->pw_name));
      }
    }
    endpwent();
  }
  if (*sysentry) {
    int result = 0;
    if (make_tests) {
      uint8 unixname[512];
      uint8 maildir[512];
      uint8 sysname[256];
      result = get_sys_unixname(unixname, sysname, sysentry);
      if (result > -1) {
        uint32  objs[2000]; /* max. 2000 User should be enough :) */
        int     ocount=0;
        int downshift = (result & 1);
        int    unlen  = strlen(unixname);
        uint8  *pp    = unixname+unlen;
        uint8  *ppp   = maildir+unlen;
        memcpy(maildir, unixname, unlen+1);

        test_add_dir(unixname,     pp, 0, downshift,0777, 0,0, "LOGIN");
        test_add_dir(unixname,     pp, 0, downshift,0777, 0,0, "SYSTEM");
        test_add_dir(unixname,     pp, 0, downshift,0777, 0,0, "PUBLIC");
        ppp=test_add_dir(maildir, ppp, 1, downshift,0777, 0,0, "MAIL");

        if (!dbminit(FNOBJ)){
          for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
            data = fetch(key);
            if (data.dptr) {
	      NETOBJ *obj=(NETOBJ*)data.dptr;
	      if (obj->type == 1 || obj->type == 3) {
	        objs[ocount++] = obj->id;
                if (ocount == 2000) break;
              }
            }
          }
        }
        dbmclose();

        while (ocount--) {
          NETOBJ obj;
          obj.id = objs[ocount];
          nw_get_obj(&obj);
          if (obj.type == 1) {
            char sx[20];
            int gid;
            int uid;
            sprintf(sx, "%x", (int)obj.id);
            if (!get_guid(&gid, &uid, obj.id, NULL))
              test_add_dir(maildir, ppp, 2, downshift, 0733, gid, uid, sx);
            else
              errorp(0, "Cannot get unix uid/gid", "User=`%s`", obj.name);

          } else if (obj.type == 3) { /* print queue */
            uint8 buff[300];
            char  *p;
            int result=nw_get_q_dirname(obj.id, buff);
            upstr(buff);
            if (result > -1 && NULL != (p=strchr(buff, ':')) ) {
              *p++='\0';
              if (!strcmp(buff, sysname)) {
                test_add_dir(unixname, pp, 2, downshift, 0770, 0, 0, p);
              } else
               errorp(0, "Warning:queue dir not on first SYS",
                  "Queue=%s, Volume=%s", obj.name, sysname);
            } else
              errorp(0, "Cannot get queue dir", "Queue=%s", obj.name);
          }
        }
        result = 0;
      }
    }
    return(result);
  }
  return(-1);
}

int nw_init_dbm(char *servername, ipxAddr_t *adr)
/*
 * routine inits bindery
 * all dynamic objects and properties will be deletet.
 * and the always needed properties will be created
 * if not exist.
 */
{
  int     anz=0;
  uint32  objs[LOC_MAX_OBJS];
  uint8   props[LOC_MAX_OBJS];

  create_nw_db(dbm_fn[FNOBJ],  0);
  create_nw_db(dbm_fn[FNPROP], 0);
  create_nw_db(dbm_fn[FNVAL],  0);

  create_nw_db(dbm_fn[FNIOBJ], 0);

  if (!dbminit(FNOBJ)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETOBJ *obj=(NETOBJ*)data.dptr;
	if ((obj->flags & O_FL_DYNA) || !obj->name[0]) {
	  /* dynamic or without name */
	  objs[anz++] = obj->id;
	  if (anz == LOC_MAX_OBJS) break;
        } else if (obj->type == 1 /* && obj->id != 1 */ && obj->security != 0x31) {
          /* this is for correcting wrong obj security */
          obj->security=0x31;
	  (void)store(key, data);
          XDPRINTF((1,0, "Correcting access obj_id=0x%x(%s)",
                  (int)obj->id, obj->name));
        }
      }
    }
  }
  dbmclose();
  while (anz--)
    loc_delete_obj(objs[anz], 0x44);  /* Now delete dynamic objects */
  anz = 0;
  if (!dbminit(FNPROP)){
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
	NETPROP *prop=(NETPROP*)data.dptr;
	if (prop->flags & P_FL_DYNA) { /* dynamic */
	  objs[anz]    = prop->obj_id;
	  props[anz++] = prop->id;
	  if (anz == LOC_MAX_OBJS) break;
	}
      }
    }
  }
  dbmclose();
  while (anz--) /* now delete dynamic properties */
    loc_delete_property(objs[anz], (char*)NULL, props[anz], 1);
  anz = nw_fill_standard(servername, adr);
  sync_dbm();
  return(anz);
}


#if 0
#define MAX_OBJ_IDS 100000 /* should be enough */
typedef struct {
  int     anz;
  uint32  obj_ids[MAX_OBJ_IDS];
}
#endif


static FILE *open_exp_file(char *path, int what_dbm, int mode)
/* mode 1 = export, 0 = import */
{
  char buf[300];
  char *err_str="open_exp_file";
  FILE *f;
  char *opmode=mode ? "w+" : "r";
  sprintf(buf, "%s/%s.exp", (path && *path) ? path : ".",
               dbm_fn[what_dbm] );
  if (NULL == (f=fopen(buf, opmode))) {
    errorp(0, err_str, "Open error `%s` mode=%s", buf, opmode);
  } else {
    if (!mode) {
      sync_dbm();
      create_nw_db(dbm_fn[what_dbm],  1);
    }
    if (!dbminit(what_dbm))
      return(f);
    else {
      errorp(0, err_str, "dbminit error `%s`", buf);
      fclose(f);
      dbmclose();
    }
  }
  return(NULL);
}



static int export_obj(char *path)
{
  int result = 1;
  FILE *f    = open_exp_file(path, FNOBJ, 1);
  if (f != NULL) {
    result=0;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
        NETOBJ *o=(NETOBJ*)data.dptr;
        fprintf(f, "0x%08x %-47s 0x%04x 0x%02x 0x%02x\n",
            (int) o->id,  o->name, (int) o->type,
            (int) o->flags, (int)o->security);
      }
    }
    fclose(f);
    dbmclose();
  }
  return(result);
}

static int export_prop(char *path)
{
  int result = 1;
  FILE *f    = open_exp_file(path, FNPROP, 1);
  if (f != NULL) {
    result=0;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
        NETPROP *p=(NETPROP*)data.dptr;
        fprintf(f, "0x%08x 0x%02x %-15s 0x%02x 0x%02x\n",
            (int) p->obj_id, (int)p->id, p->name,
            (int) p->flags, (int)p->security);
      }
    }
    fclose(f);
    dbmclose();
  }
  return(result);
}

static int export_val(char *path)
{
  int result = 1;
  FILE *f    = open_exp_file(path, FNVAL, 1);
  if (f != NULL) {
    result=0;
    for  (key = firstkey(); key.dptr != NULL; key = nextkey(key)) {
      data = fetch(key);
      if (data.dptr) {
        NETVAL *v=(NETVAL*)data.dptr;
        int k=128;
        uint8 *p=v->value;
        fprintf(f, "0x%08x 0x%02x 0x%02x ",
            (int) v->obj_id, (int)v->prop_id, (int) v->segment);
        while (k--) {
          fprintf(f, "%02x", (int)*p++);
        }
        fprintf(f, "\n");
      }
    }
    fclose(f);
    dbmclose();
  }
  return(result);
}

int do_export_dbm(char *path)
/* Builds ASCII export files */
{
  int result=export_obj(path);
  if (!result) result=export_prop(path);
  if (!result) result=export_val(path);
  sync_dbm();
  return(result);
}

static int import_obj(char *path)
{
  char *err_str="import_obj";
  int result=1;
  FILE *f = open_exp_file(path, FNOBJ, 0);
  if (f != NULL) {
    char   buff[300];
    int    line=0;
    result=0;
    while (fgets(buff, sizeof(buff), f) != NULL){
      NETOBJ obj;
      char   name[300];
      int    type;
      int    flags;
      int    security;
      int    obj_id;
      line++;
      if (sscanf(buff, "%x %s %x %x %x",
            &(obj_id),  name, &type,
            &flags, &security) == 5) {
        strmaxcpy(obj.name, name, 47);
        obj.id       = (uint32) obj_id;
        obj.type     = (uint16)type;
        obj.flags    = (uint8) flags;
        obj.security = (uint8) security;
        key.dsize  = NETOBJ_KEY_SIZE;
        key.dptr   = (char*)&obj;
        data.dsize = sizeof(NETOBJ);
        data.dptr  = (char*)&obj;
        if (store(key, data)) {
          errorp(0, err_str, "Cannot store `%s` type=0x%x",
             obj.name, (int)obj.type);
        }
      } else {
        errorp(0, err_str, "Wrong line=%d: `%s`",line, buff);
      }
    } /* while */
    XDPRINTF((0, 0, "%s:got %d lines", err_str, line));
    fclose(f);
    dbmclose();
  }
  return(result);
}

static int import_prop(char *path)
{
  char *err_str="import_prop";
  int result=1;
  FILE *f = open_exp_file(path, FNPROP, 0);
  if (f != NULL) {
    char   buff[300];
    int    line=0;
    result=0;
    while (fgets(buff, sizeof(buff), f) != NULL){
      NETPROP prop;
      int    id;
      char   name[300];
      int    obj_id;
      int    flags;
      int    security;
      line++;
      if (sscanf(buff, "%x %x %s %x %x",
            &(obj_id), &id,  name, &flags, &security) == 5) {
        prop.obj_id   = (uint32)obj_id;
        prop.id       = (uint8)id;
        strmaxcpy(prop.name, name, 15);
        prop.flags    = (uint8) flags;
        prop.security = (uint8) security;
        key.dsize  = NETPROP_KEY_SIZE;
        key.dptr   = (char*)&prop;
        data.dsize = sizeof(NETPROP);
        data.dptr  = (char*)&prop;
        if (store(key, data)) {
          errorp(0, err_str, "Cannot store `%s` obj_id=0x%x, prop_id=0x%x",
             prop.name, prop.obj_id, (int)prop.id);
        }
      } else {
        errorp(0, err_str, "Wrong line=%d: `%s`",line, buff);
      }
    } /* while */
    XDPRINTF((0, 0, "%s:got %d lines", err_str, line));
    fclose(f);
    dbmclose();
  }
  return(result);
}

static int import_val(char *path)
{
  char *err_str="import_val";
  int result=1;
  FILE *f = open_exp_file(path, FNVAL, 0);
  if (f != NULL) {
    char   buff[300];
    int    line=0;
    result=0;
    while (fgets(buff, sizeof(buff), f) != NULL){
      NETVAL val;
      int    prop_id;
      int    obj_id;
      int    segment;
      char   value[300];
      line++;
      if (sscanf(buff, "%x %x %x %s",
            &obj_id, &prop_id, &segment, value) == 4) {
        uint8 *p=val.value;
        uint8 *pp=value;
        char  smallbuf[3];
        int  k=128;
        smallbuf[2] = '\0';
        val.obj_id = (uint32) obj_id;
        while (k--) {
          int i;
          memcpy(smallbuf, pp, 2);
          pp+=2;
          sscanf(smallbuf, "%x", &i);
          *p++ = (uint8) i;
        }
        val.prop_id   = (uint8) prop_id;
        val.segment   = (uint8) segment;
        key.dsize     = NETVAL_KEY_SIZE;
        key.dptr      = (char*)&val;
        data.dsize    = sizeof(NETVAL);
        data.dptr     = (char*)&val;
        if (store(key, data)) {
          errorp(0, err_str, "Cannot store obj_id=0x%8x, prop_id=0x%x",
             (int)val.obj_id, (int)val.prop_id);
        }
      } else {
        errorp(0, err_str, "Wrong line=%d: `%s`",line, buff);
      }
    } /* while */
    XDPRINTF((0, 0, "%s:got %d lines", err_str, line));
    fclose(f);
    dbmclose();
  }
  return(result);
}

int do_import_dbm(char *path)
/* Imports ASCII export files */
{
  int result=import_obj(path);
  if (!result) result=import_prop(path);
  if (!result) result=import_val(path);
  return(result);
}

/* ============> this should becomes queue.c or similar < ============== */

int nw_get_q_dirname(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_DIRECTORY", buff));
}

int nw_get_q_prcommand(uint32 q_id, uint8 *buff)
{
  return(nw_get_prop_val_str(q_id, "Q_UNIX_PRINT", buff));
}
