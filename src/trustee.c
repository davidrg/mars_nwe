/* trustee.c 15-Apr-00 */
/* (C)opyright (C) 1998,2000  Martin Stover, Marburg, Germany
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

 /* Trusttee routines for mars_nwe */

/* history since 01-Jun-00 
 *
 * mst:01-Jun-00: removed SIG_SEGV in get_eff_rights_by_trustees(), 
 *                when stat error
 * mst:01-Sep-00: pcz:added real unix rights patch from Przemyslaw Czerpak
 * mst:07-Sep-00: corrected trustee rights of subdirs.
 * mst:07-Oct-00: corrected result from tru_eff_rights_exists()
 *                and un_nw_rights().
 *
 */


#include "net.h"
#include <dirent.h>
#include "unxfile.h"
#include "nwvolume.h"
#include "connect.h"
#include "trustee.h"

/* right access routines depending on unix rights */

static int un_nw_rights(int voloptions, uint8 *unixname, struct stat *stb)
/* returns eff rights of file/dir depending on unix rights only */
/* therefore only root gets TRUSTEE_S by this routine           */
{
  int rights=0xff; /* first all, pconsole needs TRUSTEE_O */
  int is_pipe_command=(voloptions & VOL_OPTION_IS_PIPE) 
                        && !S_ISFIFO(stb->st_mode);
  
  if (act_uid || is_pipe_command || (voloptions & VOL_OPTION_READONLY)) {
    int is_dir        = S_ISDIR(stb->st_mode);
    int norights      = TRUSTEE_A;  /* no access control rights */
    int acc; 
    int accp=0; 
    int isaccp=0;

    struct stat stbp;
    uint8 *p          = unixname+strlen(unixname);
    
    /* pcz:01-Sep-00 */
    if (voloptions & VOL_OPTION_UNX_RIGHT)
      acc=get_unix_access_rights(stb,unixname);
    else
      acc=get_unix_eff_rights(stb);

    memset(&stbp, 0, sizeof(struct stat));
    if (p > unixname){  /* now we must get parent rights */
      --p;
      while (p>unixname && *p=='/') --p; /* remove trailing slash */
      while (p>unixname && *p!='/') --p; /* search for slash */
      while (p>unixname && *p=='/') --p; /* and remove it */
      if (p > unixname) {  /* found subdir */
        *(p+1)='\0';
        if (stat(unixname, &stbp) || 
           (stbp.st_dev==stb->st_dev && stbp.st_ino==stb->st_ino)
            || !S_ISDIR(stbp.st_mode) ){
          /* something wrong here, clear rights */
          errorp(0,"un_nw_rights", "wrong path=%s", unixname);
        } else {  /* pcz:01-Sep-00 */
          if (voloptions & VOL_OPTION_UNX_RIGHT)
            accp=get_unix_access_rights(&stbp,unixname);
          else
            accp=get_unix_eff_rights(&stbp);
	  isaccp=1;
	}  
        *(p+1)='/';
      } else if (!stat("/.", &stbp)) {  /* pcz:01-Sep-00 */
        if (voloptions & VOL_OPTION_UNX_RIGHT)
          accp=get_unix_access_rights(&stbp,"/.");
        else
          accp=get_unix_eff_rights(&stbp);	
        isaccp=1;
      }
    }
    
    if (isaccp == 0) { /* pcz:01-Sep-00 */
      XDPRINTF((1,0, "no rights to parentdir of %s", unixname));
      norights=rights;
    } else {
      if (!(accp & X_OK))  
        norights=rights;
      else if (!(accp & W_OK)) {
        norights |= TRUSTEE_E; /* no erase right */
        norights |= TRUSTEE_M; /* no modify rights */
      }
      /* mst:07-Oct-00 */
      if ( (!is_dir) && !(accp & R_OK) )
        norights |= TRUSTEE_F;  /* no file scan rights */
    }
    
    if (voloptions & VOL_OPTION_READONLY) {
      norights |= TRUSTEE_E; /* no erase  right */
      norights |= TRUSTEE_M; /* no modify rights */
      norights |= TRUSTEE_C; /* no creat  rights */
    } else if ((!acc||is_dir||is_pipe_command) && !(acc&X_OK)) {
      norights = rights;
    } else if (is_pipe_command) {
      norights |= TRUSTEE_E; /* no erase  right */
      norights |= TRUSTEE_M; /* no modify rights */
      norights |= TRUSTEE_C; /* no creat  rights */
    }
    
    if (!(acc & 0x30)){  /* if not user and not in groups */
      norights |= TRUSTEE_M; /* no modify rights */
    }

    if (!(acc & W_OK)) {
      if (!(acc & 0x10) || (stb->st_uid == default_uid)){
        norights |= TRUSTEE_M; /* no modify rights */
      }
      norights   |= TRUSTEE_E; /* no erase  */
      norights   |= TRUSTEE_C; /* no creat  */
      norights   |= TRUSTEE_W; /* no write  */ 
    }

    if (!(acc & R_OK)) {
      norights |= TRUSTEE_R;  /* No read rights for files */
      if (is_dir)
        norights |= TRUSTEE_F;  /* no scan rights  */
    }

    rights &= (~norights);
  } else 
    rights |= TRUSTEE_S;  /* Root always has all access rights */
  return(rights);
}

