/* namspace.c 16-Apr-97 : NameSpace Services, mars_nwe */

/* !!!!!!!!!!!! NOTE !!!!!!!!!! */
/* Its still very dirty, but it should work fairly well */

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
#include <dirent.h>
#include <utime.h>
#ifndef LINUX
#include <errno.h>
#endif

#include "nwvolume.h"
#include "connect.h"
#include "nwfile.h"
#include "unxfile.h"
#include "namspace.h"
#include "nameos2.h"

#if WITH_NAME_SPACE_CALLS

#define NW_PATH  /* */


typedef struct {
  int    volume;            /* Volume Number                 */
  int    has_wild;          /* fn has wildcards              */
  int    namespace;         /* which namespace do we use here*/
  struct stat statb;        /* stat buff                     */
  uint8  *fn;               /* points to last entry of path  */
  uint8  path[512];         /* path + fn, unixfilename       */
} N_NW_PATH;

typedef struct {
  DIR        *fdir;         /* for dir searches              */
  uint8      *kpath;        /* points one after unixname     */
  uint8      *unixname;     /* allocated fullname of path    */
                            /* + 257 Byte for filename.      */
} DIR_SEARCH_STRUCT;

typedef struct {
  uint32         basehandle;
  int            slot;        /* act slot in table             */
  int            locked;      /* if locked then do not remove  */
                              /* and do not move till end      */
  N_NW_PATH       nwpath;
} DIR_BASE_ENTRY;

static DIR_BASE_ENTRY *dir_base[MAX_DIR_BASE_ENTRIES];
static int anz_dbe    = 0;

static void init_nwpath(N_NW_PATH *nwpath, int namespace)
{
  nwpath->volume       = -1;
  nwpath->namespace    = namespace;
  nwpath->has_wild     = 0;
  nwpath->fn           = nwpath->path;
  *(nwpath->path)      = '\0';
  nwpath->statb.st_ino = -1;
}

static void norm_nwpath_fn(N_NW_PATH *nwpath)
{
  nwpath->fn = (uint8*)strrchr((char*)nwpath->path, '/');
  if (nwpath->fn) nwpath->fn++;
  else nwpath->fn=nwpath->path;
}

static char *xnwpath_2_unix(N_NW_PATH *nwpath, int modus,
                   int allocate_extra, uint8 *extra_path)
/*
 * returns complete UNIX path
 * modus & 1 : ignore fn, (only path)
 * modus & 2 : no  '/' at end
 *
 * if allocate_extra > 0, then the returned buffer must be
 * deallocated later
 */
{
  static char *last_unixname=NULL;
  char   *unixname;
  int    len;
  int    volume = nwpath->volume;
  int    len_extra = (extra_path) ? strlen(extra_path) : 0;
  char *p, *pp;
  if (volume < 0 || volume >= used_nw_volumes || !nw_volumes[volume].unixnamlen ) {
    char *errorstr="Z/Z/Z";
    errorp(0, "xnwpath_2_unix", "volume=%d not ok", volume);
    len = strlen(errorstr);
    unixname=xmalloc(len_extra+allocate_extra+len+10);
    strcpy(unixname, errorstr);
  } else {
    int m = ((modus & 1) && nwpath->fn > nwpath->path)  /* last path = fn */
               ? nwpath->fn - nwpath->path
               : strlen((char*)nwpath->path);
    len   = nw_volumes[volume].unixnamlen;
    unixname=xmalloc(len_extra+allocate_extra+m+len+10);
    memcpy(unixname, nw_volumes[volume].unixname, len);
       /* first UNIXNAME VOLUME */
    p = pp = unixname+len;
    memcpy(p, nwpath->path, m); /* path or partiell path */
    len += m;
    p   += m;
    if ((modus & 2) && *(p-1) == '/' ) {
      if (p > unixname+1) {
        --p;
        --len;
        --m;
      } else {
        *p++ = '.';
        ++len;
      }
    } else if (len_extra) {
      if (*(p-1) != '/') {
        *p++='/';
        len++;
      }
      memcpy(p, extra_path, len_extra);
      p   += len_extra;
      len += len_extra;
    }
    *p = '\0';
#if 0
    if (nwpath->namespace == NAME_DOS) {
      if (nw_volumes[volume].options & VOL_OPTION_DOWNSHIFT)
         downstr((uint8*)pp);
    }
#endif
  }
  if (!allocate_extra) {
    xfree(last_unixname);
    last_unixname=unixname;
  }
  return(unixname);
}

#define nwpath_2_unix(nwpath, modus) \
   xnwpath_2_unix((nwpath), (modus), 0, NULL)
#define nwpath_2_unix1(nwpath, modus, extrabytes) \
   xnwpath_2_unix((nwpath), (modus), (extrabytes), NULL)
#define nwpath_2_unix2(nwpath, modus, extrabytes,  extrastr) \
   xnwpath_2_unix((nwpath), (modus), (extrabytes), extrastr)

static int downsort_dbe_entries(int dbase)
{
  DIR_BASE_ENTRY **dbep=&(dir_base[dbase]);
  DIR_BASE_ENTRY **dbpq=dbep;
  int k=dbase;
  while (k--) {
    --dbpq;
    if (!(*dbpq) || !(*dbpq)->locked) {
      *dbep = *dbpq;
      if (*dbep) (*dbep)->slot = dbase;
      dbep=dbpq;
      dbase=k;
    }
  }
  return(dbase);
}

static DIR_BASE_ENTRY *allocate_dbe_p(int namespace)
/* returns new allocated dir_base_entry_pointer */
{
  int j     =-1;
  int to_use=-1;
  DIR_BASE_ENTRY *dbe;
  while (++j < anz_dbe && NULL != (dbe = dir_base[j])){
    if (j > 3 && !dbe->basehandle && !dbe->locked)
      to_use=j;
    if (dbe->slot != j) {
      XDPRINTF((0,0, "slot %d != %d", j, dbe->slot));
    }
  }

  if (j == anz_dbe) {
    if (anz_dbe == MAX_DIR_BASE_ENTRIES) {
      if (to_use > -1) {
        j    = to_use;
      } else {
        while (j && dir_base[--j]->locked) ;;
      }
      xfree(dir_base[j]);
    } else
      anz_dbe++;
    dir_base[j] = NULL;
  }
  j=downsort_dbe_entries(j);
  dbe=dir_base[j]=(DIR_BASE_ENTRY*)xcmalloc(sizeof(DIR_BASE_ENTRY));
  dbe->slot  = j;
  init_nwpath(&(dbe->nwpath), namespace);
  return(dbe);
}

static void xx_free_dbe_p(DIR_BASE_ENTRY **dbe)
{
  if (NULL != dbe && NULL != *dbe) {
    int slot=(*dbe)->slot;
    xfree(*dbe);
    *dbe=dir_base[slot] = NULL;
    while (anz_dbe && ((DIR_BASE_ENTRY*)NULL == dir_base[anz_dbe-1]) )
        --anz_dbe;
  }
}
#define free_dbe_p(dbe)  xx_free_dbe_p(&(dbe))

static int touch_handle_entry_p(DIR_BASE_ENTRY *dbe)
/* routine touchs this entry and returns the new offset */
{
  int dbase = (NULL != dbe) ? dbe->slot : -1;
  XDPRINTF((4, 0, "touch_handle_entry_p entry dbase=%d", dbase));
  if (dbase > 4) {
    dir_base[dbase] = NULL;
    dbase=downsort_dbe_entries(dbase);
    dir_base[dbase] = dbe;
    dbe->slot       = dbase;
  }
  XDPRINTF((4, 0, "touch_handle_entry_p return dbase=%d", dbase));
  return(dbase);
}

char *debug_nwpath_name(N_NW_PATH *p)
/* only for debugging */
{
#if DO_DEBUG
  static char *nwpathname=NULL;
  char volname[300];
  int  len;
  if (nw_get_volume_name(p->volume, volname) < 1)
    sprintf(volname, "<%d=NOT-OK>", (int)p->volume);
  len = strlen(volname) + strlen(p->path) + strlen(p->fn) + 40;
  xfree(nwpathname);
  nwpathname=xmalloc(len);
  sprintf(nwpathname, "%d `%s:%s`,fn=`%s`",
      p->namespace, volname, p->path, p->fn);
#else
  static char nwpathname[2];
  nwpathname[0]='\0';
  nwpathname[1]='\0';
#endif
  return(nwpathname);
}

