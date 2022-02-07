/* nwdebug.c 21-May-96 */

/****************************************************************
 * (C)opyright (C) 1993,1996  Martin Stover, Marburg, Germany   *
 ****************************************************************/

#include "net.h"

static int usage(void)
{
  fprintf(stderr, "usage:\t%s NCPSERV|NWCONN|NWBIND level\n", funcname);
  fprintf(stderr, "\tlevel=0 .. 99\n" );
  return(-1);
}

int func_debug(int argc, char *argv[], int mode)
{
  uint8 s[200];
  int   module;
  int   level;
  int   result;
  if (argc < 3) return(usage());
  strmaxcpy(s, argv[1], sizeof(s) -1);
  upstr(s);
  if (!strcmp(s,      "NCPSERV")) module=NCPSERV;
  else if (!strcmp(s, "NWCONN" )) module=NWCONN;
  else if (!strcmp(s, "NWBIND" )) module=NWBIND;
  else return(usage());
  level = atoi(argv[2]);
  if (level < 0 || level > 99) return(usage());
  result = ncp_17_02(module, level);
  if (result < 0) {
    fprintf(stderr, "set debug failed\n");
    fprintf(stderr, "perhaps you did not enable FUNC_17_02_IS_DEBUG\n");
    fprintf(stderr, "in mars_nwe/config.h ?!");
  } else fprintf(stdout, "Debug level for %s changed from %d to %d\n",
             s, result, level);
  return(result < 0 ? result : 0);
}