#define MAX_TRUSTEES      100  /* max. trustee entries for one file/dir */
#define MAX_TRUSTEE_CACHE  50  /* max. trusttees in cache */

typedef struct {
  int     trustee;
  uint32  id;
} IDS_TRUSTEE;

typedef struct {
  int volume;
  int dev;
  int inode;
  int idle;           /* idle state   */
  
  int mode_flags;     /* 
                       * &0x01 is directory      
                       * &0x02 is_root      
                       * &0x04 is_symlink  
                       * &0x08 dev changed, will be set by trustees scan 
                       * &0x10 dev differs from volume's dev
                       *       will be set by trustees scan
                       *       is important to prevent trustee changes by 
                       *       normal user (not user 'root')
                       */
  int inherited_mask; /* for all users */
  int eff_rights;     /* for actual user */
  
  /* trustees for this node */
  int trustee_count;
  IDS_TRUSTEE *trustees;
} FILE_TRUSTEE_NODE;

typedef struct {
  int  count;
  FILE_TRUSTEE_NODE *tr[MAX_TRUSTEE_CACHE];
} TRUSTEE_CACHE;

static TRUSTEE_CACHE *tr_cache=NULL;

static void free_trustee_node(FILE_TRUSTEE_NODE *tn)
{
  if (tn) {
    xfree(tn->trustees);
    xfree(tn);
  }
}

void tru_free_cache(int volume)
/* free's cache for one volume or all volume's if volume == -1 */
{
  if (tr_cache) {
    int i=tr_cache->count;
    while(i--) {
      FILE_TRUSTEE_NODE *tr=tr_cache->tr[i];
      if (tr && (volume == -1 || tr->volume == volume)){
        free_trustee_node(tr);
        tr_cache->tr[i]=NULL;
        if (i+1 == tr_cache->count)
          --tr_cache->count;
      }
    }
  }
  if (volume == -1)
    xfree(tr_cache);
}

static void add_trustee_node(FILE_TRUSTEE_NODE *trn)
{
  if (trn) {
    int i;
    int max_idle   =  0;
    int found_idle = -1;
    int to_use     = -1;
    if (!tr_cache) 
      tr_cache=(TRUSTEE_CACHE*)xcmalloc(sizeof(TRUSTEE_CACHE));
    for (i=0;i < tr_cache->count; i++) {
      FILE_TRUSTEE_NODE *tr=tr_cache->tr[i];
      if (!tr) { 
        if (to_use < 0) to_use=i;
      } else {
        if (tr->mode_flags&1) tr->idle++; /* dirs should not become idle so fast */
        else tr->idle+=10;          /* as files */
        if (tr->idle > max_idle) {
          found_idle=i;
          max_idle=tr->idle;
        }
      }
    }
    if (to_use < 0) {
      if (tr_cache->count < MAX_TRUSTEE_CACHE) 
        to_use=tr_cache->count++;
      else {
        to_use=found_idle;
        free_trustee_node(tr_cache->tr[to_use]);
      }
    }
    tr_cache->tr[to_use]=trn;
  }
}

static FILE_TRUSTEE_NODE *find_trustee_node(int volume, int dev, int inode)
{
  if (vol_trustees_were_changed(volume)) 
    return(NULL);
  if (tr_cache) {
    int i=-1;
    while (++i < tr_cache->count) {
      FILE_TRUSTEE_NODE *tr=tr_cache->tr[i];
      if (tr && tr->volume == volume && tr->dev == dev && tr->inode == inode){
        tr->idle=0;
        return(tr);
      }
    }
  }
  return(NULL);
}

