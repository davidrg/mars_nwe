/* inserted by Boris Popov <bp@butya.kz> */
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

/* IPX */
#include <netipx/ipx.h>
#include <netipx/ipx_if.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "emutli.h"



static void	rt_xaddrs __P((caddr_t, caddr_t, struct rt_addrinfo *));
static int	if_ipxscan __P((int addrcount, struct sockaddr_dl *sdl, struct if_msghdr *ifm,
		    struct ifa_msghdr *ifam,struct ipx_addr *addr));


/*
 * Find IPX interface. 
 * ifname specifies interface name, if NULL search for all interfaces
 *        if ifname[0]='0', also all interfaces, but return its name
 * addr   on input preferred net address can be specified or 0 for any,
 *        on return contains full address (except port)
 * returns 0 if interface was found
 */
int
ipx_iffind(char *ifname,struct ipx_addr *addr){
	char name[32];
	int all=0, flags, foundit = 0, addrcount;
	struct	if_msghdr *ifm, *nextifm;
	struct	ifa_msghdr *ifam;
	struct	sockaddr_dl *sdl;
	char	*buf, *lim, *next;
	size_t	needed;
	int mib[6];
	
	if( ifname!=NULL ) {
	    strncpy(name,ifname,sizeof(name)-1);
	    if( name[0]==0 )
		all=1;
	} else
	    all = 1;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_IPX;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0)
		return(1);
	if ((buf = malloc(needed)) == NULL)
		return(1);
	if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
		free(buf);
		return(1);
	}
	lim = buf + needed;

	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			flags = ifm->ifm_flags;
		} else {
			fprintf(stderr, "if_ipxfind: out of sync parsing NET_RT_IFLIST\n");
			fprintf(stderr, "expected %d, got %d\n", RTM_IFINFO, ifm->ifm_type);
			fprintf(stderr, "msglen = %d\n", ifm->ifm_msglen);
			fprintf(stderr, "buf:%p, next:%p, lim:%p\n", buf, next, lim);
			free(buf);
			return(1);
		}

		next += ifm->ifm_msglen;
		ifam = NULL;
		addrcount = 0;
		while (next < lim) {
			nextifm = (struct if_msghdr *)next;
			if (nextifm->ifm_type != RTM_NEWADDR)
				break;
			if (ifam == NULL)
				ifam = (struct ifa_msghdr *)nextifm;
			addrcount++;
			next += nextifm->ifm_msglen;
		}

		if (all) {
			if ((flags & IFF_UP) == 0)
				continue; /* not up */
			strncpy(name, sdl->sdl_data, sdl->sdl_nlen);
			name[sdl->sdl_nlen] = '\0';
		} else {
			if (strlen(name) != sdl->sdl_nlen)
				continue; /* not same len */
			if (strncmp(name, sdl->sdl_data, sdl->sdl_nlen) != 0)
				continue; /* not same name */
		}

		foundit=if_ipxscan(addrcount, sdl, ifm, ifam, addr);
		if( foundit ) {
			if( ifname!=NULL && ifname[0]==0) {
			    strncpy(ifname,sdl->sdl_data, sdl->sdl_nlen);
			    ifname[sdl->sdl_nlen]=0;
			}
			break;
		}
	}
	free(buf);

	return foundit ? 0:1;
}


int
if_ipxscan(addrcount, sdl, ifm, ifam, addr)
	int addrcount;
	struct	sockaddr_dl *sdl;
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct ipx_addr *addr;
{
	struct	rt_addrinfo info;
	struct sockaddr_ipx *sipx;
	int s;

	if ((s = socket(AF_IPX, SOCK_DGRAM, 0)) < 0) {
		perror("ifconfig: socket");
		return 0;
	}

	while (addrcount > 0) {
		info.rti_addrs = ifam->ifam_addrs;
		/* Expand the compacted addresses */
		rt_xaddrs((char *)(ifam + 1), ifam->ifam_msglen + (char *)ifam, &info);
		addrcount--;
		ifam = (struct ifa_msghdr *)((char *)ifam + ifam->ifam_msglen);
		if (info.rti_info[RTAX_IFA]->sa_family == AF_IPX) {
			sipx = (struct sockaddr_ipx *)info.rti_info[RTAX_IFA];
			if( ipx_nullnet(sipx->sipx_addr) ) continue;
			if( ipx_nullnet(*addr) || 
			    ipx_neteq(sipx->sipx_addr,*addr) ) {
			    *addr=sipx->sipx_addr;
			    close(s);
			    return(1);
			}
		}
	}
	close(s);
	return(0);
}
/*
 * Expand the compacted form of addresses as returned via the
 * configuration read via sysctl().
 */

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

static void
rt_xaddrs(cp, cplim, rtinfo)
	caddr_t cp, cplim;
	struct rt_addrinfo *rtinfo;
{
	struct sockaddr *sa;
	int i;

	memset(rtinfo->rti_info, 0, sizeof(rtinfo->rti_info));
	for (i = 0; (i < RTAX_MAX) && (cp < cplim); i++) {
		if ((rtinfo->rti_addrs & (1 << i)) == 0)
			continue;
		rtinfo->rti_info[i] = sa = (struct sockaddr *)cp;
		ADVANCE(cp, sa);
	}
}

