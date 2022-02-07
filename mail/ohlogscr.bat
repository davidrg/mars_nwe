@ECHO OFF

rem DOS BATCH FILE
rem File is called ohlogscr.bat and gets called with an exit "ohlogscr.bat"
rem from the master mars login script....
rem unix2dos this file before using it.

echo .
rem
rem I: is a map to the user directory.
rem
if not "%name%"=="SUPERVIS" goto aaa
if not exist i:\users.usr goto aaa
i:
cd \
cls
echo Creating new users and destroying old ones.
rem Use makeuser script from novell to create/destroy the new/old users
makeuser users.usr
type users.rpt | more
copy users.old + users.usr users.lst > nul
copy usrpt.old + users.rpt usrpt.lst > nul
copy users.lst users.old > nul
copy usrpt.lst usrpt.old > nul
del users.rpt > nul
del users.usr > nul
f:
pause

:aaa

rem
rem Place your normal household stuff in here......
rem all the variable names below gets created in the master login script

Echo ________________________________________________________________________
echo Welcome %LONGNAME% driving on your %MACHINE% from IPX node %NETADDR%:%NODE%
echo Your %OSTYPE% %OSVER% is far too outdated for THIS powerfull server!
echo Your mailbox is %MBOX% and your temp directory is set to %TEMP%
echo It looks like we're ready to rock and roll!!
Echo ________________________________________________________________________