static int get_comp_pathes_size(NW_HPATH *nwp, uint8 *pp_pathes)
/* returns size of path components in bytes */
{
  int     k         = -1;
  int     size      =  0;
  while (++k < nwp->components) {
    int   len = (int) *(pp_pathes++);
    pp_pathes+= len;
    size     += len;
    size++;
  }
  return(size);
}

static int add_hpath_to_nwpath(N_NW_PATH *nwpath,
                               NW_HPATH *nwp, uint8 *pp_pathes)
/*
 * Routine adds nwp  to  nwpath
 * nwpath must be filled correctly before entry
 * return > -1 if ok
 */
{
  int     result    =  0;
  int     k         = -1;
  int     npbeg     = strlen(nwpath->path);
  uint8  *pp        = nwpath->path+npbeg;
#if 0
  int    perhaps_inode_mode = 0;
#endif
  XDPRINTF((2, 0, "entry add_hpath_to_nwpath: %s",
                    debug_nwpath_name(nwpath)));
  while (!result && ++k < nwp->components) {
    int   len = (int) *(pp_pathes++);
    uint8 *p  =         pp_pathes;
    pp_pathes+=len;
    if (!len) {  /* this means '..' ! */
      if (pp > nwpath->path) {
        pp=strrchr((char*)nwpath->path, '/');
        if (!pp)
          pp=nwpath->path;
        *pp='\0';
        npbeg = pp - nwpath->path;
        continue;
      } else {
        result=-0x9c; /* wrong path */
        goto leave_build_nwpath;
      }
    }
    if (!k && nwpath->volume == -1) { /* first component is volume */
      if ((nwpath->volume=nw_get_volume_number(p, len)) < 0)  {
        /* something not OK */
        result = nwpath->volume;
        goto leave_build_nwpath;
      }
      pp    = nwpath->path;
      *pp   = '\0';
      npbeg = 0;
    } else {  /* here is path (+ fn ) */
      int i=len;
      if (pp > nwpath->path) {  /* not the first entry */
        *pp   = '/';   /* we must add a slash */
        *++pp = '\0';
      }

      while (i--) {
        if (*p == 0xae || *p == '.') {
          *pp++ = '.';
#if 0
          if (nwpath->namespace == NAME_DOS) {
            if (i==3 && *(p+1) == '_')
              perhaps_inode_mode++;
          }
#endif
        } else if (*p == 0xaa || *p == '*' ) {
          *pp++ = '*';
          nwpath->has_wild++;
        } else if (*p == 0xbf || *p == '?' ) {
          *pp++ = '?';
          nwpath->has_wild++;
        } else if (*p == '/' || *p == '\\') {
          *pp++ = '/';
        } else if (*p == ':') { /* extract volumename */
          int vlen = (int) (pp - nwpath->path);
          if ((nwpath->volume=nw_get_volume_number(nwpath->path, vlen)) < 0) {
            result = nwpath->volume;
            goto leave_build_nwpath;
          }
          pp    = nwpath->path;
          *pp   = '\0';
          npbeg = 0;
        } else *pp++ = *p;
        p++;
      }  /* while */
      *pp = '\0';
    }  /* else */
  } /* while */
  if (nwpath->volume < 0) result=-0x9c; /* wrong path */
leave_build_nwpath:
  if ((!result) && (!act_obj_id) && !(entry8_flags & 1)) {
    if (nwpath->volume)
      result = -0x9c;   /* wrong path, only volume 0 is OK */
    else {
      char *p=nwpath->path;
      char pp[10];
      while (*p=='/') ++p;
      strmaxcpy(pp, p, 6);
      upstr(pp);
      p=pp+5;
      if (memcmp(pp, "LOGIN", 5) || (*p!='\0' && *p!='/') )
        result=-0x9c;
    }
  }

  if (!result) {
    NW_VOL *v = &nw_volumes[nwpath->volume];
    if (nwpath->namespace == NAME_DOS || nwpath->namespace == NAME_OS2) {
      if (v->options & VOL_OPTION_IGNCASE /* || perhaps_inode_mode*/) {
        int nplen= (int)(pp - nwpath->path) - npbeg;
        uint8 unixname[1024]; /* should be enough */
        memcpy(unixname, v->unixname, v->unixnamlen);
        strcpy(unixname+v->unixnamlen, nwpath->path);
        pp=unixname+v->unixnamlen+npbeg;
        if (nwpath->namespace == NAME_OS2) {
          mangle_os2_name(v, unixname, pp);
          if (nplen > 0)
            memcpy(nwpath->path+npbeg, pp, nplen);
          XDPRINTF((5,0, "Mangle OS/2 unixname='%s'", unixname));
        } else if (nwpath->namespace == NAME_DOS) {
          mangle_dos_name(v, unixname, pp);
          if (nplen > 0)
            memcpy(nwpath->path+npbeg, pp, nplen);
          XDPRINTF((5,0, "Mangle DOS unixname='%s'", unixname));
        }
      } else {
        if (v->options & VOL_OPTION_DOWNSHIFT)
          downstr(nwpath->path);
        else
          upstr(nwpath->path);
#if 0
        if (nwpath->namespace == NAME_DOS){


        }
#endif
      }
    }
  }
  XDPRINTF((2, 0, "add_hpath_to_nwpath: result=0x%x, %s",
                  result, debug_nwpath_name(nwpath)));
  return(result);
}

static int nwp_stat(N_NW_PATH *nwpath, char *debstr)
{
  uint8 *uname=nwpath_2_unix1(nwpath, 2, 1);
  int result=stat(uname, &(nwpath->statb));
  if (nw_debug) {
    char xdebstr[2];
    if (!debstr) {
      xdebstr[0]='\0';
      debstr = xdebstr;
    }
    XDPRINTF((4, 0, "nwp_stat:%s:%d,%s",
               debstr,
              result,
             debug_nwpath_name(nwpath)));
  }
  xfree(uname);
  return(result);
}

static uint32 name_2_base(N_NW_PATH *nwpath, int namespace, int no_stat)
/* returns basehandle of path, or 0 if not exist !!   */
/* nwpath must be filled, namespace must be specified */
{
  uint32 basehandle=0L;
  if (no_stat || !nwp_stat(nwpath, "name_2_base")) {
    DEV_NAMESPACE_MAP dnm;
    dnm.dev       = nwpath->statb.st_dev;
    dnm.namespace = namespace;
    basehandle    = nw_vol_inode_to_handle(nwpath->volume,
                                  nwpath->statb.st_ino,
                                  &dnm);
  }
  return(basehandle);
}

static int add_dbe_entry(int namspace, int volume,
                      uint32 basehandle, uint8 *path, struct stat *stb)
{
  DIR_BASE_ENTRY *dbe=allocate_dbe_p(namspace);
  if (dbe) {
    dbe->nwpath.volume   = volume;
    dbe->basehandle      = basehandle;
    if (path) {
      strcpy(dbe->nwpath.path, path);
      norm_nwpath_fn(&(dbe->nwpath));
    }
    if (stb)
      memcpy(&(dbe->nwpath.statb), stb, sizeof(struct stat));
    return(dbe->slot);
  }
  return(-0x96);  /* we use out of memory here */
}

static int find_base_entry(int volume, uint32 basehandle)
/* returns base_entry from volume/basehandle pair */
{
  int k=-1;
  DEV_NAMESPACE_MAP dnm;
  ino_t  ino;
  if (!basehandle) return(-0x9b);
  ino = nw_vol_handle_to_inode(volume, basehandle, &dnm);

  while (++k < anz_dbe) {
    DIR_BASE_ENTRY *e=dir_base[k];
    if ( (DIR_BASE_ENTRY*)NULL != e
      && volume  == e->nwpath.volume
      && ino     == e->nwpath.statb.st_ino
      && dnm.dev == e->nwpath.statb.st_dev)  {
      if (!nwp_stat(&(e->nwpath), "find_base_entry")) {
        return(touch_handle_entry_p(e));
      } else {  /* the path has changed, we say handle is wrong */
        free_dbe_p(e);
        return(-0x9b);
      }
    }
  }
  /* now we test whether it is the root of volume */
  if (0 < ino) {
    struct stat statb;
    if (ino == get_volume_inode(volume, &statb)) {
      /* its the handle of the volumes root */
      return(add_dbe_entry(dnm.namespace, volume, basehandle, NULL, &statb));
    }
  }
  return(-0x9b);
}

