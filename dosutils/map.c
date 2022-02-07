/* map.c 05-Apr-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

typedef struct {
  uint8 connection;
  uint8 volume;
  uint8 buff[512];  /* complete path  */
  uint8 *path;      /* points to path */
} NWPATH;

static void show_map(uint8 *drvstr)
{
  int j;
  for (j=0; j < 32; j++){
    uint8 connid;
    uint8 dhandle;
    uint8 flags;
    if (*drvstr && (j + 'A' != *drvstr)) continue;
    if ((!get_drive_info(j, &connid, &dhandle, &flags)) && flags){
      char  servern[52];
      char  path[256];
      servern[0]='\0';
      if (flags & 0x80) { /* lokal DRIVE */
        path[0]= '\\';
        if (j < 2){
          strcpy(path, "DISK LW");
        } else if (getcurdir(j+1, path+1)) {
          strcpy(path, "LW !OK");
        }
      } else {
        if (get_dir_path(dhandle, path)) {
          strcpy(path, "DHANDLE !OK");
        }
        if (connid) {
          get_fs_name(connid, servern);
          strcat(servern, "\\");
        } else servern[0]='\0';
      }
      printf("MAP %c: = %s%s\n", (char)j+'A', servern, path);
    }
  }
}
#if 0
static void do_map(int drive, NWPATH *nwp)
{
  if (drive > -1 && drive < 32) {
    uint8 connid;
    uint8 dhandle;
    uint8 flags;
    if ((!get_drive_info(drive, &connid, &dhandle, &flags)) && flags){
      char  servern[52];
      char  path[256];
      if (flags & 0x80) { /* lokal DRIVE */
        path[0]= '\\';
        if (drive < 2){
          strcpy(path, "DISK LW");
        } else if (getcurdir(drive+1, path+1)) {
          strcpy(path, "LW !OK");
        }
      } else {
        if (get_dir_path(dhandle, path)) {
          strcpy(path, "DHANDLE !OK");
        }
      }
      if (connid) {
        get_fs_name(connid, servern);
        strcat(servern, "\\");
      } else servern[0]='\0';
      printf("DOMAP %c: = %s%s\n", (char)drive+'A', servern, path);
    }
  }
}
#endif

static int do_map(int drive, NWPATH *nwp, int delete)
{
  int result = -1;
  if (drive > -1 && drive < 32) {
    uint8 connid;
    uint8 dhandle;
    uint8 flags;
    if (!delete ||
      (!get_drive_info(drive, &connid, &dhandle, &flags) && flags && connid)){
      uint8 nmdrive[3];
      nmdrive[0] = drive+'A';
      nmdrive[1] = ':';
      nmdrive[2] = '\0';
      result = redir_device_drive(delete ? -1 : 0x4, nmdrive, nwp->path);
    }
  }
  return(result);
}

static int parse_argv(uint8 *drvstr, NWPATH *nwpath,
                  int argc, char *argv[], int smode, int argvmode)
{
  int k     = 0;
  int mode  = 0;
  uint8 *pd = drvstr;
  *drvstr   = '\0';
  memset(nwpath, 0, sizeof(NWPATH));
  *(nwpath->buff) = '\0';
  nwpath->path = nwpath->buff;

  while (++k < argc && mode > -1) {
    uint8 *p  = argv[k];
    while (*p && mode > -1) {
      if (!mode) {
        if (*p == ':') mode = -1;
        else if (smode && *p != 's' && *p  != 'S') mode = -1;
      }
      if (mode < 0) break;
      else if (mode < 20) {
        if (*p == ':') {
          if (!mode || (mode > 1 && (*drvstr != 'S' || !smode)))
            mode = -1;
          else {
            *pd = '\0';
            if (mode > 1) {
              *drvstr='s';
              *(drvstr+1)=(uint8) atoi((char*)drvstr+1);
            }
            mode = 20;
            pd   = nwpath->buff;
          }
        } else {
          if (++mode == 20) mode = -1;
          else {
            if (*p > 0x60 && *p < 0x7b)
              *pd++ = *p - 0x20;  /* upshift */
            else
              *pd++ = *p;
          }
        }
      } else if (mode == 20) {
        if (*p == '=') mode = 30;
        else if (*p != ' ' && *p != '\t') mode = -2;
      } else if (mode == 30) {
        if (*p != ' ' && *p != '\t') {
          mode = 40;
          continue;
        }
      } else if (mode == 40) {
        if (*p > 0x60 && *p < 0x7b)
          *pd++ = *p - 0x20;  /* upshift */
        else
          *pd++ = *p;
      }
      p++;
    } /* while *p */
  } /* while k */
  if (mode == 30) {
    if (argvmode != 1)
       getcwd((char *)nwpath->buff, sizeof(nwpath->buff));
    mode = 40;
  }
  if (mode && mode != 20 && mode != 40) {
    fprintf(stderr, "Cannot interpret line. errcode=%d\n", mode);
    return(mode < 0 ? mode : -3);
  }
  return(0);
}

