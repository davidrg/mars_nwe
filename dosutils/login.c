/* login.c 21-May-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"
#include "nwcrypt.h"

static int do_change_object_passwd(char *name,
                                   uint16 objtyp,
                                   char *oldpassword,
                                   char *newpassword)

{
  uint8 key[8];
  if (0 && !ncp_17_17(key)) {
    uint32 objid = ncp_17_35(name, objtyp);
    if (objid) {
      uint8 buff[128];
      uint8 encrypted[8];
      uint8 newcryptpasswd[16];
      int   passwdx=0;
      uint8 tmpid[4];
      U32_TO_BE32(objid, tmpid);
      shuffle(tmpid, oldpassword, strlen(oldpassword), buff);
      nw_encrypt(key, buff, encrypted);

      shuffle(tmpid, newpassword, strlen(newpassword), buff);

      if (!ncp_17_4b(encrypted, name, objtyp, passwdx, newcryptpasswd)) {
        ;;
        return(0);
      }
    }
  } else { /* now we use old unencrypted algorithmus */
    if (!ncp_17_40(name, objtyp, oldpassword, newpassword)) {
      ;;
      return(0);
    }
  }
  return(-1);
}

static int do_object_login(char *name, uint16 objtyp, char *password, int option)
{
  uint8 key[8];
  if (!(option & 1) && !ncp_17_17(key)) {
    uint32 objid = ncp_17_35(name, objtyp);
    if (objid) {
      uint8 buff[128];
      uint8 encrypted[8];
      uint8 tmpid[4];
      U32_TO_BE32(objid, tmpid);
      shuffle(tmpid, password, strlen(password), buff);
      nw_encrypt(key, buff, encrypted);
      if (!ncp_17_18(encrypted, name, objtyp)) {
        ;;
        return(0);
      }
    }
  } else { /* now we use old unencrypted algorithmus */
    if (!ncp_17_14(name, objtyp, password)) {
      return(0);
    }
  }
  return(-1);
}

static void beep(void)
{
  fprintf(stdout, "\007");
}

static int get_raw_str(uint8 *s, int maxlen, int doecho)
/* returns len of readed str */
{
  int len = 0;
  while (len < maxlen){
    int key = getch();
    if (key == '\r' || key == '\n') break;
    switch (key) {
      case    8 : if (len) {
                    --len;
                    --s;
                    if (doecho) fprintf(stdout, "\010 \010");
                  } else beep();
                  continue;

      case  '\t': beep();
                  continue;

      default   : *s++=(uint8)key;
                  len++;
      		  break;
    } /* switch */
    if (doecho) fprintf(stdout, "%c", (uint8)key);
  }
  *s='\0';
  return(len);
}

static void getstr(char *what, char *str, int rsize, int doecho)
{
  fprintf(stdout, "%s: ", what);
  get_raw_str(str, rsize, doecho);
  fprintf(stdout, "\n");
}

static int login_usage(void)
{
  fprintf(stderr, "usage:\t%s [-u] [user | user password]\n", funcname);
  fprintf(stderr, "\t-u : use unecrypted password\n" );
  return(-1);
}

int func_login(int argc, char *argv[], int mode)
{
  int result=-1;
  int option=0;
  uint8 uname[200];
  uint8 upasswd[200];
  SEARCH_VECTOR  save_drives;

  if (argc > 1) {
    if (argv[1][0] == '-') {
      if (argv[1][1] == 'u') option |= 1;
      else return(login_usage());
      argc--;
      argv++;
    }
  }
  get_search_drive_vektor(save_drives);
  remove_nwpathes();
  if (argc > 1) strmaxcpy(uname, argv[1], sizeof(uname) -1);
  else uname[0]='\0';
  if (argc > 2) strmaxcpy(upasswd, argv[2], sizeof(upasswd) -1);
  else upasswd[0]='\0';

  while (result) {
    if (!uname[0]) getstr("Login", uname, sizeof(uname)-1, 1);
    if (uname[0]) {
      upstr(uname);
      upstr(upasswd);
      if ((result = do_object_login(uname, 0x1,  upasswd, option)) < 0 && !*upasswd) {
        getstr("Password", upasswd, sizeof(upasswd)-1, 0);
        upstr(upasswd);
        result = do_object_login(uname, 0x1, upasswd, option);
      }
      if (result < 0) {
        fprintf(stdout, "Login incorrect\n\n");
        uname[0]   = '\0';
        upasswd[0] = '\0';
      }
    } else break;
  }
  if (result > -1) {
    char profile[200];
    remove_nwpathes();
    sprintf(profile, "%slogin", prgpath);
    read_command_file(profile);
  } else {
    (void)set_search_drive_vektor(save_drives);
  }
  return(result);
}

int func_logout(int argc, char *argv[], int mode)
{
  remove_nwpathes();
  if (logout()) {
    fprintf(stderr, "logout=%d\n", neterrno);
    return(1);
  }
  return(0);
}