static int insert_get_base_entry(N_NW_PATH *nwpath,
                           int namespace, int creatmode)
{
  uint32 basehandle = name_2_base(nwpath, namespace, 0);
  if (!basehandle && creatmode) { /* now creat the entry (file or dir) */
    int result = 0;
    char *unname = nwpath_2_unix(nwpath, 2);
    if (get_volume_options(nwpath->volume, 1) &
            VOL_OPTION_READONLY) return(-0x8a);

    if (creatmode & FILE_ATTR_DIR) {
       /* creat dir */
      if (mkdir(unname, 0777))
        result=-0x84;
      else
        chmod(unname, act_umode_dir);
    } else {
       /* creat file */
      if ((result = creat(unname, 0777)) > -1) {
        chmod(unname, act_umode_file);
        close(result);
        result = 0;
      } else result=-0x84;
    }
    if (result) return(result);
    basehandle = name_2_base(nwpath, namespace, 0);
  }

  if (basehandle) {
    int k=-1;
    while (++k < anz_dbe) {
      DIR_BASE_ENTRY *e=dir_base[k];
      if ( (DIR_BASE_ENTRY*)NULL != e
        && nwpath->volume       == e->nwpath.volume
        && nwpath->statb.st_ino == e->nwpath.statb.st_ino
        && nwpath->statb.st_dev == e->nwpath.statb.st_dev)  {
        if (nwp_stat(&(e->nwpath), "insert_get_base_entry")
          || strcmp(e->nwpath.path, nwpath->path)) {
         /* the path has changed, we remove this entry */
          free_dbe_p(e);
        } else {
          return(touch_handle_entry_p(e));
        }
        break;
      }
    }
    /* now i know that it's a new base entry */
    return(add_dbe_entry(namespace, nwpath->volume,
                       basehandle,  nwpath->path, &(nwpath->statb)));
  }
  return(-0xff); /* invalid path = -0x9c, -0xff no matching files */
}

static int build_base(int            namespace,
                      NW_HPATH       *nwp,
                      uint8          *pathes,
                      int            mode,
                      uint8          *rets)

/*
 * routine returns the actual dbe entry offset or
 * < 0 if error
 * if mode == 1, then last path element will be ignored
 * and will be put into the rets variable
 */

{
  int  result=0;
  N_NW_PATH  loc_nwpath;
  N_NW_PATH  *nwpath=&loc_nwpath;
  init_nwpath(nwpath, namespace);
  if (!nwp->flag) {  /* short directory handle */
    int dir_handle = (int)nwp->base[0];
    NW_DIR *dir    = (dir_handle > 0 && dir_handle <= used_dirs)
                     ? &(dirs[dir_handle-1])
                     : NULL;
    if (dir && dir->inode) {
      int llen       = strlen(dir->path);
      uint8  *p      = nwpath->path+llen;
      nwpath->volume = dir->volume;
      memcpy(nwpath->path, dir->path, llen+1);
      if (llen && *(p-1) == '/')
        *(p-1) = '\0';
      result = (nwpath->volume > -1) ? 0 : -0x98;
    } else result = -0x9b;
    XDPRINTF((4, 0, "build_base with short_dir_handle=%d, result=0x%x",
                     dir_handle, result));
  } else if (nwp->flag == 1) { /* basehandle */
    if (-1 < (result = find_base_entry(nwp->volume, GET_32(nwp->base)))) {
      DIR_BASE_ENTRY *e = dir_base[result];
      nwpath->volume    = nwp->volume;
      strcpy(nwpath->path, e->nwpath.path);
      result = 0;
    } else if (!GET_32(nwp->base)) {
      nwpath->volume    = nwp->volume;
      nwpath->path[0]   = '\0';
      result = 0;
    }
    XDPRINTF((4, 0, "build_base with basehandle=%ld, result=0x%x",
                     GET_32(nwp->base), result));
  } else if (nwp->flag != 0xff)
    result=-0xff;
  if (!result) {
    nwpath->namespace = namespace;
    if ((result = add_hpath_to_nwpath(nwpath, nwp, pathes)) > -1) {
      char   *pp=strrchr((char*)nwpath->path, '/');
      if (mode) {
        if (pp) {
          if (rets) strcpy(rets, pp+1);
          *(pp)=0;
          pp=strrchr((char*)nwpath->path, '/');
        } else {
          if (rets) strcpy(rets, nwpath->path);
          *(nwpath->path) = '\0';
        }
      }
      nwpath->fn = (pp) ? (uint8*)pp+1 : nwpath->path;
      result = insert_get_base_entry(nwpath, namespace, 0);
    }
  }
  return(result);
}

static int build_dos_name(DIR_BASE_ENTRY *e, uint8 *fname)
{
  uint8 *ss=e->nwpath.fn;
  int   len=0;
  int   pf=0;
  int   is_ok=1;
  int   options=get_volume_options(e->nwpath.volume, 1);
  for (; *ss; ss++){
    if (*ss == '.') {
      if (pf++) {  /* no 2. point */
        is_ok=0;
        break;
      }
      len=0;
    } else {
      ++len;
      if ((pf && len > 3) || len > 8) {
        is_ok=0;
        break;
      }
      if (!(options & VOL_OPTION_IGNCASE)){
        if (options & VOL_OPTION_DOWNSHIFT){   /* only downshift chars */
          if (*ss >= 'A' && *ss <= 'Z') {
            is_ok=0;
            break;
          }
        } else {            /* only upshift chars   */
          if (*ss >= 'a' && *ss <= 'z') {
            is_ok=0;
            break;
          }
        }
      }
    }
  }
  if (is_ok) {
    strcpy(fname, e->nwpath.fn);
    upstr(fname);
    return(strlen(fname));
  } else {
    return(sprintf(fname, "%ld.___", (long)e->nwpath.statb.st_ino));
  }
}

#if 0
typedef struct {
  int            anz;
  DIR_BASE_ENTRY *ee[MAX_DIR_BASE_ENTRIES];
} BASE_COMPONENTS;

static BASE_COMPONENTS *get_path_components(DIR_BASE_ENTRY *dbe)
{
  BASE_COMPONENTS *be=(BASE_COMPONENTS*)xcmalloc(sizeof(BASE_COMPONENTS));
  uint8 *p=dbe->nwpath.path;
  uint8 *lastpp = p+strlen(p);
  int   len=0;
  int   k=0;
  int   done=(lastpp==dbe->nwpath.path ||
             (lastpp==dbe->nwpath.path+1 && *(dbe->nwpath.path)== '/' ));


  result = insert_get_base_entry(nwpath, namespace, 0);



  int l;
  while (!done) {
    uint8 *pp=lastpp;
    while (pp > dbe->nwpath.path && *pp!='/') --pp;
    if (*pp != '/') {
      done++;
      l = lastpp-pp;
    } else {
      pp++;
      l = lastpp-pp;
      lastpp=pp-1;
    }
    if (!k++) {


    } else {

    }


    if (l > 255)
       return(-0xff);
    len+=(l+1);
    *(p++) = (uint8)l;
    memcpy(p, pp, l);
    *(p+l)='\0';
    k++;
    XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));
    p+=l;
  }
  /* now as last element the volume */
  l=strlen(nw_volumes[dbe->nwpath.volume].sysname);
  len+=(l+1);
  *(p++) = (uint8)l;
  memcpy(p, nw_volumes[dbe->nwpath.volume].sysname, l);
  *(p+l)='\0';
  k++;
  XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));

  if (len > 400)
    return(-0xff); /* we support only 'short' pathnames */

}

#endif


int nw_generate_dir_path(int      namespace,
                         NW_HPATH *nwp,
                         uint8    *ns_dir_base,
                         uint8    *dos_dir_base)

/* returns Volume Number >=0  or errcode < 0 if error */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    U32_TO_32(dbe->basehandle, ns_dir_base);  /* LOW - HIGH */
    if (namespace != NAME_DOS) {
      U32_TO_32(name_2_base(&(dbe->nwpath), NAME_DOS, 1),  dos_dir_base);
    } else {
      U32_TO_32(dbe->basehandle, dos_dir_base);
    }
    XDPRINTF((3, 0, "nw_generate_dir_path path=%s, result=%d, basehandle=0x%x",
       debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
    result= dbe->nwpath.volume;
  } else {
    XDPRINTF((3, 0, "nw_generate_dir_path NOT OK result=-0x%x", -result));
  }
  return(result);
}

