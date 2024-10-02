# MARS_NWE

This is Mars_nwe, a free NetWare emulator for Linux originally written by 
Martin Stover of Marburg, Germany.

## Current Development Status
The last release by the original author was on 1 September 2000. 
Marco Cavallini of Koan Software produced version 0.99.pl21 on or around 14 June 2003.
Mario Fetka ([@geos_one](https://gitea.disconnected-by-peer.at/geos_one)) released version 0.99.pl22 on 21 May 2012 and 0.99.pl23 on 5
November 2013

No significant work has been done on this in public since 2013. While it all builds and runs on
modern linux systems (surprisingly) there are a lot of build warnings and many missing
features to be dealt with as well as bugs to be found and squashed.

Mario Fetka has a [MARS_NWE repository](https://gitea.disconnected-by-peer.at/mars_nwe/mars-nwe) which
is likely to be more up-to-date than the code here.

## About This Repository
This repository was initially produced to see what the differences were between the few
most recent versions of mars_nwe, in particular the versions not produced by the
original author. Most other versions going back to 0.96 have also been imported to see
how it changed over time. The source archives used to build this repo can be found
[here](https://ftp.zx.net.nz/pub/archive/novell/mars_nwe_dist/).

## What can Mars_nwe do?
From the old [README file](README): 

- mars_nwe is a very functional clone of a NetWare server that runs
  under Linux. It works fine with the usual DOS client software that
  normally comes with your NetWare server.

- mars_nwe offers file, bindery and print services for NetWare client
  software.

- mars_nwe does not include any user license restrictions. You can
  increase mars_nwe's licenses by simply recompiling it, and you can
  start any number of mars_nwe's on your network!

- mars_nwe includes a RIP/SAP daemon that turns your Linux box into a
  fine IPX router.


Related packages include:

- mars_dosutils: Some utilities that should free you from having to
  use proprietary utilities when you want to use mars_nwe with DOS
  clients. These are now in the dosutils directory of this repository

- [ncpfs](https://github.com/EnzephaloN/ncpfs-module): a linux-only 
  filesystem allowing you to mount volumes exported by NetWare servers 
  on your linux box.

- [ipx kernel module for Linux](https://github.com/pasis/ipx): this is
  required to run mars_nwe on versions of Linux newer than 4.17.

## WARNING Security
This code has not been actively maintained in over two decades. The network protocol it
implements is even older than that. **THERE WILL BE SECURITY VULNERABILITIES.** If you 
decide to run this please do so on a private network only.

In particular, since version 0.99.pl9 mars_nwe has made filesystem calls as the root
user in order to handle trustees so this may be a source of security issues.

## Installing

With [the IPX Kernel Module](https://github.com/pasis/ipx) installed you should be able to 
just go:
```shell
cmake .
make
sudo make install
```

Once thats done you'll need to edit `/usr/local/etc/mars_nwe/nwserv.conf` to:
- Give your server a name (section 2)
- Set ethernet frame types and IPX network numbers (section 4, you may be able to skip 
  this if you have no other NetWare servers on your LAN)
- Give the supervisor user a password (section 12)

You should probably read all the comments in the server configuration file in case there
are any other settings you wish to customise.

You'll also want to edit `/var/mars_nwe/SYS/public/net$log.dat` and replace the
default login script with something sensible like:
```
MAP INS S1:=SYS:PUBLIC
MAP *1:=SYS:
```

Once all that's done you should be able to start mars_nwe by running the following as root:
```shell
mkdir -p /var/log/mars_nwe
mkdir -p /var/run/mars_nwe
nwserv
```
To make it start automatically you can put the above in `/etc/rc.local` or, even better,
write a systemd unit file.

Lastly you'll need a client. The best one for DOS and Windows 3.11 is probably
[Client32 v2.71](https://ftp.zx.net.nz/pub/archive/novell/clients/client32_2.71_dos_win3x/dw271e.exe) 
from February 1999 which uses at most 4K of conventional memory (386SX or better CPU 
and a few MB of extended memory required). For other operating systems (or older DOS 
machines) you can grab another client from [here](https://www.zx.net.nz/netware/client/). Note
that Mars_NWE won't work with the Mac client as that needs NDS (NetWare 4.0+) which Mars_NWE
does not implement.

## NetWare DOS & OS/2 Utilities
mars_nwe only comes with the bare minimum required to login and map drives on DOS. You can get
some other utilities (such as flag and ncopy) from the NetWare DOS Client Kit v3.01 
(released in 1990) which was made freely available by Novell. You can get it from 
[this page](http://www.zx.net.nz/netware/client/dos-netx.shtml) (DSWIN3.ZIP and 
DSWIN4.ZIP). These utilities would normally live in `/var/mars_nwe/SYS/public/`

For OS/2, some of the client kits for OS/2 include updated 16bit OS/2 utilities
(usually the OS2UTIL disks) which may fill some gaps. Four EXEs (attach, login, map, slist) 
should go in `/var/mars_nwe/SYS/login/OS2/` while the rest go in `/var/mars_nwe/SYS/public/OS2/`.

For other utilities such as SysCon you may need to obtain a proper copy of 
NetWare 3.11 or 3.12.

## Managing Users and Login Scripts
For NetWare 3.x servers this is normally done with the SYSCON utility for DOS. Mars_NWE doesn't
include any replacement for this utility and the original Novell one is probably out of bounds
unless you've got a NetWare 3 license.

One alternative is [WnSyscon](https://web.archive.org/web/20050828165409/http://www.amcsoft.demon.co.uk/wnsyscon.htm), 
a 16bit windows SYSCON replacement. Novell actually bundled the registered version of 
this with NetWare 3.2 as their [Graphical SYSCON Utility](https://support.novell.com/techcenter/articles/ana19980403.html).
You can grab the ShareWare version from [here](https://ftp.zx.net.nz/pub/archive/novell/3rdparty/admin/wnsyscon/) - it
seems to be fully functional aside from a few nag screens. Unfortunaltely buying a copy of NetWare 3.2 is probably
the only way to get a proper registered version of this today unless the original author can be contacted somehow.

## Where To Get Help

There is some very old documentation available [here](doc) which may be of interest. 
The old [linware mailing list archives](https://marc.info/?l=linware&r=1&w=2) may still
contain useful information as well. There is also a few additional notes [here](http://www.zx.net.nz/netware/server/mars.shtml).
If you can't find a solution to a problem in any of those places the only remaining 
option is probably reading the code.
