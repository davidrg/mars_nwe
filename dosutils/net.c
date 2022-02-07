/* net.c */
#define VERS_DATE "21-May-96"
/* simple client program to act with mars_nwe */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

char *funcname=NULL;
char  prgpath[65];

typedef int (*NET_FUNC)(int argc, char *argv[], int mode);

static struct s_net_functions {
 char      *name;
 char      *description;
 NET_FUNC  func;
 int       mode;
} net_functions[] = {

{"SPAWN",  "spawn program(command file)" ,          func_exec   , 0},
{"EXEC",   "execute program(command file)",         func_exec   , 1},
{"ECHO",   "echoes string (command file)" ,         func_echo   , 0},
{"CD",     "change directory (command file)" ,      func_cwd    , 0},
{"LOGIN",  "login to server as user" ,              func_login  , 0},
{"PROFILE","read command file" ,                    func_profile, 0},
{"CAPTURE","list and redirect printers" ,           func_capture, 0},
{"ENDCAP", "cancel redirect printers" ,             func_capture, 1},
{"MAP",    "list maps and map drives" ,             func_map    , 0},
{"MAPDEL", "removes maps" ,                         func_map    , 1},
{"PATH",   "list and set search path" ,             func_path   , 0},
{"PATHDEL","removes search path" ,                  func_path   , 1},
{"PATHINS","insert search path" ,                   func_path   , 2},
{"LOGOUT", "logout from server",       		    func_logout , 0},
#if 0
{"SLIST",  "list servers",          		    func_slist  , 0},
#endif
{"PASSWD", "change password",          		    func_passwd , 0},
#if 1
{"TESTS",  "only testroutines!",                    func_tests  , 0},
#endif
{"DEBUG",  "set debug level, for mars_nwe only !",  func_debug  , 0}
};

#define MAX_FUNCS  (sizeof(net_functions) / sizeof(struct s_net_functions))

static int get_entry_nr(char *fstr)
{
  int       entry    = MAX_FUNCS;
  char      buff[200];
  char      funcn[100];
  char      *pp;
  strmaxcpy(buff, fstr, sizeof(buff)-1);
  korrpath(buff);
  get_path_fn(buff, NULL, funcn);
  pp=strrchr(funcn, '.');
  if (NULL != pp) *pp = '\0';
  upstr(funcn);
  while (entry--) {
    if (!strcmp(funcn, net_functions[entry].name)) return(entry);
  }
  return(-1);
}

int call_func_entry(int argc, char *argv[])
{
  int       funcmode;
  int       result   = -1;
  NET_FUNC  func     = NULL;
  int       entry = get_entry_nr(argv[0]);
  if (entry > -1) {
    func     = net_functions[entry].func;
    funcmode = net_functions[entry].mode;
    funcname = net_functions[entry].name;
  }
  if (NULL != func) {
    if (ipx_init() || func == func_map) {
      result = (*func)(argc, argv, funcmode);
    } else {
      fprintf(stderr, "Cannot init IPX\n");
    }
  } else result = -0xff;
  return(result);
}

static void get_path(char *path)
{
  char buf[100];
  strmaxcpy(buf, path, sizeof(buf)-1);
  korrpath(buf);
  get_path_fn(buf, prgpath, NULL);
}

int main(int argc, char *argv[])
{
  int result = -0xff;
  get_path(argv[0]);
  result = call_func_entry(argc, argv);
  if (result == -0xff)
    result = call_func_entry(argc-1, argv+1);
  if (result == -0xff) {
    int  k= MAX_FUNCS;
    char progname[256];
    strmaxcpy(progname, argv[0], sizeof(progname)-1);
    upstr(progname);
    fprintf(stderr, "\n"
     "* (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany *\n"
     "  Version: %s\n\n", VERS_DATE);

    fprintf(stderr, "Usage:\t%s func ... \nfuncs:", progname);
    while (k--) {
      if (net_functions[k].func) {
        fprintf(stderr, "\t%s\t: %s\n",
           net_functions[k].name, net_functions[k].description);
      }
    }
  }
  return(result);
}