static int build_dir_info(DIR_BASE_ENTRY *dbe,
                          int namespace,
                          uint32 infomask, uint8 *p)
{
  N_NW_PATH      *nwpath=&(dbe->nwpath);
  struct stat    *stb=&(nwpath->statb);
  int    result      = 76;  /* minimumsize */
  uint32 owner       = get_file_owner(stb);
  memset(p, 0, result+2);

  if (infomask & INFO_MSK_DATA_STREAM_SPACE) {
    U32_TO_32(stb->st_size, p);
  }
  p  += 4;

  if (infomask & INFO_MSK_ATTRIBUTE_INFO) {
    uint32 attrib = (uint32) un_nw_attrib(stb, 0, 0);
    U32_TO_32(attrib, p);
    p      += 4;
    U16_TO_16((uint16)(attrib & 0xFFFF), p);
    p      += 2;
  } else p+=6;

  if (infomask & INFO_MSK_DATA_STREAM_SIZE) {
    U32_TO_32(stb->st_size, p);
  }
  p      +=4;

  if (infomask & INFO_MSK_TOTAL_DATA_STREAM_SIZE) {
    U32_TO_32(stb->st_size, p);
    p      +=4;
    U16_TO_16(0, p);
    p      +=2;
  } else p+=6;

  if (infomask & INFO_MSK_CREAT_INFO) {
    un_time_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    un_date_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    U32_TO_BE32(owner, p);  /* HI-LOW ! */
    p      +=4;
  } else  p+=8;

  if (infomask & INFO_MSK_MODIFY_INFO) {
    un_time_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    un_date_2_nw(stb->st_mtime, p, 0);
    p      +=2;
    U32_TO_BE32(owner, p); /* HI-LOW ! */
    p      +=4;
    un_date_2_nw(stb->st_atime, p, 0);  /* access date */
    p      +=2;
  } else p+=10;

  if (infomask & INFO_MSK_ARCHIVE_INFO) {
    un_time_2_nw(0, p, 0);
    p      +=2;
    un_date_2_nw(0, p, 0);
    p      +=2;
    U32_TO_BE32(0, p);   /* HI-LOW */
    p      +=4;
  } else p+=8;

  if (infomask & INFO_MSK_RIGHTS_INFO) { /* eff. rights ! */
    U16_TO_16( un_nw_rights(stb), p);
  }
  p      +=2;

  if (infomask & INFO_MSK_DIR_ENTRY_INFO) {
    U32_TO_32(dbe->basehandle, p);
    p      +=4;
#if 0
    U32_TO_32(dbe->basehandle, p);
#else
    U32_TO_32(name_2_base(nwpath, NAME_DOS, 1),  p);
#endif
    p      +=4;
    U32_TO_32(nwpath->volume, p);
    p      +=4;
  } else p+=12;

  if (infomask & INFO_MSK_EXT_ATTRIBUTES) {
    U32_TO_32(0, p);  /* Ext Attr Data Size */
    p      +=4;
    U32_TO_32(0, p);  /* Ext Attr Count */
    p      +=4;
    U32_TO_32(0, p);  /* Ext Attr Key Size */
    p      +=4;
  } else p+=12;

  if (infomask & INFO_MSK_NAME_SPACE_INFO){
    U32_TO_32(namespace, p);  /* using namespace */
  }
  p      +=4;

  /* ---------------------------------------------- */
  if (infomask & INFO_MSK_ENTRY_NAME) {
    result++;
    if (namespace == NAME_DOS) {
      *p=(uint8) build_dos_name(dbe, p+1);
      result += (int) *p;
    } else {
      *p = (uint8) strlen(nwpath->fn);
      if (*p) {
        memcpy(p+1, nwpath->fn, (int) *p);
        result += (int) *p;
      }
    }
  }
  XDPRINTF((3, 0, "build_d_i:space=%d, path=%s, result=%d, handle=0x%x, mask=0x%lx",
          namespace, debug_nwpath_name(nwpath), result, dbe->basehandle, infomask));
  return(result);
}
int nw_optain_file_dir_info(int namespace,    NW_HPATH *nwp,
                            int destnamspace,
                            int searchattrib, uint32 infomask,
                            uint8 *responsedata)
/* returns sizeof info_mask
 * the sizeof info_mask is NOT related by the infomask.
 * But the _valid_ info is.
 * must return -0xff for file not found  !
 */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    nwp_stat(&(dbe->nwpath), "nw_optain_file_dir_info");
    result = build_dir_info(dbe, destnamspace, infomask, responsedata);
  } else {
    XDPRINTF((3, 0, "nw_optain_file_dir_info NOT OK result=-0x%x",
           -result));
  }
  return(result);
}

int nw_get_eff_rights(int namespace,    NW_HPATH *nwp,
                            int destnamspace,
                            int searchattrib, uint32 infomask,
                            uint8 *responsedata)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    nwp_stat(&(dbe->nwpath), "nw_get_eff_rights");
    U16_TO_16(un_nw_rights(&(dbe->nwpath.statb)), responsedata);
    responsedata+=2;
    result = 2+build_dir_info(dbe, destnamspace, infomask, responsedata);
  } else {
    XDPRINTF((3, 0, "nw_get_eff_rights NOT OK result=-0x%x",
           -result));
  }
  return(result);
}

static int nw_init_search(int       namespace,
                          NW_HPATH *nwp,
                          uint8    *pathes,
                          uint8    *responsedata)
{
  int result = build_base(namespace, nwp, pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    result = S_ISDIR(dbe->nwpath.statb.st_mode) ? 0 : -0xff;
    if (result > -1) {
      *responsedata++ = dbe->nwpath.volume;
      U32_TO_32(dbe->basehandle, responsedata);
      responsedata+=4;
      U32_TO_32(0L, responsedata); /* searchsequenz */
      result     = 9;
    }
    XDPRINTF((3, 0, "nw_init_search path=%s, result=%d, basehandle=0x%x",
       debug_nwpath_name(&(dbe->nwpath)), result, dbe->basehandle));
  } else {
    XDPRINTF((3, 0, "nw_init_search NOT OK result=%d", result));
  }
  return(result);
}


static int get_add_new_entry(DIR_BASE_ENTRY *qbe, int namespace,
                      uint8 *path, int creatmode)
{
  N_NW_PATH      nwpath;
  nwpath.volume     = qbe->nwpath.volume;
  nwpath.namespace  = namespace;
  strcpy(nwpath.path, qbe->nwpath.path);
  nwpath.fn = nwpath.path+strlen(nwpath.path);
  if (nwpath.fn > nwpath.path && *(nwpath.fn-1) != '/') {
    *(nwpath.fn)    = '/';
    *(++nwpath.fn)  = '\0';
  }
  strcpy(nwpath.fn, path);
  return(insert_get_base_entry(&nwpath, namespace, creatmode));
}

static int namespace_fn_match(uint8 *s, uint8 *p, int namespace)
/* for other namespaces than DOS + OS2 */
{
  int  pc, sc;
  uint state = 0;
  int  anf, ende;
  int  not = 0;
  uint found = 0;
  while ( (pc = *p++) != 0) {
    switch (state){
      case 0 :
      if (pc == 255) {
         switch (pc=*p++) {
           case 0xaa :
           case '*'  : pc=3000; break; /* star  */

           case 0xae :
           case '.'  : pc=1000; break; /* point */

           case 0xbf :
           case '?'  : pc=2000; break; /*  ? */

           default   : pc=*(--p);
                       break;
         }
      } else if (pc == '\\') continue;

      switch  (pc) {
        case 1000: if ('.' != *s++) return(0);
                    break;

        case 2000: if (!*s++) return(0);
                    break;

        case 3000 : if (!*p) return(1);
                   while (*s){
                     if (namespace_fn_match(s, p, namespace) == 1) return(1);
                     ++s;
                   }
                   return(0);

        case '[' :  if ( (*p == '!') || (*p == '^') ){
                       ++p;
                       not = 1;
                     }
                     state = 1;
                     continue;

        default  :  if ( pc != *s &&
                       ( namespace != NAME_OS2
                         || (!isalpha(pc)) || (!isalpha(*s))
                         || (pc | 0x20) != (*s | 0x20) ) )
                           return(0);
                    ++s;
                    break;

      }  /* switch */
      break;

      case   1  :   /*  Bereich von Zeichen  */
        sc = *s++;
        found = not;
        if (!sc) return(0);
        do {
          if (pc == '\\') pc = *(p++);
          if (!pc) return(0);
          anf = pc;
          if (*p == '-' && *(p+1) != ']'){
            ende = *(++p);
            p++;
          }
          else ende = anf;
          if (found == not) { /* only if not found */
            if (anf == sc || (anf <= sc && sc <= ende))
               found = !not;
          }
        } while ((pc = *(p++)) != ']');
        if (! found ) return(0);
        not   = 0;
        found = 0;
        state = 0;
        break;

      default :  break;
    }  /* switch */
  } /* while */
  return ( (*s) ? 0 : 1);
}