static int find_id_trustee(FILE_TRUSTEE_NODE *tr, uint32 id)
{
  int i=0;
  IDS_TRUSTEE *ids=tr->trustees;
  while (i++ < tr->trustee_count) {
    if (ids->id == id)   
      return(ids->trustee);
    ids++;
  }
  return(-1); /* not found */
}

static int     grps_count=0;
static uint32 *grps_grps=NULL;

static int cmp_uint32(const void *e1, const void *e2)
{
  if (*((uint32*)e1) < *((uint32*)e2)) return(-1);
  if (*((uint32*)e1) > *((uint32*)e2)) return(1);
  return(0);
}

static int grp_exist(uint32 grp_id)
/* returns 1 if grp_id exist */
{
  return( (NULL == bsearch(&grp_id, grps_grps, 
             (size_t)grps_count, (size_t)sizeof(uint32), cmp_uint32))
         ? 0 : 1);
}

void tru_init_trustees(int count, uint32 *grps)
/* must be called after new loging */
{
  tru_free_cache(-1);
  xfree(grps_grps);
  grps_count=count;
  if (count) {
    grps_grps=(uint32*)xmalloc(sizeof(uint32) * count);
    memcpy(grps_grps, grps, sizeof(uint32) * count);
    qsort(grps_grps, (size_t)grps_count, (size_t)sizeof(uint32), cmp_uint32);
  }
}

static void creat_trustee_path(int volume, int dev, ino_t inode, uint8 *path)
/* is always called with uid = 0 */
{
  char   buf[256];
  uint8  buf_uc[4];
  char   volname[100];
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) return;
  U32_TO_BE32(inode, buf_uc);
  slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/n.%x", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  unlink(buf);
  if (symlink(path, buf)) {
    XDPRINTF((0,0,"creat_trustee_path buf=`%s`, path=`%s` failed", buf, path));
  }
}

static int put_trustee_to_disk(int volume, int dev, ino_t inode, uint32 id, int trustee)
/* is always called with uid = 0 */
/* if id=0, it means inherited_mask */
{
  char   buf[256];
  char   btrustee[256];
  int    l;
  uint8  buf_uc[4];
  char   volname[100];
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) return(-0xff);
  U32_TO_BE32(inode, buf_uc);
  l=slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/t.%x", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  unx_xmkdir(buf, 0755);
  slprintf(buf+l, sizeof(buf) -l -1,  "/%x", (unsigned int) id);
  unlink(buf);
  l=slprintf(btrustee, sizeof(btrustee)-1, "%04x", (unsigned int) trustee);
  return(symlink(btrustee, buf) ? -0xff : 0);
}

static int get_trustee_from_disk(int volume, int dev, ino_t inode, uint32 id, int *trustee)
/* 
 * if id=0, it means inherited_mask 
 * return 0 if 0, < 0 if error
 */
{
  char   buf[256];
  char   btrustee[256];
  int    l;
  uint8  buf_uc[4];
  char   volname[100];
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) 
    return(-0xff);
  U32_TO_BE32(inode, buf_uc);
  slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/t.%x/%x", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3],
            (unsigned int)id);
  l = readlink(buf, btrustee, 254);
  if (l > 0) {
    unsigned int utrustee=0;
    btrustee[l]='\0';
    if (1 == sscanf(btrustee, "%x", &utrustee)) {
      *trustee = (int)utrustee;
      return(0);
    }
  }
  return(-0xff);
}

static int del_trustee_from_disk(int volume, int dev, ino_t inode, uint32 id)
/* removes users id trustee */
{
  char   buf[256];
  int    result=-0xfe; /* no such trustee */
  uint8  buf_uc[4];
  char   volname[100];
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) return(result);
  U32_TO_BE32(inode, buf_uc);
  slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/t.%x/%x", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3],
            (unsigned int)id);
  seteuid(0);
  if (!unlink(buf)) 
    result=0;
  reseteuid();
  return(result);
}