int func_map(int argc, char *argv[], int mode)
{
  uint8  drvstr[22];
  NWPATH nwpath;
  if (!ipx_init()) argc = 1;
  if (!parse_argv(drvstr, &nwpath, argc, argv, 0, mode)) {
    if (*(nwpath.path) || mode==1) {
      if (do_map(*drvstr - 'A', &nwpath, mode)< 0)
        fprintf(stderr, "MAP Error\n");
    }
    if (mode != 1)
      show_map(drvstr);
    return(0);
  }
  return(1);
}


/* ------------------------------------------------- */
static int show_search(uint8 *drvstr)
{
  SEARCH_VECTOR          drives;
  SEARCH_VECTOR_ENTRY *p=drives;
  int j=0;
  get_search_drive_vektor(drives);
  while (p->drivenummer != 0xff && j++ < 16) {
    char  path[256];
    char  nwname_path[300];


    if ( !*drvstr || j == *(drvstr+1)) {

      if (p->drivenummer == 0xfe){
        strcpy(path, p->dospath);
      } else {
        *path      = p->drivenummer+'A';
        *(path+1)  = ':';
        strcpy(path+2, p->dospath);
      }

      if (p->flags && !(p->flags & 0x80)){
        char *pp=nwname_path;
        *pp++ = '[';
        get_fs_name(p->connid, pp);
        pp   +=strlen(pp);
        *pp++='\\';
        if (get_dir_path(p->dhandle, pp)) {
          strcpy(pp, "ERROR NW");
        }
        pp   += strlen(pp);
        *pp ++= ']';
        *pp   = '\0';
      } else {
        *nwname_path  = '\0';
      }
      printf("SEARCH%2d = %s %s\n", j,  path, nwname_path);
    }
    p++;
  }
  return(0);
}

static int set_search(uint8 *drvstr, NWPATH *nwp, int pathmode)
{
  int result=-1;
  SEARCH_VECTOR          drives;
  SEARCH_VECTOR_ENTRY *p=drives;
  int j=0;
  int entry = (*drvstr=='s') ? *(drvstr+1) : 0;
  get_search_drive_vektor(drives);

  while (p->drivenummer != 0xff && j++ < 16) {
    if (!entry && (p->drivenummer + 'A' == *drvstr)) entry=j;
    if (p->drivenummer + 'A' == nwp->path[0] && nwp->path[1] == ':'
         && !strcmp(nwp->path+2, p->dospath)) {
      p->drivenummer=0xfe;
      *(p->dospath) = '\0';
    }
    p++;
  }

  if (entry > 0) {
    if (entry > 16)  entry = 16;
    if (pathmode == 2 && entry <= j && entry < 16) {  /* insert modus */
      int k=j+1-entry;
      if (j < 16) {
        p++;
        k++;
        j++;
      }
      while (k--) {
        memcpy(p, p-1, sizeof(SEARCH_VECTOR_ENTRY));
        --p;
      }
    }
    if (--entry < j)
      p = drives+entry;
    else (p+1)->drivenummer = 0xff;
    p->flags       = 0;
    p->drivenummer = 0xfe;
    if (pathmode==1)
      *(p->dospath) = '\0';
    else
      strcpy(p->dospath, nwp->path);
    result = set_search_drive_vektor(drives);
  }
  return(result);
}

int func_path(int argc, char *argv[], int mode)
{
  uint8  drvstr[22];
  NWPATH nwpath;
  if (!parse_argv(drvstr, &nwpath, argc, argv, 1, mode)) {
    int result=0;
    if (*(nwpath.path) || mode==1)
       result=set_search(drvstr, &nwpath, mode);
    if (mode != 1)
      show_search(drvstr);
    return(result);
  }
  return(1);
}

void remove_nwpathes(void)
{
  SEARCH_VECTOR          drives;
  SEARCH_VECTOR_ENTRY *p=drives;
  int j=0;
  get_search_drive_vektor(drives);
  while (p->drivenummer != 0xff && j++ < 16) {
    if (p->flags && !(p->flags & 0x80)){
      p->flags=0;
      p->drivenummer=0xfe;
      *(p->dospath) ='\0';
    }
    ++p;
  }
  set_search_drive_vektor(drives);
}