int nw_search_file_dir(int namespace, int datastream,
                       uint32 searchattrib, uint32 infomask, int *count,
                       int volume, uint32 basehandle, uint32 *sequence,
                       int len, uint8  *path, uint8 *info, int *perhaps_more)

{
static uint32 saved_sequence=0L;
#if 0
  int max_counts = *count;
#endif
  int found      = 0;
  int result     = find_base_entry(volume, basehandle);
  *perhaps_more  = 0;
  *count         = 0;
  if (len > 255) return(-0x9c); /* wrong path  */

  if (result > -1) {
    DIR_BASE_ENTRY    *dbe=dir_base[result];
    DIR_SEARCH_STRUCT *ds=(DIR_SEARCH_STRUCT*) xcmalloc(sizeof(DIR_SEARCH_STRUCT));
    ds->unixname   = (uint8*)nwpath_2_unix1(&(dbe->nwpath), 2, 258);
    if (NULL != (ds->fdir = opendir(ds->unixname)) ) {
      uint8          entry[257];
      uint8          *pe=entry;
      int            have_wild=0;
      int            inode_search=0;
      uint8          *is_ap=NULL; /* one after point */
      struct dirent  *dirbuff;
      struct stat    statb;
      int            dest_entry=-1;
      int vol_options  = get_volume_options(volume, 0);
      ds->kpath        = ds->unixname+strlen(ds->unixname);
      *(ds->kpath)     = '/';
      *(++(ds->kpath)) = '\0';
      if (*sequence == MAX_U32) {
        saved_sequence=0L;
        *sequence=0L;
      }
#if 0
      if (saved_sequence) {
        seekdir(ds->fdir, saved_sequence);
        saved_sequence=0L;
      } else
#endif
      if (*sequence)
         seekdir(ds->fdir, *sequence);

      dbe->locked++;

      while (len--) {
        uint8 c=*path++;
        *pe++=c;
        if (!have_wild) {
          if (c==0xff) {
            if (*path == '?' || *path == '*'
                     || *path == 0xae || *path == 0xbf || *path==0xaa)
              have_wild++;
          } else if (c == '.') is_ap=pe;
        }
      }
      *pe='\0';

      if (!have_wild && is_ap && pe - is_ap == 3 && *is_ap== '_'
        && *(is_ap+1) == '_'  && *(is_ap+2) == '_') {
        *(is_ap -1) = '\0';
        inode_search=atoi(entry);
        *(is_ap -1) = '.';
      }

      if ( (namespace == NAME_DOS || namespace == NAME_OS2)
          && !(vol_options & VOL_OPTION_IGNCASE) )  {
        if (vol_options & VOL_OPTION_DOWNSHIFT) {
          downstr(entry);
        } else {
          upstr(entry);
        }
      }

      XDPRINTF((5,0,"nw_s_f_d namsp=%d,sequence=%d, search='%s'",
                        namespace, *sequence, entry ));
      while ((dirbuff = readdir(ds->fdir)) != (struct dirent*)NULL){
        uint8 *name=(uint8*)(dirbuff->d_name);
        if (dirbuff->d_ino && strlen((char*)name) < 256) {
          int   flag;
          XDPRINTF((5,0,"nw_search_file_dir Name='%s'", name));
          if (!inode_search) {
            if (namespace == NAME_DOS) {
              flag = (*name != '.' &&  fn_dos_match(name, entry, vol_options));
            } else if (namespace == NAME_OS2) {
              flag = (*name != '.' || (*(name+1) != '.' && *(name+1) != '\0' ))
                     && fn_os2_match(name, entry, vol_options);
            } else {
              flag = (!strcmp(name, entry)
                     || namespace_fn_match(name, entry, namespace));
            }
          } else
            flag = (dirbuff->d_ino == inode_search);
          if (flag) {
            strcpy(ds->kpath, name);
            XDPRINTF((5,0,"nw_search_file_dir Name found=%s unixname=%s",
                                               name, ds->unixname));
            if (!stat(ds->unixname, &statb)) {
              flag = (searchattrib & W_SEARCH_ATTR_ALL) == W_SEARCH_ATTR_ALL;
              if (!flag) {
                if (S_ISDIR(statb.st_mode))
                  flag=(searchattrib & FILE_ATTR_DIR);
                else
                  flag = !(searchattrib & FILE_ATTR_DIR);
              }
              if (flag) {
                if (!found) {
                  strcpy(entry, name);
                  if ((dest_entry = get_add_new_entry(dbe, namespace, entry, 0)) > -1) {
                    found++;
#if 0
                    if (max_counts > 1) {
                      saved_sequence = (uint32)telldir(ds->fdir);
                      /* for next turn */
                    } else
#endif
                      break;
                  } else {
                    XDPRINTF((1, 0, "nw_search_file_dir:Cannot add entry '%s'", entry));
                  }
                } else {
                  found++;
                  break;
                }
              } else {
               XDPRINTF((10, 0, "type = %s not ok searchattrib=0x%x",
                 S_ISDIR(statb.st_mode) ? "DIR" :"FILE" ,searchattrib));
              }
            } else {
              XDPRINTF((1,0,"nw_search_file_dir: stat error fn='%s'",
                              ds->unixname));
            }
            *(ds->kpath) = '\0';
          }
        }  /* if */
      } /* while */
      *(ds->kpath) = '\0';

      if (dest_entry > -1) {
        DIR_BASE_ENTRY *dest_dbe=dir_base[dest_entry];
        (void) nwp_stat(&(dest_dbe->nwpath), "nw_search_file_dir");
#if 0
        if (saved_sequence)
          *sequence= saved_sequence;
        else
#endif
        *sequence= (uint32)telldir(ds->fdir);
        /* if (found < 2) saved_sequence=0L; */
        result = build_dir_info(dest_dbe, datastream,
                         infomask |INFO_MSK_NAME_SPACE_INFO,
                         info);
        *count=1;
#if 0
        *perhaps_more=(found==2) ? 0xff : 0;
#else
        *perhaps_more=(have_wild) ? 0xff : 0;
#endif
      } else {
        saved_sequence=0L;
        result=-0xff; /* no files matching */
      }
      dbe->locked=0;
      closedir(ds->fdir);
    } /* if NULL != ds->fdir */
    xfree(ds->unixname);
    xfree(ds);
  }
  return(result);
}