unsigned int tru_vol_sernum(int volume, int mode)
/* mode == 0, reads sernum, else change sernum, returns new sernum */
{
  char   volname[100];
  char   buf[256];
  char   buf1[20];
  int    len;
  unsigned int sernum=0;
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) return(-1);
  slprintf(buf, sizeof(buf)-1,  "%s/%s/ts", path_trustees, volname);
  len=readlink(buf, buf1, sizeof(buf1)-1);
  if (len>0) {
    buf1[len]='\0';
    if (1!=sscanf(buf1,"%x", &sernum))
      sernum=0;
  }
  if (mode) {
    if (++sernum==MAX_U32) sernum=1;
    seteuid(0);
    unlink(buf);
    slprintf(buf1, sizeof(buf1)-1, "%x", sernum);
    if (symlink(buf1, buf)) 
      errorp(0, "rw_trustee_sernum", "symlink %s %s failed", buf1, buf);
    reseteuid();
    tru_free_cache(volume);
  }
  return(sernum);
}

void tru_free_file_trustees_from_disk(int volume, int dev, ino_t inode)
/* is called if directory/file is removed */
{
  char   buf[256];
  uint8  buf_uc[4];
  int    len;
  char   volname[100];
  if (nw_get_volume_name(volume, volname, sizeof(volname) ) < 1) return;
  U32_TO_BE32(inode, buf_uc);
  len=slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2]);
  slprintf(buf+len, sizeof(buf) - len -1,  "t.%x", (int)buf_uc[3]);
  seteuid(0);
  unx_xrmdir(buf);
  /* now we remove the name of the dir/file */
  slprintf(buf+len, sizeof(buf) -len -1,  "n.%x", (int)buf_uc[3]);
  unlink(buf); 
  reseteuid();
}

int tru_del_trustee(int volume, uint8 *unixname, struct stat *stb, uint32 id)
{
  int result=-0x85; /* we say no privileges */
  int voloptions     = get_volume_options(volume);
  if (  (voloptions & VOL_OPTION_TRUSTEES) && 
        ( (tru_get_eff_rights(volume, unixname, stb) & TRUSTEE_A) 
     || (act_id_flags&1)) )  {
    result=del_trustee_from_disk(volume, stb->st_dev, stb->st_ino, id);
    if (!result)
      tru_vol_sernum(volume, 1);  /* trustee sernum needs updated */
  }
  MDEBUG(D_TRUSTEES, {
    xdprintf(1,0, "tru_del_trustee: id=%08lx, volume=%d, file=`%s`, result=-0x%x", 
      id, volume, unixname, -result);
  })
  return(result); 
}

static FILE_TRUSTEE_NODE *create_trustee_node(int volume, int dev, 
                          ino_t inode, int mode_flags)
/* 
 * mode_flags: &1=directory, &2=root, &4=symlink, &0x8 dev changes
 * &0x10 dev differs from volumes dev.
 */
{
  char   buf[256];
  int    l;
  uint8  buf_uc[4];
  DIR    *d;
  char   volname[100];
  FILE_TRUSTEE_NODE *tr = (FILE_TRUSTEE_NODE*)xcmalloc(sizeof(FILE_TRUSTEE_NODE));
  tr->volume            =  volume;
  tr->dev               =  dev;
  tr->inode             =  inode;
  tr->mode_flags        =  mode_flags;
  tr->inherited_mask    =  (mode_flags&0xe) 
                           ? 0       /* root dir or symlink no rights */
                           : MAX_TRUSTEE_MASK; /* default all allowed */

  tr->eff_rights        = -1;     /* not yet set  */
  U32_TO_BE32(inode, buf_uc);
  
  (void)nw_get_volume_name(volume, volname, sizeof(volname) );
  
  l=slprintf(buf, sizeof(buf)-1, "%s/%s/%x/%x/%x/%x/t.%x", path_trustees, volname,
            dev,
            (int) buf_uc[0],
            (int) buf_uc[1],
            (int) buf_uc[2],
            (int) buf_uc[3]);
  
  if (NULL != (d= opendir(buf)) ) {
    uint8 *p=buf+l;
    struct      dirent *dirbuff;
    int         trustee_count=0;
    IDS_TRUSTEE trustees[MAX_TRUSTEES];
    *p++ = '/';
    while (trustee_count < MAX_TRUSTEES &&
          (dirbuff = readdir(d)) != (struct dirent*)NULL){
      if (dirbuff->d_ino && dirbuff->d_name[0] != '.') {
        char         btrustee[255];
        int          len;
        unsigned int id;
        if (1 == sscanf(dirbuff->d_name, "%x", &id)) {
          len = (int)(p - (uint8*)buf);
          strmaxcpy(p, dirbuff->d_name, sizeof(buf) - len -1);
          len=readlink(buf, btrustee, 254);
          if (len > 0) {
            unsigned int utrustee=0;
            btrustee[len]='\0';
            if (1 == sscanf(btrustee, "%x", &utrustee)) {
              if (id) {
                trustees[trustee_count].id      = (uint32) id;
                trustees[trustee_count].trustee = (int) utrustee;
                trustee_count++;
              } else
                tr->inherited_mask=(int)utrustee;
            }
          }
        }
      }
    } /* while */
    closedir(d);
    tr->trustee_count=trustee_count;
    if (trustee_count) {
      tr->trustees=(IDS_TRUSTEE*)xcmalloc(sizeof(IDS_TRUSTEE)*trustee_count);
      while (trustee_count--){
        tr->trustees[trustee_count].id      = trustees[trustee_count].id;
        tr->trustees[trustee_count].trustee = trustees[trustee_count].trustee;
      }
    }
  }
  return(tr);
}

