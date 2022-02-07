/* nwfname.h 17-Jun-97 */

#ifndef _NWFNAME_H_
#define _NWFNAME_H_ 1


extern void init_nwfname(char *convfile);

extern uint8 *up_fn(uint8 *ss);
extern uint8 *down_fn(uint8 *ss);
extern uint8 *dos2unixcharset(uint8 *ss);
extern uint8 *unix2doscharset(uint8 *ss);

extern int dfn_imatch(uint8 a, uint8 b);
extern int ufn_imatch(uint8 a, uint8 b);

#if PERSISTENT_SYMLINKS
typedef struct {
  dev_t st_dev;
  ino_t st_ino;
  int   islink; /* if symblic link */
} S_STATB;
extern int s_stat(char  *path, struct stat *statbuf, S_STATB *stb);
extern int s_utime(char *fn,   struct utimbuf *ut, S_STATB *stb);
extern int s_chmod(char *fn,   umode_t mode, S_STATB *stb);
#else
# define s_stat(path, statbuf, stb) \
  stat((path), (statbuf)) 
# define s_utime(fn, ut,      stb) \
  utime((fn), (ut)) 
# define s_chmod(fn, mode,    stb) \
  chmod((fn), (mode)) 
#endif

#endif
