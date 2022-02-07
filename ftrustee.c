/* ftrustee.c, 19.09.99 */
/* (C)opyright (C) 1999  Martin Stover, Marburg, Germany
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
#include "trustee.h"
#include "nwvolume.h"

int  default_uid=-1;
int  default_gid=-1;

int act_uid=0;   // unix uid
int act_gid=0;   // unix uid

int act_id_flags=0; /* &1 == supervisor equivalence !!! */
int act_obj_id=0L;  /* mars_nwe UID, 0=not logged in, 1=supervisor  */

static gid_t *act_grouplist=NULL;  /* first element is counter !! */

void set_default_guid(void)
{
  seteuid(0);
  setgroups(0, NULL);
  if (setegid(default_gid) < 0 || seteuid(default_uid) < 0) {
    errorp(1, "set_default_guid, !! SecurityAbort !!",
      "Cannot set default gid=%d and uid=%d" , default_gid, default_uid);
    exit(1);
  }
  act_gid        = default_gid;
  act_uid        = default_uid;
  xfree(act_grouplist);
}

void set_guid(int gid, int uid)
{
  if ( gid < 0 || uid < 0
     || seteuid(0)
     || setegid(gid)
     || seteuid(uid) ) {
    set_default_guid();
    if (gid < 0  && uid < 0) {
      /* don't print error */
      gid = act_gid;
      uid = act_uid;
    }
  } else if (act_gid != gid || act_uid != uid) {
    struct passwd *pw = getpwuid(uid);
    if (NULL != pw) {
      seteuid(0);
      initgroups(pw->pw_name, gid);
    }
    act_gid = gid;
    act_uid = uid;
    xfree(act_grouplist);
    if (seteuid(uid))
      set_default_guid();
    else {
      int k=getgroups(0, NULL);
      if (k > 0) {
        act_grouplist=(gid_t*)xmalloc((k+1) * sizeof(gid_t));
        getgroups(k, act_grouplist+1);
        *act_grouplist=(gid_t)k;
      }
    }

  }
  XDPRINTF((5,0,"SET GID=%d, UID=%d %s", gid, uid,
        (gid==act_gid && uid == act_uid) ? "OK" : "failed"));
}

void reset_guid(void)
{
  set_guid(act_gid, act_uid);
}

void reseteuid(void)
{
  if (seteuid(act_uid))
    reset_guid();
}

int in_act_groups(gid_t gid)
/* returns 1 if gid is member of act_grouplist else 0 */
{
  int    k;
  gid_t *g;
  if (!act_grouplist) return(0);
  k=(int)*act_grouplist;
  g = act_grouplist;
  while (k--) {
    if (*(++g) == gid) return(1);
  }
  return(0);
}

int get_unix_eff_rights(struct stat *stb)
/* returns F_OK, R_OK, W_OK, X_OK  */
/* ORED with 0x10 if owner access  */
/* ORED with 0x20 if group access  */
{
  int mode = 0;
  if (!act_uid)
    return(0x10 | R_OK | W_OK | X_OK) ;  /* root */
  else {
    if (act_uid == stb->st_uid) {
      mode    |= 0x10;
      if (stb->st_mode & S_IXUSR)
        mode  |= X_OK;
      if (stb->st_mode & S_IRUSR)
        mode  |= R_OK;
      if (stb->st_mode & S_IWUSR)
        mode  |= W_OK;
    } else if ( (act_gid == stb->st_gid)
             || in_act_groups(stb->st_gid) ) {
      mode    |= 0x20;
      if (stb->st_mode & S_IXGRP)
        mode  |= X_OK;
      if (stb->st_mode & S_IRGRP)
        mode  |= R_OK;
      if (stb->st_mode & S_IWGRP)
        mode  |= W_OK;
    } else {
      if (stb->st_mode & S_IXOTH)
        mode  |= X_OK;
      if (stb->st_mode & S_IROTH)
        mode  |= R_OK;
      if (stb->st_mode & S_IWOTH)
        mode  |= W_OK;
    }
  }
  return(mode);
}

#if 0
FILE *open_trustee_file(char *volname, char *opmode)
{
  FILE *f=fopen(
  return(f);
}
#endif

int do_export_trustees(char *expfn)
{
  int j=0;
  NW_VOL *v = nw_volumes;
  while (j++ < used_nw_volumes) {
    FILE *f=NULL;
    if (v->options & VOL_OPTION_TRUSTEES) {
      char fn[300];
      memcpy(fn, v->unixname, v->unixnamlen);
      strmaxcpy(fn+v->unixnamlen, ".trustees", 300-v->unixnamlen-1);
      printf("volume %d, '%s', '%s', '%s'\n",
               j, v->sysname, v->unixname, fn);
      if (NULL != (f=fopen(fn, "w")) ) {
        chmod(fn, 0600);
        chown(fn, 0, 0);


        fclose(f);
      } else
        errorp(0, "do_export_trustees", "cannot open '%s'", fn);
    }
    v++;
  }
  return(-1);
}

int do_import_trustees(char *expfn)
{
  return(-1);
}


static int localinit(void)
{
  FILE *f= open_nw_ini();
  if (f != (FILE*) NULL){
    int k=-1;
    nw_init_volumes(f);
    fclose(f);
    printf("Count Volumes = %d, trusteepath=%s\n",
                used_nw_volumes, path_trustees);
#if 1
    while (++k < used_nw_volumes) {
      NW_VOL *v = nw_volumes+k;
      printf("volume %2d|%-15s|%s\n", k, v->sysname, v->unixname);
    }
#endif
  } else
    printf("open_nw_ini failed\n");
  return(0);
}


static int usage(char *s)
{
  char *p=strrchr(s, '/');
  fprintf(stderr, "usage:\t%s e | i | r [path]\n", p ? p+1 : s);
  fprintf(stderr, "\te = export\n");
  fprintf(stderr, "\ti = import\n");
  fprintf(stderr, "\tr = repair\n");
  return(1);
}

int main(int argc, char *argv[])
{
  init_tools(0, 0);
  nw_debug = 5;
  localinit();
  if (argc < 2)             return(usage(argv[0]));
  if      (*argv[1] == 'e') return(do_export_trustees(argv[2]));
  else if (*argv[1] == 'i') return(do_import_trustees(argv[2]));
  else if (*argv[1] == 'r') if (!do_export_trustees(argv[2]))
                              return(do_import_trustees(argv[2]));
                            else return(1);
  else usage(argv[0]);
  return(0);
}