static FILE_TRUSTEE_NODE *find_creat_add_trustee_node(
                   int volume, uint8 *unixname, struct stat *stb)
{
  FILE_TRUSTEE_NODE *tr=find_trustee_node(volume, stb->st_dev, stb->st_ino);
  if (!tr) {
    struct stat lstatbuf;
    int mode_flags=S_ISDIR(stb->st_mode) ? 1:0;
    if (  lstat(unixname, &lstatbuf)  
       || (lstatbuf.st_dev != stb->st_dev)
       || (lstatbuf.st_ino != stb->st_ino)
       || S_ISLNK(lstatbuf.st_mode) ) {
      mode_flags|=4;  
    }
    tr=create_trustee_node(volume, stb->st_dev, stb->st_ino, mode_flags);
    add_trustee_node(tr);
  }  
  return(tr);
}

int tru_get_id_trustee(int volume, uint8 *unixname, struct stat *stb, uint32 id)
/* is called by vol_trustee_scan */
{
  int voloptions=get_volume_options(volume);
  if (voloptions&VOL_OPTION_TRUSTEES){ 
    FILE_TRUSTEE_NODE *tr=find_creat_add_trustee_node(volume, unixname, stb);
    return(find_id_trustee(tr, id));
  }
  return(-0x85); 
}

static FILE_TRUSTEE_NODE *find_build_trustee_node(int volume, uint8 *unixname, struct stat *stb);

static int local_tru_add_trustee_set(int volume, uint8 *unixname, 
                       struct stat *stb,
                       int count, NW_OIC *nwoic) 
{
  int voloptions     = get_volume_options(volume);
  int own_eff_rights;
  int result=-0x85; /* we say no privileges */
  if (  (voloptions & VOL_OPTION_TRUSTEES) && 
     (  ((own_eff_rights=tru_get_eff_rights(volume, unixname, stb)) & TRUSTEE_A) 
     || (act_id_flags&1) ))  {
    /* FILE_TRUSTEE_NODE *tr=find_trustee_node(volume, stb->st_dev, stb->st_ino); */
    /* mst:11-May-00 */
    FILE_TRUSTEE_NODE *tr=find_build_trustee_node(volume, unixname, stb); 
    if (tr && (!(tr->mode_flags&0x18) || !act_uid)) {
      int volumenamelen = get_volume_unixnamlen(volume);
      uint8  ufnbuf[2];
      uint8  *ufn;
      seteuid(0);
      while (count--) {
        if (! ((own_eff_rights & TRUSTEE_S) || (act_id_flags&1)) ) {
          /* only user with TRUSTEE_S are allowed to set TRUSTEE_S */
          if (nwoic->trustee&TRUSTEE_S)
            nwoic->trustee&=~TRUSTEE_S;
        }
        result=put_trustee_to_disk(volume, stb->st_dev, stb->st_ino, nwoic->id, nwoic->trustee);
        MDEBUG(D_TRUSTEES, {
          xdprintf(1,0, "tru_add_trustee_set: id=%08lx, trustee=0x%04x, volume=%d, file=`%s`, result=-0x%x", 
              nwoic->id, nwoic->trustee, volume,  unixname, -result);
        })
        if (result){ 
          reseteuid();
          goto func_err;
        }
        nwoic++;
      }
      ufn=unixname+min(strlen(unixname), volumenamelen);
      if (!*ufn) { /* is volume direct */
        ufn=ufnbuf;
        *ufn='.';
        *(ufn+1)='\0';
      }
      creat_trustee_path(volume, stb->st_dev, stb->st_ino, ufn);
      reseteuid();
#if 0  /* now in tru_add_trustee_set */    
      tru_vol_sernum(volume, 1);  /* trustee sernum needs updated */
#endif      
      return(0);
    }
  }
func_err:
  XDPRINTF((1,0, "user %08x tried to add trustees to %s, result=-0x%x", 
             act_obj_id, unixname, -result));
  tru_free_cache(-1);
  return(result); /* we say no privileges */
}


