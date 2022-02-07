/* nwattrib.h  30-Apr-98 */

#ifndef _NWATTRIB_H_
#define _NWATTRIB_H_

/* <-------------- File Attributes -------------> */
#define FILE_ATTR_NORMAL         0x00000000
#define FILE_ATTR_R              0x00000001
#define FILE_ATTR_H              0x00000002
#define FILE_ATTR_S              0x00000004
#define FILE_ATTR_DIR            0x00000010
#define FILE_ATTR_A              0x00000020
#define FILE_ATTR_SHARE          0x00000080

#define FILE_ATTR_RENAME_INH     0x00020000
#define FILE_ATTR_DELETE_INH     0x00040000

extern uint32 get_nw_attrib_dword(int volume, char *unixname, struct stat *stb);
extern int    set_nw_attrib_dword(int volume, char *unixname, struct stat *stb, uint32 attrib);
extern int    set_nw_attrib_byte (int volume, char *unixname, struct stat *stb, int battrib);
extern int    set_nw_attrib_word(int volume, char *unixname, struct stat *stb, int wattrib);
extern void   set_nw_archive_bit(int volume, char *unixname, int dev, ino_t inode);


extern void   free_nw_ext_inode(int volume, char *unixname, int dev, ino_t inode);
#endif

