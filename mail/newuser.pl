#!/usr/bin/perl
#
# New user script
#
# This script gets called from a web form where users apply for usership
# on your server. It creates a executable file that you as admin can
# just run and the user gets created without headaches.

$| = 1;

if ($ENV{'REQUEST_METHOD'} eq 'POST')
{

$base="/hamster/home";


chop($date = `date`);
chop($ddd = `date +\%j\%H\%M`);
print "Content-type: text/html\n\n";
print <<"HTML";
<HTML>
<TITLE>New user details completed</TITLE>
<BODY BACKGROUND=/yourown.gif>
<IMG SRC="/yourlogo.gif" ALT="MY OWN LOGO" HEIGHT=114 WIDTH=640><BR>
<H1>New User Application</H1>
HTML

  read(STDIN, $buffer, $ENV{'CONTENT_LENGTH'});
  @pairs=split(/&/, $buffer);
  foreach $pair(@pairs)
  {
	($name, $value) = split(/=/, $pair);
	$value =~ tr/+/ /;
	$value =~ s/%([a-fA-F0-9][a-fA-F0-9])/pack("C", hex($1))/eg;
	$contents{$name} = $value;
        $contents{'remoteip'} = $ENV{'REMOTE_ADDR'};
#        print "$contents{$name} = $value <BR>\n";
  }
# Check if some-one gave in a bullshit form.
if (($contents{'fname'} eq '') or ($contents{'sname1'} eq ''))
{
print <<"HTML";
<HR>
Dear person working from machine or proxy $ENV{'REMOTE_ADDR'}, do WE have some
news for you: <P>

Oh boy! You cannot even complete a form! How do you think it makes you look?
At least fill in a Full name and one username, else this form is useless! <P>

Please take some time and consider if you are <I>really</I> capable of using a 
computer and if you <I>should</I> apply for usership. 
If your previous attempt to fill in a form turned out like this 
then we do not consider you a good canidate.
Take a deep breath.

Use your browsers BACK button and 
try again!
<HR>
</BODY>
</HTML>
HTML
exit;
}
else { ; }
print <<"HTML";
Thank you for taking the time to complete the form. Please read the following
information carefully. Use your browsers save function to save this page to
a file. It will act as a receipt of your new (possible) user account.<P>

If we think that the IP $contents{'remoteip'} that you appear to work from is
not in our domain, extra effort will be made to ensure that
you are in fact a valid applicant.<P>

<P> This is how your details will appear to others:
<CENTER><TABLE BORDER=1 WIDTH=80%>
<TR><TD> Name </TD><TD>
$contents{'fname'}
HTML

if ($contents{'addtitle'} eq 'addmytitle') 
{
print <<"HTML";
 ,$contents{'title'} 
HTML
}
else
{ ; }

print <<"HTML";
 $contents{'fname'}
</TD></TR>
<TR><TD>
Office</TD><TD>$contents{'office'}</TD></TR>
<TR><TD>Username choices:</TD><TD><B>$contents{'sname1'}</B>, 
<B>$contents{'sname2'}</B> or <B>$contents{'sname3'}</B> </TD></TR>
<TR><TD>Work Tel: $contents{'wtel'}</TD><TD>Home Tel: $contents{'htel'}</TD></TR> 
<TR><TD>Stud/ID number:</TD><TD> $contents{'idnum'}</TD></TR>
<TR><TD>Birthdate:</TD><TD> $contents{'bidate'}</TD></TR>
</TABLE></CENTER><P>

You will most probably granted the first choice of username, unless it clashes
with an existing name, is too short or too close to a swearing word.
<B><P>PLEASE PHONE netadmin at 555-5555 to arrange a password for your account</B>

$contents{'reff'} is your reference person or study leader that will be 
contacted at email address <B>$contents{'refmail'}</B>. 
Your account might be stopped if the reference person does not know you or give us a good reason to do so.</B><P>

Reason why you would like to be a network user:
<QUOTE>$contents{'whyi'}</QUOTE><P>
You rate yourself a $contents{'skill'} computer user that will be using 
$contents{'os'} as your primary operating system. You also have some knowlage on
$contents{'oos1'} $contents{'oos2'} $contents{'oos3'} $contents{'oos4'} 
$contents{'oos5'} $contents{'oos6'} $contents{'oos7'} $contents{'oos8'} 
$contents{'oos9'}<P>
Please read the section on operating systems carefully to give you a broader understanding on how to connect to the network.
HTML

if ($contents{'helpme'} eq 'yes')
{
print <<"HTML";
  You are also interrested in giving a helping hand with network related problems. <B>Good!</B> As soon as there is a problem near you, <I>you</I> will be seen as the
solution. We'll keep in touch.
HTML

}
else
{ ; }

print <<"HTML";
<P> Your feedback on the questionaire will be stored in a database for future
refference. These details will give us direction on how to expand the network
services.
<P> You rate the following services:
<CENTER><TABLE BORDER=1 WIDTH=80%}
<TR><TD>E-Mail and other communication tools: </TD><TD>$contents{'s1'}</TD></TR>
<TR><TD>WWW for info search and research:</TD><TD>$contents{'s2'}</TD></TR>
<TR><TD>Wordprocessing and office based work:</TD><TD>$contents{'s3'}</TD></TR>
<TR><TD>Cruising internet for fun and pleasure</TD><TD>$contents{'s4'}</TD></TR>
<TR><TD>Engineering apps CADCAM, PCB etc.</TD><TD>$contents{'s5'}</TD></TR>
<TR><TD>Shareware source</TD><TD>$contents{'s6'}</TD></TR>
<TR><TD>Programming apps, VisC, Pascal, VB etc</TD><TD>$contents{'s7'}</TD></TR>
<TR><TD>Storage of files and userdata</TD><TD>$contents{'s8'}</TD></TR>
<TR><TD>Exam results electronic lecture notes.</TD><TD>$contents{'s9'}</TD></TR>
</TABLE></CENTER>