int tru_add_trustee_set(int volume, uint8 *unixname, 
                       struct stat *stb,
                       int count, NW_OIC *nwoic) 
{
  int result = local_tru_add_trustee_set(volume, unixname, stb, count, nwoic); 
  if (!result) {  /* mst: 13-Apr-00 */
    int len    = strlen(unixname);
    int vollen = get_volume_unixnamlen(volume);
    char *p    = unixname+len;
    char *volp = unixname+vollen;
    
    seteuid(0);
    while (--p > volp) {
      if (*p == '/') {
        struct stat statb;
        *p='\0';
        if (!stat(unixname, &statb)){
          int i;
          NW_OIC *poic=nwoic;
          for (i=0; i < count; i++) {
            int trustee = 0;
            if (poic->id) {
              get_trustee_from_disk(volume, statb.st_dev, statb.st_ino, poic->id, &trustee);
              if ( !(trustee & (TRUSTEE_T|TRUSTEE_F)) ) {
                trustee |= TRUSTEE_T;
                put_trustee_to_disk(volume, statb.st_dev, statb.st_ino, poic->id, trustee);
              }
            }
            poic++;
          }
        }
        *p='/';
      }
    }
    reseteuid();
    tru_vol_sernum(volume, 1);  /* trustee sernum needs updated */
  }
  return (result);
}

int tru_get_trustee_set(int volume, uint8 *unixname, 
                       struct stat *stb,
                       int sequence,
                       int maxcount, uint32 *ids, int *trustees) 
{
  int voloptions = get_volume_options(volume);
  if (voloptions & VOL_OPTION_TRUSTEES) {
    int offset     = sequence*maxcount;
    int count      = 0;
    FILE_TRUSTEE_NODE *tr=find_creat_add_trustee_node(volume, unixname, stb);
    while (offset < tr->trustee_count && count < maxcount) {
      *ids=tr->trustees[offset].id;
      ids++;
      *trustees=tr->trustees[offset].trustee;
      trustees++;
      offset++;
      count++;
    }
    if (count) return(count); 
  }
  return(-0x9c);  /* no more trustees */
}

int tru_set_inherited_mask(int volume, uint8 *unixname, 
                       struct stat *stb, int new_mask)
/* sets inherited mask of directory */
{
  int voloptions = get_volume_options(volume);
  if (  (voloptions & VOL_OPTION_TRUSTEES) && 
        ( (tru_get_eff_rights(volume, unixname, stb) & TRUSTEE_A) 
     || (act_id_flags&1)) )  {
    FILE_TRUSTEE_NODE *tr=find_trustee_node(volume, stb->st_dev, stb->st_ino);
    if (tr && (!(tr->mode_flags&0x1e) || !act_uid)) {
      int result;
      seteuid(0);
      result=put_trustee_to_disk(volume, stb->st_dev, stb->st_ino, 0L, new_mask);
      reseteuid();
      if (!result)
        tru_vol_sernum(volume, 1);  /* trustee sernum needs updated */
      return(result);
    }
  }
  return(-0x85); /* we say no privileges */
}

int tru_get_inherited_mask(int volume, uint8 *unixname, 
                       struct stat *stb)
/* returns inherited mask of directory */
{
  int voloptions = get_volume_options(volume);
  if (voloptions & VOL_OPTION_TRUSTEES){ 
    FILE_TRUSTEE_NODE *tr=find_creat_add_trustee_node(volume, unixname, stb);
    return(tr->inherited_mask);
  }
  return(0x01ff); /* default */
}

static int insert_ugid_trustee(IDS_TRUSTEE *ugid_trustees, int count, 
                               uint32 id, int trustee)
