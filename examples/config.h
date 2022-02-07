/* config.h: 03-Jan-96 */
/* some of this config is needed by make, others by cc */
#define FILENAME_NW_INI  "/etc/nwserv.conf" /* full name of ini (conf) file */
#define PATHNAME_PROGS   "/sbin"     /* path location of progs       */
#define PATHNAME_BINDERY "/etc"      /* path location of bindery     */

#define MAX_CONNECTIONS   5          /* max. Number  of Connections  */
                                     /* must be < 256   !!!          */

#define MAX_NW_VOLS      10          /* max. Volumes                 */

#define MAX_NET_DEVICES   1          /* max. Netdevices, frames      */
#define IPX_DATA_GR_546   1          /* allow ipx packets > 546+30 Byte */

#define MAX_NW_ROUTES    50          /* max. networks (internal + external) */