static int nw_open_creat_file_or_dir(
                   int namespace,
                   int opencreatmode,
                   int attrib, uint32 infomask,
                   uint32 creatattrib,
                   int access_rights,
                   NW_HPATH *nwp, uint8 *pathes,
                   uint8    *responsedata,
                   int task)
{
  int   result = build_base(namespace, nwp, pathes, 0, NULL);
  int   exist  = result;
  uint8 last_part[258];
  *last_part='\0';
  if (result < 0 && (opencreatmode & OPC_MODE_CREAT)) {  /* do not exist */
    result = build_base(namespace, nwp, pathes, 1, last_part);
    XDPRINTF((5, 0, "nw_open_c... result=%d, last_part='%s'",
                result, last_part));
    if (result > -1)
      result = get_add_new_entry(dir_base[result], namespace, last_part,
           (creatattrib & FILE_ATTR_DIR) ? FILE_ATTR_DIR : 1);
  }
  if (result > -1) {
    uint32 fhandle=0L;
    int    actionresult=0;
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (exist < 0) actionresult |= OPC_ACTION_CREAT;
    if (!(creatattrib & FILE_ATTR_DIR)) {
      int creatmode=0; /* open */
      int attrib=0;
      if (opencreatmode & (OPC_MODE_OPEN | OPC_MODE_CREAT) ) {
        if (opencreatmode & OPC_MODE_CREAT) {
#if 0
          if (exist > -1 && !(opencreatmode & OPC_MODE_REPLACE))
            creatmode=2;
          else
#endif
          creatmode = 1;
        }

        if ((result = file_creat_open(dbe->nwpath.volume,
              nwpath_2_unix(&dbe->nwpath, 2), &(dbe->nwpath.statb),
              attrib, access_rights, creatmode, task)) > -1) {
          fhandle = (uint32) result;
#if 0
          actionresult |= OPC_ACTION_OPEN;  /* FILE OPEN */
#endif
          if (exist > -1 && (opencreatmode & OPC_MODE_REPLACE))
              actionresult |= OPC_ACTION_REPLACE;  /* FILE REPLACED */
        }
      } else
        result=-0xff;
    } else if
      (exist  > -1) result=-0x84;

    if (result > -1) {
      U32_TO_32(fhandle, responsedata);
      responsedata += 4;
      *responsedata     =(uint8) actionresult;
      *(responsedata+1) = 0;
      responsedata+=2;
      result = 6 + build_dir_info(dbe, namespace, infomask, responsedata);
    }
  }
  XDPRINTF((3, 0, "nw_open_creat mode=0x%x, creatattr=0x%x, access=0x%x, attr=0x%x, result=%d",
       opencreatmode, creatattrib, access_rights, attrib, result));
  return(result);
}

static int nw_delete_file_dir(int namespace, int searchattrib,
                               NW_HPATH *nwp)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    uint8  *unname=(uint8*)nwpath_2_unix(&(dbe->nwpath), 2);
    if (get_volume_options(dbe->nwpath.volume, 1) &
            VOL_OPTION_READONLY) result = -0x8a;
    else {
      if (S_ISDIR(dbe->nwpath.statb.st_mode)) {
        free_dbe_p(dbe);
        result = rmdir(unname);
      } else {
        if (-1 < (result = unlink(unname)))
           free_dbe_p(dbe);
      }
      if (result < 0) {
        switch (errno) {
          case EEXIST: result=-0xa0; /* dir not empty */
          default:     result=-0x8a; /* No privilegs */
        }
      } else
        result = 0;
    }
  }
  return(result);
}

static int nw_alloc_short_dir_handle(int namespace, int hmode,
                               NW_HPATH *nwp, int task, int *volume)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (S_ISDIR(dbe->nwpath.statb.st_mode)) {
      result=xinsert_new_dir(dbe->nwpath.volume, dbe->nwpath.path,
         dbe->nwpath.statb.st_ino, 300, hmode, task);
      *volume=dbe->nwpath.volume;
    } else result=-0xff;
  }
  return(result);
}

static int nw_set_short_dir_handle(int namespace, int desthandle,
                                     NW_HPATH *nwp, int task)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    if (S_ISDIR(dbe->nwpath.statb.st_mode)) {
      result=alter_dir_handle(desthandle,
              dbe->nwpath.volume, dbe->nwpath.path,
         dbe->nwpath.statb.st_ino, task);
    } else result=-0x9c; /* wrong path */
  }
  return((result > 0) ? 0 : result);
}

static int nw_get_full_path_cookies(int namespace,
                int destnamspace,
                NW_HPATH *nwp, int *cookieflags,
                uint32  *cookie1, uint32 *cookie2,
                int     *components,  uint8 *componentpath)

/* SIMPLE IMPLEMENTATION, needs more work !!!! */
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    uint8 *p      = componentpath;
    uint8 *lastpp = dbe->nwpath.path+strlen(dbe->nwpath.path);
    int   len=0;
    int   k=0;
    int   done=(lastpp==dbe->nwpath.path ||
               (lastpp==dbe->nwpath.path+1 && *(dbe->nwpath.path)== '/' ));
    int l;
    if (*cookie1 != MAX_U32 || *cookie2 != MAX_U32)
      return(-0xff);
    if (strlen(dbe->nwpath.path) > 256)
       return(-0xfb); /* we do support 'short' pathnames only */

    while (!done) {
      uint8 *pp=lastpp;
      while (pp > dbe->nwpath.path && *--pp !='/');;
      if (*pp == '/') {
        if (pp < dbe->nwpath.path + 2) ++done;
        pp++;
        l = lastpp-pp;
        lastpp = pp-1;
      } else {
        done++;
        l = lastpp-pp;
      }
      if (l > 255)
         return(-0xff);
      else if (l > 0) {
        len+=(l+1);
        *(p++) = (uint8)l;
        memcpy(p, pp, l);
        *(p+l)='\0';
        k++;
        XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));
        p+=l;
      } else {
        XDPRINTF((1, 0, "component=%d, l=%d", k ,l));
      }
    }
    /* now as last element the volume */
    l=strlen(nw_volumes[dbe->nwpath.volume].sysname);
    len+=(l+1);
    *(p++) = (uint8)l;
    memcpy(p, nw_volumes[dbe->nwpath.volume].sysname, l);
    *(p+l)='\0';
    k++;
    XDPRINTF((5, 0, "component=%d, path=`%s`", k ,p));

    if (len > 400)
      return(-0xff); /* we support only 'short' pathnames */

    *components  = k;
    *cookieflags = (S_ISDIR(dbe->nwpath.statb.st_mode)) ? 0 : 1;
    *cookie1     = 0;
    *cookie2     = MAX_U32; /* no more cookies */
    return(len);
  }
  return(result);
}

static int nw_rename_file_dir(int namespace,
                              NW_HPATH *nwps, uint8 *pathes_s,
                              NW_HPATH *nwpd, uint8 *pathes_d,
                              int      searchattrib,
                              int      renameflag)
{
  int result = build_base(namespace, nwps, pathes_s, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe_s = dir_base[result];
    uint8  last_part[258];
    uint8  *unname_s=
        (uint8*)nwpath_2_unix1(&(dbe_s->nwpath), 2, 1);
    result = build_base(namespace, nwpd, pathes_d, 1, last_part);
    if (result > -1) {
      DIR_BASE_ENTRY *dbe_d = dir_base[result];
      uint8  *unname_d =
        (uint8*)nwpath_2_unix2(&(dbe_d->nwpath), 0, 1, last_part);

      if (get_volume_options(dbe_s->nwpath.volume, 1) &
            VOL_OPTION_READONLY) result= EROFS;
      else if (get_volume_options(dbe_d->nwpath.volume, 1) &
            VOL_OPTION_READONLY) result= EROFS;

      else {
        if (S_ISDIR(dbe_s->nwpath.statb.st_mode))
          result = unx_mvdir(unname_s,  unname_d);
        else
          result = unx_mvfile(unname_s, unname_d);
      }
      XDPRINTF((5, 0, "Rename:%d '%s' -> '%s'", result, unname_s, unname_d));
      xfree(unname_d);
      switch (result) {
        case   0      : break;

        case   EEXIST : result = -0x92; break;
        case   EROFS  : result = -0x8b; break;
        default       : result = -0x8b;
      }
      if (!result) {
        free_dbe_p(dbe_s);
        if ((result=get_add_new_entry(dbe_d, namespace, last_part, 0)) > -1)
           result = 0;
      }
    }
    xfree(unname_s);
  }
  return(result);
}