/* return 1 if inserted, else 0 */
{
  while (count--) { 
    if (ugid_trustees->id==id) {
      /* ugid_trustees->trustee|=trustee;   wrong  */
      ugid_trustees->trustee = trustee;  /* correct, mst:07-Sep-00 */
      /* new trustees must always override the current one */
      return(0);
    }
    ugid_trustees++;
  }
  ugid_trustees->id=id;
  ugid_trustees->trustee=trustee;
  return(1);
}

static int build_trustee_rights(FILE_TRUSTEE_NODE *tr, 
                IDS_TRUSTEE *ugid_trustees, int count)
/* this routine must be called root to leaf */
{
  if (tr) {
    int i            = tr->trustee_count;
    IDS_TRUSTEE *trn = tr->trustees;
    int k = count;
    if (tr->mode_flags & (8|4|2)) { 
      /* dev changed or root volume or symlink  */
      if ( (tr->mode_flags&2) && (act_id_flags&1) ) { 
        /* root directory and supervisor equivalences */
        /* get all trusttee rights  */
        if (insert_ugid_trustee(ugid_trustees, count, 
                     act_obj_id, MAX_TRUSTEE_MASK))
          count++;
      } else {
      /* trusteess will not be passed to childs */
        while (k--) { /* first we set all to null */
          (ugid_trustees+k)->trustee = 0;
        }
      }
      /* inherited_mask will be 0               */
      tr->inherited_mask=0;
    } else {
      while (k--) { 
       /* trusteess will be passed to childs but */
       /* first we mask all with inherited_mask  */
        (ugid_trustees+k)->trustee &= tr->inherited_mask;
      }
    }
    while (i--) { /* now we read all trustees for this node */
      if (trn->id == act_obj_id || grp_exist(trn->id)){
        if (insert_ugid_trustee(ugid_trustees, count, 
                     trn->id, trn->trustee))
          count++;
      }
      trn++;
    } /* while */
    /* now we build eff_rights for this node */
    tr->eff_rights=0;
    for (k=0; k < count; k++) {
      tr->eff_rights |= (ugid_trustees+k)->trustee;
    }
  }
  return(count);
}

static FILE_TRUSTEE_NODE *find_build_trustee_node(int volume, uint8 *unixname, struct stat *stb)
{
  FILE_TRUSTEE_NODE *tr=find_creat_add_trustee_node(volume, unixname, stb);
  if (tr->eff_rights < 0) { /* now we must rebuild eff rights */
    int count=0;
    IDS_TRUSTEE *ugid_trustees=
          (IDS_TRUSTEE*)xcmalloc((grps_count+1)*sizeof(IDS_TRUSTEE)); 
    struct stat       stb1;
    (void)get_volume_inode(volume, &stb1);
    if (stb1.st_ino != stb->st_ino || stb1.st_dev != stb->st_dev) {
      /* is not volumes root */
      int volumenamelen = get_volume_unixnamlen(volume);
      char *p           = unixname+volumenamelen;
      int last_dev      = stb1.st_dev;
      int volumes_dev   = stb1.st_dev;
      FILE_TRUSTEE_NODE *tr1=find_trustee_node(
                                volume, stb1.st_dev, stb1.st_ino);
      
      if (!tr1) {
        tr1=create_trustee_node(volume, stb1.st_dev, stb1.st_ino, 3);
        add_trustee_node(tr1);
      } else tr1->mode_flags|=3;
      /* build trustees for unix volume */
      count=build_trustee_rights(tr1, ugid_trustees, count);
      
      while (*p=='/')++p;
      
      while (NULL != (p=strchr(p, '/'))) {
        *p = '\0';
        if (!stat(unixname, &stb1)) {
          if (stb1.st_ino != stb->st_ino || stb1.st_dev != stb->st_dev) {
            int mode_flags=S_ISDIR(stb1.st_mode)?1:0;
            tr1=find_trustee_node(volume, stb1.st_dev, stb1.st_ino);
            if (last_dev != stb1.st_dev) {
              last_dev    = stb1.st_dev;
              mode_flags |= 0x8; 
            }
            if (volumes_dev != stb1.st_dev)
              mode_flags|=0x10;
            
            if (!tr1) {
              struct stat lstatbuf;
              if (  lstat(unixname, &lstatbuf)  
                 || (lstatbuf.st_dev != stb1.st_dev)
                 || (lstatbuf.st_ino != stb1.st_ino)
                 || S_ISLNK(lstatbuf.st_mode) ) {
                mode_flags|=4;  
              }
              tr1=create_trustee_node(volume, stb1.st_dev, stb1.st_ino, 
                              mode_flags);
              add_trustee_node(tr1);
            } else
              tr1->mode_flags|=mode_flags;
            count=build_trustee_rights(tr1, ugid_trustees, count);
          } else {
            *p='/';
            break;
          }
        } else {
          errorp(10, "tru_get_eff_rights", "stat error `%s`", unixname);
          *p='/';
          xfree(ugid_trustees);
          return(NULL);
        }
        *p='/';
        while (*p=='/')++p;
      } /* while */
      
      if (last_dev != stb->st_dev) 
        tr->mode_flags|=0x8;
      if (volumes_dev!=stb->st_dev)
        tr->mode_flags|=0x10;
    } else { 
      /* volumes directory */
      tr->mode_flags|=(1|2); 
    }
    count=build_trustee_rights(tr, ugid_trustees, count);
    xfree(ugid_trustees);
  } /* if eff_rights < 0 */
  return(tr);
}

