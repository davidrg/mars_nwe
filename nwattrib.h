/* nwattrib.h  14-Feb-98 */

#ifndef _NWATTRIB_H_
#define _NWATTRIB_H_

extern uint32 get_nw_attrib_dword(int volume, char *unixname, struct stat *stb);
extern int    set_nw_attrib_dword(int volume, char *unixname, struct stat *stb, uint32 attrib);
extern int    set_nw_attrib_byte (int volume, char *unixname, struct stat *stb, int battrib);
extern void   set_nw_archive_bit(int volume, char *unixname, int dev, ino_t inode);

extern int    set_nw_trustee(int dev, ino_t inode, uint32 id, int trustee);


extern void   free_nw_ext_inode(int volume, char *unixname, int dev, ino_t inode);
#endif