static int nw_modify_file_dir(int namespace,
                              NW_HPATH *nwp, uint8 *path,
                              int searchattrib,
                               uint32 infomask, DOS_MODIFY_INFO *dmi)
{
  int result = build_base(namespace, nwp, nwp->pathes, 0, NULL);
  if (result > -1) {
    DIR_BASE_ENTRY *dbe=dir_base[result];
    uint8 *uname=nwpath_2_unix1(&(dbe->nwpath), 2, 1);
    struct stat *stb = &(dbe->nwpath.statb);
    if (-1 != (result=stat(uname, stb))){
      struct  utimbuf ut;
      int     do_utime=0;
      uint8   datetime[4];
      un_time_2_nw(stb->st_mtime, datetime,   1);
      un_date_2_nw(stb->st_mtime, datetime+2, 1);
      ut.actime  = stb->st_atime;
      ut.modtime = stb->st_mtime;

      XDPRINTF((5, 0, "modify datetime 1=%2x,%2x,%2x,%2x",
                  (int) *datetime,
                  (int) *(datetime+1),
                  (int) *(datetime+2),
                  (int) *(datetime+3)));

      if (infomask & DOS_MSK_MODIFY_DATE || infomask & DOS_MSK_MODIFY_TIME) {
#if 1
        if (infomask & DOS_MSK_MODIFY_TIME){
           datetime[0] = dmi->modified_time[1];
           datetime[1] = dmi->modified_time[0];
        }

        if (infomask & DOS_MSK_MODIFY_DATE) {
          datetime[2] = dmi->modified_date[1];
          datetime[3] = dmi->modified_date[0];
        }
#endif
        do_utime++;
      } else if (infomask & DOS_MSK_CREAT_DATE
              || infomask & DOS_MSK_CREAT_TIME) {
#if 1
        if (infomask & DOS_MSK_CREAT_TIME) {
           datetime[0] = dmi->created_time[1];
           datetime[1] = dmi->created_time[0];
        }
        if (infomask & DOS_MSK_CREAT_DATE) {
          datetime[2] = dmi->created_date[1];
          datetime[3] = dmi->created_date[0];
        }
#endif
        do_utime++;
      }
      if (do_utime) {
        XDPRINTF((5, 0, "modify datetime 2=%2x,%2x,%2x,%2x",
                  (int) *datetime,
                  (int) *(datetime+1),
                  (int) *(datetime+2),
                  (int) *(datetime+3)));

        ut.modtime = nw_2_un_time(datetime+2, datetime);
        utime(uname, &ut);
      }
      return(0);
    } else result=-0xff;
    xfree(uname);
  }
  return(result);
}

