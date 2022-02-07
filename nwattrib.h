/* nwattrib.h  01-Feb-98 */

#ifndef _NWATTRIB_H_
#define _NWATTRIB_H_

extern uint32 get_nw_attrib_dword(struct stat *stb, int voloptions);
extern int    set_nw_attrib_dword(struct stat *stb, int voloptions, uint32 attrib);
extern int    set_nw_attrib_byte (struct stat *stb, int voloptions, int   battrib);
extern void   set_nw_archive_bit(int dev, ino_t inode);

extern int    set_nw_trustee(int dev, ino_t inode, uint32 id, int trustee);


extern void   free_nw_ext_inode(int dev, ino_t inode);
#endif

