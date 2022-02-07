/* comm.c 22-Oct-98
 * simple demo for a command programm which do a
 * DOS/WIN <-> UNX command handling using PIPE filesystem.
 * Most problem under W95 is the file caching.
 * Only the 32-bit version (comm32) works correct under W95.
 * NT do not have this problems.
 *
 * can be used with unxcomm for UNX.
 *
 * Can also be used under Linux for ncpfs <-> mars_nwe.
 * comm and unxcomm must be same version !  
 */

#define MAXARGLEN 1024

/* Environment string could be in the form: UNXCOMM=p:/unxcomm
 * or under 32bit: UNXCOMM=\\lx1\pipes\unxcomm
 * or under linux: UNXCOMM=/pipes/unxcomm
 *
 */
#define ENV_UNXCOMM    "UNXCOMM"

#ifdef LINUX
# define DEFAULT_COMM   "/pipes/unxcomm"
#else
# ifdef  DEFAULT_UNC
#  define DEFAULT_COMM   DEFAULT_UNC
# else
#  define DEFAULT_COMM   "p:/unxcomm"
# endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#ifndef LINUX
#  include <io.h>
#else
#   define  O_BINARY 0
#endif
#include <fcntl.h>

static int usage(char *progname)
{
  fprintf(stderr, "Usage:\t%s prog [paras]\n", progname);
  return(1);
}

#ifdef WIN32
#include <windows.h>
#include <direct.h>
int get_server_name(char *servername, char *path)
/* returns len of servername if exist or 0 */
{
  int result=0;
  char remotepath[300];
  if (path && *path != '\\' && *path != '/' && *(path+1) != ':'){
    getcwd(remotepath, sizeof(remotepath)-1);
    path=remotepath;
  }
  if (path && path[1] == ':') {
    char localpath[10];
    DWORD  size=sizeof(remotepath);
    memcpy(localpath, path, 2);
    *(localpath+2)='\0';
    if (WNetGetConnection(localpath, remotepath, 
         &size)==NO_ERROR) {
      path=remotepath;
    }
  }
  if (path && (*path     == '\\' || *path == '/') 
           && (*(++path) == '\\' || *path == '/')  ) {
    char *p=++path;
    while (*p && *p!='/' && *p!='\\') ++p;
    result= (int)(p-path);
    if (result&&servername) {
      memcpy(servername, path, result );
      servername[result] = '\0';
    }
  } 
  return(result);
}

HANDLE loc_open(char *fn, int mode)
{
  HANDLE fd=CreateFile(fn,   GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL|
                             FILE_FLAG_SEQUENTIAL_SCAN|
                             FILE_FLAG_NO_BUFFERING, /* IMPORTANT !!! */
                             NULL);
  return(fd);
}

int loc_read(HANDLE fd, char *buf, int size)
{
  if (ReadFile(fd, buf, size, &size, NULL))
    return(size);
  return(-1);
}


#define loc_close(fd)  CloseHandle(fd)
#define loc_lseek(fd, offs, what)  /* not yet used */

#else
#define  loc_lseek  lseek
#define  loc_read   read
#define  loc_open   open
#define  loc_close  close
typedef  int HANDLE;
#define  INVALID_HANDLE_VALUE -1
#endif


int main(int argc, char **argv)
{
  char *unxcomm=getenv(ENV_UNXCOMM);
  if (NULL == unxcomm) unxcomm=DEFAULT_COMM;
  if (argc > 1) {
    char **pp=argv+1;
    int  size;
    char buf[MAXARGLEN+1024];
    HANDLE fdin  = loc_open(unxcomm, O_RDONLY|O_BINARY);
    int fdout = -1;
#ifdef WIN32    
    char buf_unxcomm[200];
    if (fdin == INVALID_HANDLE_VALUE)  {
      char servername[100];
      if (get_server_name(servername, argv[0])>0){
        sprintf(buf_unxcomm, "\\\\%s\\pipes\\unxcomm", servername);
        unxcomm=buf_unxcomm;
        fdin  = loc_open(unxcomm, O_RDONLY|O_BINARY);
      }
    }
#endif
    if (fdin != INVALID_HANDLE_VALUE)  {
      int count=loc_read(fdin, buf, 10);
      char pipepath[200];
      char *pipeext=pipepath;
      int tries=0;
      while (count < 10 && tries < 20) {
        int nc;
        if (count < 0) count =0;
        nc=loc_read(fdin, buf+count, 1);
        if (nc > 0) count+=nc;
        tries++;
      }

      if (count == 10 && buf[0]=='#' && buf[9] == '\n') {
        char *p;
        strcpy(pipepath, unxcomm);
        p=pipepath+strlen(unxcomm);
        while (p>pipepath) {
          if (*p=='\\' || *p=='/') 
            break;
          --p;
        }
        
        if (p > pipepath) {
          ++p;
          *p++='r';
          *p++='u';
          *p++='n';
          *p++='/';
          memcpy(p, buf+1, 8);
          p  += 8;
          *p++='.';
          pipeext = p;
          strcpy(pipeext, "in");
        } else pipepath[0] = '\0';
        
        tries=0;
        do {
          fdout = open(pipepath, O_WRONLY|O_BINARY);
        } while (fdout < 0 && tries++ < 5);
        
        if (fdout <0) {
          fprintf(stderr, "Cannot open pipe '%s'\n", pipepath);
        }

      } else {
        buf[count>0 ? count : 0]='\0';
        fprintf(stderr, "%d Bytes read are wrong'%s'\n", count, buf);
      }
      
      if (fdout > -1) {
        char *p=buf;
        while(--argc) {
          int l=strlen(*pp);
          memcpy(p, *pp, l);
          ++pp;
          p+=l;
          *p++ = 32;
        }
        *p++='\0';
        write(fdout, buf, (int)(p-buf));
        
        close(fdout);
        
        loc_lseek(fdin, 0, 0);
        memset(buf, 0, 512);
        
        while (0 < (size = loc_read(fdin, buf, 512 /*sizeof(buf)*/))) {
          write(1, buf, size);
          loc_lseek(fdin, 0, 2);
        }
        
        loc_close(fdin);
        return(0);
      } 
      loc_close(fdin);
    } else
      fprintf(stderr, "Cannot open PIPECOMMAND '%s'\n", unxcomm);
  }
  return(usage(argv[0]));
}
