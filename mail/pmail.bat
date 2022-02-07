@echo off

rem This batch file gets called when a dos user wants to run pmail from
rem a menu or command line. It relies on I: mapped to his user dir
rem and a valid pmail.ini that points to the correct gateways for
rem posting to be picked up with ohpostme.
rem It calls marsmail.exe to convert the unix mail file into readable
rem pmail messages. Note: The look for new mail option under pmail 
rem wont work, exit pmail and restart it to have marsmail run again...
rem Use a unix2dos before using this file...
rem by linking the unix /var/spool/mail to SYS:/unixmail the user
rem can only access his own mailbox. Looks fairly secure...
rem 1/1000000 chance that user runs marsmail during a mail delivery
rem that might damage the last message coming in... Anybody have any
rem ideas?

rem might check the CaSEseNsitivity of ohpostme. pmail gets created from
rem skel...

if not exist i:\pmail\nul mkdir i:\pmail > nul
if not exist i:\pmail\out\nul mkdir i:\pmail\out > nul
if not exist i:\pmail\in\nul mkdir i:\pmail\in > nul

rem variable name get's set in the master login script to the user's
rem login name.
set pmuser=%name%

Echo Please wait -- converting mail from unix to dos format...
unix2dos f:\unixmail\%name% > i:\pmail\in\mailfile.dos

rem zero.txt is a file zero bytes long but MUST exists!
rem J: points to mapping where applications reside..
rem f:\unixmail is a ln -s /var/spool/mail subdirofmarssysvolume

copy j:\dos\net\pmail\zero.txt f:\unixmail\%name% > nul
rem The line above resets the mail file to zero after converting it to
rem pmail format.

j:\dos\net\pmail\marsmail.exe
rem Marsmail is the program that strips the unix mailfile appart and generates
rem a index file and loose message files... Source is included... Please
rem recompile to your needs...

i:
cd \pmail\in

rem please correct the path below to point to your pmail executable.
j:\dos\net\pmail\pmail.exe -a