int func_passwd(int argc, char *argv[], int mode)
{
  int result=0;
  uint8 uname[100];
  uint8 upasswd[130];
  uint32 my_obj_id;

  if (ncp_14_46(&my_obj_id) < 0 || my_obj_id == MAX_U32 || !my_obj_id) {
    fprintf(stderr, "Cannot get actual user id\n");
    result = -1;
  }

  if (!result && argc > 1) {
    uint32 obj_id;
    strmaxcpy(uname, argv[1], sizeof(uname) -1);
    upstr(uname);
    obj_id = ncp_17_35(uname,  1);
    if (!obj_id) {
      fprintf(stderr, "Unkwown user: %s\n", uname);
      return(-1);
    }
  } else if (!result) {
    uint16 obj_typ;
    if (ncp_17_36(my_obj_id, uname, &obj_typ) || obj_typ != 1) {
      fprintf(stderr, "Cannot get actual username\n");
      result=-1;
    }
  }
  if (!result && *uname) {
    uint8 newpasswd[130];
    uint8 newpasswd2[130];
    if (my_obj_id == 1L) *upasswd='\0';
    else {
      getstr("Old password", upasswd, sizeof(upasswd)-1, 0);
      upstr(upasswd);
    }
    getstr("New password", newpasswd, sizeof(newpasswd)-1, 0);
    getstr("New password again", newpasswd2, sizeof(newpasswd2)-1, 0);
    if (!strcmp(newpasswd, newpasswd2)) {
      upstr(uname);
      upstr(newpasswd);
      if (do_change_object_passwd(uname, 1, upasswd, newpasswd) < 0)
         result = -1;
    } else {
      result = -1;
      fprintf(stderr, "Password misspelled\n");
    }
  }
  if (result < 0) fprintf(stderr, "Password not changed");
  return(result);
}

static int get_line(FILE *f, char *buff, int bufsize, uint8 *str, int strsize)
/* returns command line or -1 if ends */
{
  if ((FILE*) NULL != f) {
    while (fgets(buff, bufsize, f) != NULL){
      char *p       = buff;
      char *beg     = NULL;
      char c;
      int  len=0;
      while (0 != (c = *p++) && c != '\n' && c != '\r' && c != '#') {
        if (!beg){
          if (c != '\t' && c != 32) {
            beg = p - 1;
            len = 1;
          }
        } else ++len;
      }
      if (len) {
        strmaxcpy((uint8*)str, (uint8*)beg, min(len, strsize-1));
        return(0);
      }
    }
  }
  return(-1);
}



static char **build_argv(char *buf, int bufsize, char *command)
/* routine returns **argv for use with execv routines */
/* buf will contain the path component 	     	      */
{
  int len        = strlen(command);
  int offset     = ((len+4) / 4) * 4; /* aligned offset for **argv */
  int components = (bufsize - offset) / 4;
  if (components > 1) {  /* minimal argv[0] + NULL */
    char **argv  = (char **)(buf+offset);
    char **pp    = argv;
    char  *p     = buf;
    char  c;
    int   i=0;
    --components;
    memcpy(buf, command, len);
    memset(buf+len, 0, bufsize - len);
    *pp    = p;
    while ((0 != (c = *p++)) && i < components) {
      if (c == 32 || c == '\t') {
        *(p-1) = '\0';
        if (*p != 32 && *p != '\t') {
          *(++pp)=p;
          i++;
        }
      } else if (!i && c == '/') {  /* here i must get argv[0] */
        *pp=p;
      }
    }
    return(argv);
  }
  return(NULL);
}

int read_command_file(char *fstr)
{
  FILE *f=fopen(fstr, "r");
  int  result=-1;
  if (f != NULL) {
    char *linebuf= xmalloc(512);
    char *buf    = xmalloc(512);

    while (get_line(f, buf, 512, linebuf, 512) > -1) {
      char **argv=build_argv(buf, 512, linebuf);
      if (argv != NULL) {
        int argc=0;
        char **pp=argv;
        while (*pp) {
          argc++;
          pp++;
        }
        upstr(argv[0]);
        if (argc > 2 && !strcmp(argv[0], "ECHO")) {
          char *p=argv[argc-1];
          while (p-- > argv[1]) {
            if (*p=='\0') *p=32;
          }
          argc=2;
        }
        call_func_entry(argc, argv);
        result = 0;
      }
    }

    fclose(f);
    xfree(linebuf);
    xfree(buf);
  } else result=-2;
  return(result);
}

int func_profile(int argc, char *argv[], int mode)
{
  if (argc < 2) {
    fprintf(stderr, "usage:\t%s fn\n", funcname);
    return(-1);
  }
  if (read_command_file(argv[1]) == -2) {
    fprintf(stderr, "command file %s not found\n", argv[1]);
  }
  return(0);
}

int func_cwd(int argc, char *argv[], int mode)
{
  char pathname[65];
  int  len;
  if (argc < 2) {
    fprintf(stderr, "usage:\t%s path\n", funcname);
    return(-1);
  }
  strmaxcpy(pathname, argv[1], sizeof(pathname) -1);
  korrpath(pathname);
  if (0 != (len = strlen(pathname))) {
    char *p=pathname+len-1;
    if (*p == '/' || *p == ':') {
      *(++p) = '.';
      *(++p) = '\0';
      len++;
    }
    if (!chdir(pathname)) {
      if (len > 2 && *(pathname+1) == ':')   /* device changed */
        setdisk(*pathname - 'a' );
    } else {
      fprintf(stderr, "cannot chdir to %s\n", pathname);
      return(1);
    }
    return(0);
  } else return(-1);
}

int func_echo(int argc, char *argv[], int mode)
{
  if (argc > 1)
    fprintf(stdout, "%s\n", argv[1]);
  return(0);
}

int func_exec(int argc, char *argv[], int mode)
{
  if (argc > 1)  {
    char *buf   = xmalloc(512);
    char *buff  = xmalloc(512);
    char *p     = buff;
    int   k     = 0;
    char **nargv;
    while (++k < argc) {
      strcpy(p, argv[k]);
      p    += strlen(argv[k]);
      *p++  = 32;
      *p    = '\0';
    }
    nargv=build_argv(buf, 512, buff);
    xfree(buff);
    if (nargv != NULL) {
      if (!mode)
        spawnvp(P_WAIT, buf, nargv);
      else
        execvp(buf, nargv);
    }
    xfree(buf);
  }
  return(0);
}