static int get_eff_rights_by_trustees(int volume, uint8 *unixname, struct stat *stb)
/* returns the eff. rights the actual user has as real trustees */
{
  if ( (act_obj_id == 1) && (act_id_flags&1) )  /* supervisor */
    return(MAX_TRUSTEE_MASK); /* all rights */
  else {
    FILE_TRUSTEE_NODE *tr = find_build_trustee_node(volume, unixname, stb);
    return( (tr && tr->eff_rights > -1) ? tr->eff_rights : 0);
  }
}

int tru_get_eff_rights(int volume, uint8 *unixname, struct stat *stb)
/* returns the eff. rights the actual user has  */
{
  int voloptions = get_volume_options(volume);
  int rights     = 0;
  int rights1    = 0;
  if (voloptions & VOL_OPTION_TRUSTEES){ 
    rights=(get_eff_rights_by_trustees(volume, unixname, stb) & MAX_TRUSTEE_MASK);
  }    
  if (!(voloptions & VOL_OPTION_IGNUNXRIGHT)){
    rights1 = un_nw_rights(voloptions, unixname, stb);
  }
  MDEBUG(D_TRUSTEES, {
    xdprintf(1,0, "eff_rights=%04x,%04x for`%s`", 
      rights, rights1, unixname);
  })
  return(rights|rights1);
}

int tru_eff_rights_exists(int volume, uint8 *unixname, struct stat *stb,
                           int lookfor)
/* 
 * returns 0 if lookfor right exist, 
 * otherwise returns the current rights
 */
{
  int voloptions = get_volume_options(volume);
  int rights = 0;
  int rights1 = 0;
  int result = -1;
  if (voloptions & VOL_OPTION_TRUSTEES){ 
    /* we look for trustee rights first */
    rights=get_eff_rights_by_trustees(volume, unixname, stb);
    if ((rights & TRUSTEE_S)||((rights&lookfor)==lookfor))
      result = 0;
    else if ((lookfor == TRUSTEE_T) && (rights&TRUSTEE_F) ) /* mst: 13-Apr-00 */
      result=0;
  }
  if (result && !(voloptions & VOL_OPTION_IGNUNXRIGHT)){
    rights1 = un_nw_rights(voloptions, unixname, stb);
  }
  MDEBUG(D_TRUSTEES, {
    xdprintf(1,0, "lookfor=%04x, eff_rights_exists ? = %04x(tru),%04x(unx) for`%s`", 
     lookfor, rights, rights1, unixname);
  })
  if (!result) return(0);
  
  rights |= rights1;
  
  if ((lookfor == TRUSTEE_T) && (rights&TRUSTEE_F) ) /* mst: 13-Apr-00 */
      return(0);
  
/*  return(((rights & TRUSTEE_S)||((rights&lookfor)==lookfor)) ? 0 : -1);*/
  if (rights) { 
    /* mst: 07-Oct-00, -1 as result was bad, because nw_search_file_dir()  
     * expected the real rights.
     */
    return(((rights & TRUSTEE_S)||((rights&lookfor)==lookfor)) ? 0 : rights);
  } else return(1 << 31);  // no rights
}


