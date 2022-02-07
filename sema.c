/* sema.c :07-Aug-97 */

/* simple semaphore emulation, needs more work */

/* (C)opyright (C) 1997 Martin Stover, Marburg, Germany
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
#include "nwbind.h"
#include "sema.h"

#define MAX_SEMAPHORES  100

typedef struct {
  int    namlen;
  uint8  *name;
  int    opencount;
  int    value;
} SEMA;

static int  count_semas=0;
static SEMA *semas[MAX_SEMAPHORES];

static int open_sema(int namlen, uint8 *name, int value, int *opencount)
{
  int  k=-1;
  int  isfree=-1;
  SEMA *se;
  while (++k < count_semas){
    se=semas[k];
    if (se) {
      if (se->namlen==namlen && !memcmp(se->name, name, namlen)) {
        *opencount=++(se->opencount);
        return(++k);
      }
    } else if (isfree<0) isfree=k;
  }
  if (isfree<0 && count_semas < MAX_SEMAPHORES)
    isfree=count_semas++;
  if (isfree < 0) return(-0x96); /* out of mem */
  se=semas[isfree]=(SEMA*)xcmalloc(sizeof(SEMA));
  se->namlen=namlen;
  se->name=xmalloc(namlen);
  memcpy(se->name, name, namlen);
  se->value=value;
  *opencount=se->opencount=1;
  return(++isfree);
}

static int close_sema(int handle)
{
 SEMA *se;
 if (handle > 0 && --handle < count_semas && NULL != (se=semas[handle])){
   if (!(--se->opencount)) {
     xfree(se->name);
     xfree(se);
     semas[handle]=NULL;
     while (handle == count_semas-1) {
       --count_semas;
       if (!handle || semas[--handle]) break;
     }
   }
   return(0);
 } 
 return(-0xff); /* wrong handle */
}

static int wait_sema(int handle, int timeout)
{
 SEMA *se;
 if (handle > 0 && --handle < count_semas && NULL != (se=semas[handle])){
   if (se->value < 1) return(-0xfe);  /* timeout */
   --se->value;
   return(0);
 } 
 return(-0xff); /* wrong handle */
}

static int signal_sema(int handle)
{
 SEMA *se;
 if (handle > 0 && --handle < count_semas && NULL != (se=semas[handle])){
   ++se->value;
   return(0);
 } 
 return(-0xff); /* wrong handle */
}

static int examine_sema(int handle, int *value, int *opencount)
{
 SEMA *se;
 if (handle > 0 && --handle < count_semas && NULL != (se=semas[handle])){
   *value=se->value;
   *opencount=se->opencount;
   return(0);
 } 
 return(-0xff); /* wrong handle */
}

static int open_conn_sema(CONNECTION *c, int handle)
{
  int k=c->count_semas;
  int is_free=-1;
  SEMA_CONN *sem;
  while (k--) {
    sem=&(c->semas[k]);
    if (sem->handle==handle) {
      sem->opencount++;
      return(handle);
    } else if (!sem->handle) is_free=k;
  }
  if (is_free < 0 && c->count_semas < MAX_SEMA_CONN) 
    is_free=c->count_semas++;
  if (is_free < 0)
    return(-0x96); /* we say out of mem here */
  sem=&(c->semas[is_free]);
  sem->handle=handle;
  sem->opencount=1;
  return(handle);
}

static int close_conn_sema(CONNECTION *c, int handle)
{
  int k=c->count_semas;
  SEMA_CONN *sem;
  while (k--) {
    sem=&(c->semas[k]);
    if (sem->handle==handle) {
      if (!--sem->opencount) {
        sem->handle=0;
        if (k==c->count_semas-1) {
          while(c->count_semas > 0 
            && !(c->semas[c->count_semas-1].handle) )
           --c->count_semas;
        }
      }
      return(0);
    } 
  }
  return(-0xff); /* wrong handle */
}

void clear_conn_semas(CONNECTION *c)
/* removes semas which are opened by this connection */
{
  while (c->count_semas--) {
    SEMA_CONN *sem=&(c->semas[c->count_semas]);
    if (sem->handle>0&&sem->opencount>0) {
      while (sem->opencount--) 
        close_sema(sem->handle);
    }
  }
  c->count_semas=0;
}

int handle_func_0x20(CONNECTION *c, uint8 *p, int ufunc, uint8 *responsedata)
{
  int result    = -0xfb;      /* unknown request                  */
  XDPRINTF((3, 0, "0x20 call ufunc=0x%x", ufunc));
  switch (ufunc) {         
    case 0x0 :  { /* open semaphore */
                  int value    = *p;
                  int namlen   = *(p+1);
                  uint8 *name  = (p+2);
                  struct XDATA {
                    uint8 handle[4]; 
                    uint8 opencount; 
                  } *xdata = (struct XDATA*) responsedata;
                  int opencount;
                  if (namlen < 1 || namlen > 127) return(-0xfe);
                  if (value  > 127) return(-0xff);
                  result=open_sema(namlen, name, value, &opencount);
                  if (result > -1) {
                    result=open_conn_sema(c, result);
                    if (result < 0){
                      close_sema(result);
                    }
                  }
                  if (nw_debug>1){
                    name[namlen]=0;
                    XDPRINTF((2, 0, "open_sem:`%s` value=%d, count=%d, handle=%d",
                        name, value, opencount,  result));
                  }
                  if (result > -1) {
                    U32_TO_BE32(result, xdata->handle);
                    xdata->opencount=(uint8)opencount;
                    result=sizeof(*xdata);
                  } 
                }
                break;
    
    case 0x1 :  { /* examine semaphore */
                  int handle   = GET_BE32(p);
                  struct XDATA {
                    char  value; 
                    uint8 opencount; 
                  } *xdata = (struct XDATA*) responsedata;
                  int value;
                  int opencount;
                  result=examine_sema(handle, &value, &opencount);
                  XDPRINTF((2, 0, "examine_sem:%d, value=%d, opcount=%d, result=%d", 
                      handle, value, opencount, result));
                  if (result > -1) {
                    xdata->value=(char)value;
                    xdata->opencount=(uint8)opencount;
                    result=sizeof(*xdata);
                  }
                }
                break;


    case 0x2 :  { /* wait on semaphore */
                  int handle   = GET_BE32(p);
                  int timeout  = GET_BE16(p+4);
                  result=wait_sema(handle, timeout);
                  XDPRINTF((2, 0, "wait_sem:%d, timeout=%d, result=%d", 
                     handle, timeout, result));
                }
                break;

    case 0x3 :  { /* signal sema */
                  int handle   = GET_BE32(p);
                  result=signal_sema(handle);
                  XDPRINTF((2, 0, "signal_sem:%d, result=%d", 
                      handle, result));
                }
                break;


    case 0x4 :  { /* close semaphore */
                  int handle   = GET_BE32(p);
                  result=close_conn_sema(c, handle);
                  if (!result)
                    result=close_sema(handle);
                  XDPRINTF((2, 0, "close_sem:%d, result=%d", 
                    handle, result));
                }
                break;

    default :  break;
  }
  return(result);
}