<P>You would like to see the following services at Our Site (Let's pray together that it will happen soon...):<BR>
<QUOTE>$contents{'future'}</QUOTE><P>

You think that $contents{'numstaff'} staff member(s) is/are needed to maintain our network.<P>
According to you an amount of $contents{'hypopay'} is fair for a network account per year, as long as you can get the  no $contents{'hserv'} service you asked for.
<P>
<HR>
Thank you for submitting this request for a new user account.
Your account will be processed by 
<A HREF="mailto:adminuser\@your.domain.name">adminuser\@your.domain.name</A> or 
<A HREF="mailto:bigboss\@your.domain.name">bigboss\@your.domain.name</A>. Please give us a few days to decide whether you would make a suitable user. It normally takes only a few minutes to create the account if one of us is close-by.
<HR>
<h1> Futher information </h1>
You automagically get the following as a bonus:
<UL>
<LI> Electronic mail (Your email account will most probably be $contents{sname1}\@your.domain.name)
<LI> Your own WWW page which you can customize from anywhere.
<LI> Your own public ftp site
<LI> Secure storage space for your data on the file server
<LI> Access to our software and shareware
<LI> Remote access to your data, where-ever you are roaming about.
<LI> A public accessible read-write file area to exchange data with others.
<LI> Automated secure backups of your data
</UL>
<P>
Follow <A HREF="/main/ohhelp.html">this link</a> to get more detailed information on the services and how to make use of it.

<HR>
This automated service creates a file that could create your new account with
a single keypress, if the network administrator feels like it. ALL you need to
do on your own is to visit the network center to select a password for yourself.Even with the account created it will not be active before you either phone or visit your network admin to set up your password. 
<HR>
<FONT SIZE=3><I>Last updated 960916</I></FONT>
</BODY>
</HTML>

HTML

# Now create email message and mail it.
$subject = "New User application - $date";
$sendto = "adminuser\@your.domain.name, bigboss\@your.domain.name";

# open named pip to send mail
open (MAIL, "| /usr/sbin/sendmail $sendto") || die "Cannot send mail: $!\n";
# this selects the open handle to send email:
select(MAIL);
print <<"EMAIL";
Date: $date
From: root\@your.domain.name
Return-Path: <$contents{'refmail'}>
To: $sendto
Subject: $subject

This is an automated reply to the http://yourweb.site.name/ohform.html script.

New user application details - $date
Please note: A file was created under $contents{'base'}/httpd/newuser/$contents{'sname1'}.$ddd 
that can be directly executed by root in order to create the account.
EMAIL

if ($contents{'addtitle'} eq 'addmytitle') 
{ 
print <<"EMAIL";
Full name: $contents{'title'} $contents{'fname'}
EMAIL

}

else 
{ print <<"EMAIL";
Full name: $contents{'fname'}
EMAIL
}

print <<"EMAIL";
Working from machine or proxy: $contents{'remoteip'}
Title: $contents{'title'}

Username1: $contents{'sname1'}
Username2: $contents{'sname2'}
Username3: $contents{'sname3'}

Office: $contents{'office'}
WorkPhone: $contents{'wtel'}
HomePhone: $contents{'htel'}
Birthdate: $contents{'bidate'}
Stud/ID number: $contents{'idnum'}
Refference Person: $contents{'reff'}
Refference Email : $contents{'refmail'}
Would like to help with network problems?: $contents{'helpme'}

Skill: $contents{'skill'}
Primary operating System: $contents{'os'}
Knowlage on: $contents{'oos1'} $contents{'oos2'} $contents{'oos3'} $contents{'oos4'} $contents{'oos5'} $contents{'oos6'} $contents{'oos7'} $contents{'oos8'} $contents{'oos9'}

Usership plea: $contents{'whyi'}

Questionaire
============
Email: $contents{'s1'}
WWW: $contents{'s2'}
Office: $contents{'s3'}
FunGames: $contents{'s4'}
Apps: $contents{'s5'}
Shareware: $contents{'s6'}
Programming: $contents{'s7'}
Storage: $contents{'s8'}
Exam and notes: $contents{'s9'}
Staffnum: $contents{'numstaff'}
YearFee: $contents{'hypopay'}
ServiceLevel: $contents{'hserv'}
ConnectionValue1: $contents{'userv'}
ConnectionValue2: $contents{'nserv'}
MonthAccount: $contents{'aserv'}
AccountMethod: $contents{'aget'}
AccountExtra: $contents{'aret'}

Services interrested in
=======================
BirthdayNotify:  $contents{'c0'}
VoiceMail: $contents{'c1'}
PinupNotice: $contents{'c2'}
AppointRemind: $contents{'c3'}
GlobalPhoneBook: $contents{'c4'}
LocalIRCserver: $contents{'c5'}
AutomatedSoftwareInstalls: $contents{'c6'}
VoiceDictation: $contents{'c7'}
ExamsonComputers: $contents{'c8'}
VideoLectures: $contents{'c9'}
VideoConference: $contents{'c10'}
SharedAppointment: $contents{'c11'}
AnimatedMultimedia: $contents{'c12'}
StudentResults: $contents{'c13'}
HWSFTWMonitor: $contents{'c14'}


Please phone the user and ask him/her to come and select a password for themselves.

From your friendly automated Webserver running Mars.
EMAIL
close(MAIL);

# Append questionaire to the log.
open(CATFILE, ">> /var/log/newusr.questionaire") || die "Cant update newuser questionaire list";
select(CATFILE);
print "$contents{'fname'}:";
print "$contents{'skill'}:";
print "$contents{'helpme'}:";
print "$contents{'os'}:";
print "$contents{'oos1'}:";
print "$contents{'oos2'}:";
print "$contents{'oos3'}:";
print "$contents{'oos4'}:";
print "$contents{'oos5'}:";
print "$contents{'oos6'}:";
print "$contents{'oos7'}:";
print "$contents{'oos8'}:";
print "$contents{'oos9'}:";
print "$contents{'s1'}:";
print "$contents{'s2'}:";
print "$contents{'s3'}:";
print "$contents{'s4'}:";
print "$contents{'s5'}:";
print "$contents{'s6'}:";
print "$contents{'s7'}:";
print "$contents{'s8'}:";
print "$contents{'s9'}:";
print "$contents{'hserv'}:";
print "$contents{'userv'}:";
print "$contents{'nserv'}:";
print "$contents{'aserv'}:";
print "$contents{'aget'}:";
print "$contents{'aret'}:";
print "$contents{'c0'}:";
print "$contents{'c1'}:";
print "$contents{'c2'}:";
print "$contents{'c3'}:";
print "$contents{'c4'}:";
print "$contents{'c5'}:";
print "$contents{'c6'}:";
print "$contents{'c7'}:";
print "$contents{'c8'}:";
print "$contents{'c9'}:";
print "$contents{'c10'}:";
print "$contents{'c11'}:";
print "$contents{'c12'}:";
print "$contents{'c13'}:";
print "$contents{'c14'}\n";
close(CATFILE);

# Create an autouser script
open(NFILE, "> $contents{'base'}/httpd/newuser/$contents{'sname1'}.$ddd") || die "Cannot create autouser file";
select(NFILE);
print "#/bin/bash\n\n";
print "/usr/sbin/ohmasteruser \"$contents{'sname1'}\" ";
if ($contents{'addtitle'} eq 'addmytitle')
{
print "\"$contents{'title'} ";
}
else
{ print "\""; }
print "$contents{'fname'}\" ";
print "\"$contents{'office'}\" ";
print "\"$contents{'wtel'}\" ";
print "\"$contents{'htel'}\" ";
print "\"$contents{'bidate'}\"\n";
print "\n\n# Other names if it is a clash: $contents{'sname2'}";
print "\n# Other name: $contents{'sname3'}\n";
close(NFILE);
$a=`chmod +x "$contents{'base'}/httpd/newuser/$contents{'sname1'}.$ddd"`;
$a=`chmod o-rwx "$contents{'base'}/httpd/newuser/$contents{'sname1'}.$ddd"`;
$a=`chmod g-rwx "$contents{'base'}/httpd/newuser/$contents{'sname1'}.$ddd"`;
}
exit;