int handle_func_0x57(uint8 *p, uint8 *responsedata, int task)
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++;
  /* now p locates at 4 byte boundary ! */
  int namespace = (int) *p;   /* for most calls                   */
  XDPRINTF((3, 0, "0x57 call ufunc=0x%x namespace=%d", ufunc, namespace));
  switch (ufunc) {
    case  0x01 :  /* open creat file or subdir */
      {
        /* NW PATH STRUC */
        int      opencreatmode = (int)      *(p+1);
        int      attrib        = (int) GET_16(p+2);  /* LOW-HI */
        uint32   infomask      =       GET_32(p+4);  /* LOW-HI */
        uint32   creatattrib   =       GET_32(p+8);
        int      access_rights = (int) GET_16(p+12); /* LOW-HI */
        NW_HPATH nwpathstruct;
        memcpy(&nwpathstruct, p+14, sizeof(nwpathstruct));
        result  = nw_open_creat_file_or_dir(namespace, opencreatmode,
                   attrib, infomask, creatattrib, access_rights,
                   &nwpathstruct, p+21, responsedata, task);
      }
      break;


    case  0x02 :  /* Initialize Search */
      {
        /* NW PATH STRUC */
        NW_HPATH nwpathstruct;
        memcpy(&nwpathstruct, p+2, sizeof(nwpathstruct));
        result  = nw_init_search(namespace,
                                 &nwpathstruct,
                                 p+9,
                                 responsedata);
      }
      break;

    case  0x03 :  /* Search for File or DIR */
      {
        struct OUTPUT {
          uint8   volume;
          uint8   basehandle[4];
          uint8   sequence[4];
          uint8   reserved;
        } *xdata= (struct OUTPUT*)responsedata;
        /* NW PATH STRUC */
#if 0
        int      datastream    = (int) *(p+1);
#endif
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        int      volume        = *(p+8);
        uint32   basehandle    = GET_32(p+9);       /* LOW-HI */
        uint32   sequence      = GET_32(p+13);      /* LOW-HI */
        int      len           = *(p+17);
        uint8    *path         = p+18;
        int      count = 1;
        int      more_entries;
        result  = nw_search_file_dir(namespace, namespace /*datastream*/,
                                     searchattrib, infomask, &count,
                                     volume, basehandle, &sequence,
                                     len, path,
                                     responsedata+sizeof(struct OUTPUT),
                                     &more_entries);
        if (result > -1) {
          xdata->volume = (uint8)volume;
          U32_TO_32(basehandle, xdata->basehandle);
          U32_TO_32(sequence,   xdata->sequence);
          xdata->reserved = (uint8) 0;
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x04 :  /* rename File or Dir */
      {
        int      renameflag    = *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        NW_HPATH nwps;
        NW_HPATH nwpd;
        int      size;
        p+=4;
        memcpy(&nwps, p, 7); p+=7;
        memcpy(&nwpd, p, 7); p+=7;
        size   = get_comp_pathes_size(&nwps, p);
        result = nw_rename_file_dir(namespace,
                  &nwps, p, &nwpd, p+size, searchattrib, renameflag);
      }
      break;

    case  0x06 :  /* Obtain File or Subdir Info */
      {
#if 0
static int code = 0;
#endif
        int      destnamspace  = (int) *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nw_optain_file_dir_info(namespace,   nwpathstruct,
                                        destnamspace,
                                        searchattrib, infomask,
                                        responsedata);
#if 0
        if (result < 0)  {
          /* Unix Client errormessages */
          /* there exist *NO* result to force client to use new */
          /* basehandles :-(( */
          /*  most are -> IO/Error */
          /*  -0x9c; -> no such file or dir */
          /*  -0xfb; -> Protokoll Error     */
          /*  -0xff; -> no such file or dir */
          if (code < 0xff) result=-(++code);
          else result=-0xfe;
       }
#endif
      }
      break;

    case  0x07 :  /* Modify File or Dir Info */
      {
        NW_HPATH         nwp;
        DOS_MODIFY_INFO  dmi;
        int      searchattrib  = (int) GET_16(p+2);
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        p+=8;
        memcpy(&dmi, p, sizeof(DOS_MODIFY_INFO));
        p+= sizeof(DOS_MODIFY_INFO);
        memcpy(&nwp, p, 7);
        result  = nw_modify_file_dir(namespace, &nwp, p+7,
                                   searchattrib, infomask, &dmi);
      }
      break;

    case  0x08 :  /* Delete a File or Subdir */
      {
        int     searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+4);
        result = nw_delete_file_dir(namespace, searchattrib, nwpathstruct);
      }
      break;

    case  0x09 : /* Set short Dir Handle*/
      {          /* nw32client needs it */
        int desthandle = (int)*(p+2);
        NW_HPATH *nwp  = (NW_HPATH *)(p+4);
        result = nw_set_short_dir_handle(namespace, desthandle, nwp, task);
        /* NO REPLY Packet */
      }
      break;

    case  0x0c : /* alloc short dir Handle */
      {
        int      hmode  = (int) GET_16(p+2); /* 0=p, 1=temp, 2=speztemp */
        NW_HPATH *nwp   = (NW_HPATH *)(p+4);
        struct OUTPUT {
          uint8   dir_handle;
          uint8   volume;
          uint8   reserved[4];
        } *xdata= (struct OUTPUT*)responsedata;
        int volume;
        result = nw_alloc_short_dir_handle(namespace, hmode, nwp, task,
                  &volume);
        if (result > -1) {
          xdata->dir_handle = (uint8) result;
          xdata->volume     = (uint8) volume;
          U32_TO_32(0L,  xdata->reserved);
          result=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x14 : /* Search for File or Subdir Set */
        /* SIMPLE IMPLEMENTATION, needs more work !!!! */
      {
        struct OUTPUT {
          uint8   volume;
          uint8   basehandle[4];
          uint8   sequence[4];
          uint8   more_entries;  /* NW4.1 (UNIX) says 0xff here */
          uint8   count[2];      /* count of entries */
        } *xdata= (struct OUTPUT*)responsedata;
#if 0
        int      datastream    = (int) *(p+1);
#endif
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        int      count         = (int)GET_16(p+8);
        int      volume        = *(p+10);
        uint32   basehandle    = GET_32(p+11);      /* LOW-HI */
        uint32   sequence      = GET_32(p+15);      /* LOW-HI */
        int      len           = *(p+19);
        uint8    *path         = p+20;
        int      more_entries;
        result  = nw_search_file_dir(namespace,  namespace /*datastream*/,
                                     searchattrib, infomask, &count,
                                     volume, basehandle, &sequence,
                                     len, path,
                                     responsedata+sizeof(struct OUTPUT),
                                     &more_entries);
        if (result > -1) {
          xdata->volume        = (uint8)volume;
          U32_TO_32(basehandle, xdata->basehandle);
          U32_TO_32(sequence,   xdata->sequence);
          xdata->more_entries  = (uint8) more_entries;
          U16_TO_16(count,       xdata->count);
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x15 : /* Get Path String from short dir new */
      {
        int  dir_handle=(int) *(p+1);
        result=nw_get_directory_path(dir_handle, responsedata+1);
        if (result > -1) {
          *responsedata=(uint8) result;
          result+=1;
        }
      }
      break;

    case  0x16 : /* Generate Dir BASE and VolNumber */
      {
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+4);
        struct OUTPUT {
          uint8   ns_dir_base[4];   /* BASEHANDLE */
          uint8   dos_dir_base[4];  /* BASEHANDLE */
          uint8   volume;           /* Volumenumber*/
        } *xdata= (struct OUTPUT*)responsedata;
        result = nw_generate_dir_path(namespace,
                 nwpathstruct, xdata->ns_dir_base, xdata->dos_dir_base);
        if (result >-1 ) {
          xdata->volume = result;
          result        = sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x18 :  /* Get Name Spaces Loaded*/
      {
        int volume=*(p+2);
        struct OUTPUT {
          uint8   anz_name_spaces;
          uint8   name_space_list[1];
        } *xdata= (struct OUTPUT*)responsedata;
        result=get_volume_options(volume, 0);
        if (result >-1) {
          xdata->anz_name_spaces    = (uint8) 0;
          if (result & VOL_NAMESPACE_DOS)
            xdata->name_space_list[xdata->anz_name_spaces++]
              = (uint8) NAME_DOS;
          if (result & VOL_NAMESPACE_OS2)
            xdata->name_space_list[xdata->anz_name_spaces++]
              = (uint8) NAME_OS2;
          if (result & VOL_NAMESPACE_NFS)
            xdata->name_space_list[xdata->anz_name_spaces++]
              = (uint8) NAME_NFS;
          result=xdata->anz_name_spaces+1;
        }
      }
      break;

    case  0x1a :  /* Get Huge NS Info new*/
      {

      }
      break;

    case  0x1c :  /* GetFullPathString new*/
      {           /* nw32 client needs it */
        /* SIMPLE IMPLEMENTATION, needs more work !!!! */
        int      destnamspace  = (int)*(p+1);
        int      cookieflags   = (int)GET_16(p+2);
        uint32   cookie1       = GET_32(p+4);
        uint32   cookie2       = GET_32(p+8);
        NW_HPATH *nwpathstruct = (NW_HPATH *)(p+12);
        int      components;
        struct OUTPUT {
          uint8   cookieflags[2];    /* 0 = DIR; 1 = FILE */
          uint8   cookie1[4];
          uint8   cookie2[4];
          uint8   pathsize[2];       /* complete  components size */
          uint8   pathcount[2];      /* component counts */
        } *xdata= (struct OUTPUT*)responsedata;
        result = nw_get_full_path_cookies(namespace, destnamspace,
                         nwpathstruct,
                          &cookieflags, &cookie1, &cookie2, &components,
                          responsedata+sizeof(struct OUTPUT));
        if (result > -1) {
          U16_TO_16(cookieflags,  xdata->cookieflags);
          U32_TO_32(cookie1,      xdata->cookie1);
          U32_TO_32(cookie2,      xdata->cookie2);
          U16_TO_16(result,       xdata->pathsize);
          U16_TO_16(components,   xdata->pathcount);
          result+=sizeof(struct OUTPUT);
        }
      }
      break;

    case  0x1d :  /* GetEffDirRights new */
      {
        int      destnamspace  = (int) *(p+1);
        int      searchattrib  = (int) GET_16(p+2); /* LOW-HI */
        uint32   infomask      = GET_32(p+4);       /* LOW-HI */
        NW_HPATH *nwpathstruct = (NW_HPATH *) (p+8);
        result = nw_get_eff_rights(namespace, nwpathstruct,
                                    destnamspace,
                                    searchattrib, infomask,
                                    responsedata);
      }
      break;

    default : result = -0xfb; /* unknown request */
  } /* switch */
  return(result);
}


int get_namespace_dir_entry(int volume, uint32 basehandle,
                                   int namspace, uint8 *rdata)
{
  int result = find_base_entry(volume, basehandle);
  XDPRINTF((3, 0, "get_ns_dir_entry: vol=%d, base=0x%lx, namspace=%d",
                                    volume, basehandle, namspace));
  if (result > -1) {
    DIR_BASE_ENTRY  *e = dir_base[result];
    NW_SCAN_DIR_INFO *scif = (NW_SCAN_DIR_INFO*)rdata;
    uint8  fname[20];
    (void) nwp_stat(&(e->nwpath), "get_namespace_dir_entry");
    memset(rdata, 0, sizeof(NW_SCAN_DIR_INFO));
#if 0
    U32_TO_32(basehandle, scif->searchsequence);
#else
    U32_TO_32(name_2_base(&(e->nwpath), NAME_DOS, 1), scif->searchsequence);
#endif
    (void)build_dos_name(e, fname);
    if (S_ISDIR(e->nwpath.statb.st_mode)) {
      get_dos_dir_attrib(&(scif->u.d), &e->nwpath.statb,
                              e->nwpath.volume, fname);
    } else {
      get_dos_file_attrib(&(scif->u.f), &e->nwpath.statb,
                              e->nwpath.volume, fname);
    }
    return(sizeof(NW_SCAN_DIR_INFO));
  }
  return(result);
}


int handle_func_0x56(uint8 *p, uint8 *responsedata, int task)
/* extended attribute calls */
{
  int result    = -0xfb;      /* unknown request                  */
  int ufunc     = (int) *p++; /* now p locates at 4 byte boundary */
  XDPRINTF((3, 0, "0x56 call ufunc=0x%x", ufunc));
  switch (ufunc) {

#if 1
    case  0x01 :  /* close extended attribute handle */
      {
        /*
        uint32 ea_handle=GET_BE32(p+2);
        */
        result=0; /* dummy */
      }
      break;

    case  0x02 :  /* write extended attribute handle */
      {
        result=0; /* dummy */
      }
      break;

    case  0x03 :  /* read extended attribute */
      {
#if 0
        int flags         =  GET_16(p);  /* LOW-HIGH */
        /* next 2 entries only if flags bits 0-1 = 00 */
        /* (~(flags & 3)) volume   + basehandle  */
        /* ( flag s & 2)  eahandle + basehandle  */
        int volume        = (int)GET_32(p+2);
        uint32 basehandle = (int)GET_32(p+6);

        uint32 readpos    = GET_32(p+10);
        uint32 size       = GET_32(p+14);
        uint16 keylen     = GET_16(p+18);    /* LOW-HIGH */
#endif
        struct OUTPUT {
          uint8   errorcode[4];     /* LOW-HIGH */
          uint8   ttl_v_length[4];  /* LOW-HIGH */
          uint8   ea_handle[4];     /* ???? */
          uint8   access[4];        /* ???? */
          uint8   value_length[2];  /* LOW-HIGH */
          uint8   value[2];
        } *xdata= (struct OUTPUT*)responsedata;
        memset(xdata, 0, sizeof(struct OUTPUT));
        U32_TO_32(0xc9,  xdata->errorcode); /* NO extended Attributes */
        result        = sizeof(struct OUTPUT);
      }
      break;

    case  0x04 :  /* enumerate extended attributes */
      {
        struct OUTPUT {
          uint8   dontknow1[16];   /* all zero */
          uint8   ea_handle[4];    /* ???? */
          uint8   dontknow3[4];    /* all zero */
        } *xdata= (struct OUTPUT*)responsedata;
        memset(xdata, 0, sizeof(struct OUTPUT));
        result       = sizeof(struct OUTPUT);
      }
      break;

    case  0x05 :  /* duplicate extended attributes */
      {
      }
      break;

#endif
    default : result = -0xfb; /* unknown request */
  } /* switch */
  return(result);
}
#endif
