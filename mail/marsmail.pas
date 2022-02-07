program Marsmail;
uses dos;

var mailfile, numfile, outfile: text;
     number, miline, moline, sl, slc: string;   {mail-in, mail-out}
     nfile, odir, mfile: string; {numberfile, output dir, mailfile}
     numbint: longint;
     o, code, mi, i,j: integer; {mailindex}
     prevline, shortline,v : boolean; {v=verbose}


Function FileExists(FileName: String): Boolean;
{ Boolean function that returns True if the file exists;otherwise,
 it returns False. Closes the file if it exists. }
var
 F: text;
begin
 {$I-}
 Assign(F, FileName);
 FileMode := 0;  { Set file access to read only }
 Reset(F);
 Close(F);
 {$I+}
 FileExists := (IOResult = 0) and (FileName <> '');
end;  { FileExists }


Begin
v:=false;
nfile:='i:\pmail\in\numfile';
mfile:='i:\pmail\in\mailfile.dos';
odir:='i:\pmail\in';
for i:=1 to paramcount do
begin
  sl:=paramstr(i);
  for j:=1 to length(sl) do sl[j]:=upcase(sl[j]);
  if (pos('/N=',sl)=1) then nfile:=copy(sl,3,length(sl)-3);
  if (pos('/O=',sl)=1) then odir:=copy(sl,3,length(sl)-3);
  if (pos('/M=',sl)=1) then mfile:=copy(sl,3,length(sl)-3);
  if (pos('/V',sl)=1) then v:=true;
  if (pos('/?',sl)=1) or (pos('/H',sl)=1) or (pos('/HELP',sl)=1) then
  begin
    Writeln('Copyright 1996/9 Dud software');
    Writeln;
    Writeln('Purpose: Creates *.cnm files from a unix mail file for Pegasus');
    Writeln('Usage: marsmail.exe [/n=x:\numb\file] [/m=y:\mail\file]');
    Writeln('                    [/o=z:\base\out\dir] [/v] [/?|h|help]');
    Writeln('Where:');
    Writeln('  /n = numberfile, /m=singlemailfile, /o=outputbasedir');
    Writeln('  /v = verbose messaging mode,  /?|/h|/help = this help screen');
    writeln('And: numberfile contains a single 8 digit number e.g. 00000019');
    writeln('     singlemailfile is the unix /var/spool/mail/userfile unix2dos');
    writeln('     converted. outdir is the dir where *.cnm files should be created');
    writeln('     without the trailing \');
    writeln;
    writeln;
    writeln;
    halt(2);
  end;
end;

{ Open the numfile to read in a number }

   if v then writeln('Opening Number file...');
   if FileExists(nfile) then
    Begin
      {$I-}
      assign (numfile,nfile);
      reset(numfile);
      if doserror >0 then begin writeln('Error reading '+nfile); halt(1); end;
      {$I+}
      if v then writeln('Reading number file...');
      readln(numfile,number);
      close(numfile);
    end
   else
     Begin
       assign (numfile,nfile);
        {$I-}
       Rewrite(numfile);
        {$I+}
       if doserror >0 then begin writeln('Error rewriting '+nfile); halt(1); end;
       number:='00000000';
       if v then writeln('Creating a new number file');
       writeln(numfile,number);
       Close(numfile);
     end;
    val(number,numbint,code);
    numbint:=numbint+1;
    str(numbint:8,number);
    if v then writeln('Converting spaces to zeroes');
    for i:=1 to length(number) do if number[i]=' ' then number[i]:='0';


{ Open the converted mailfile and start reading it }
    slc:='|/-\|/-\';
    sl:='|';
    o:=2;
    assign(mailfile,mfile);
    assign(outfile,odir+'\'+number+'.cnm');
    {$I-}
    if v then writeln('Opening mailfile for reading...');
    reset(mailfile);
    {$I+}
    if doserror >0 then begin writeln('Error opening '+mfile); halt(1); end;
    if eof(mailfile) then
    begin
     close(mailfile);
     halt(3);
    end;
    {$I-}
    if v then writeln('Opening '+odir+'\'+number+'.cnm file for writing....');
    rewrite(outfile);
    {$I+}
    if doserror >0 then begin writeln('Error rewriting '+odir+'\'+number+'.cnm'); halt(1); end;
    mi:=0;
    shortline:=false;
    writeln('Creating mail messages from UnixMail file to Pegasus mail messages.');
    write('Working \');
    if v then write(' Line:');

    while not eof(mailfile) do
    begin
      readln(mailfile,miline);
      if v then write(mi,':');
      prevline:=shortline;
      shortline:=false;

      if (length(miline)<2) then
      begin
       shortline:=true;
      end;
      mi:=mi+1;
      if ( (not v) and ((mi mod 10)=0)) then
      begin
       write(char(8)+sl);
       sl:=slc[o];
       o:=o+1;
       if o=9 then o:=1;
      end;
      if (mi>5) and (prevline=true) then begin
        if (((pos('Received:',miline)=1)
          or (pos('Return-Path:', miline)=1)
          or (pos('From ',miline)=1)
          or (pos('From:',miline)=1))
         and (prevline=true)) then
            begin {to flush file and start a new one}
              close(outfile);
              numbint:=numbint+1;
              str(numbint:8,number);
              for i:=1 to length(number) do if number[i]=' ' then number[i]:='0';
              assign(outfile,odir+'\'+number+'.cnm');
              rewrite(outfile);
              if v then writeln;
              if v then writeln('Finished with message no ',numbint-1,'...');
              mi:=0;
            end;
      end;
      writeln(outfile,miline);
    end;
    if v then writeln;
    if v then writeln('Closing output files');
    close(outfile);
    if v then writeln('Closing mail file');
    close(mailfile);
    assign (numfile,nfile);
    if v then writeln('Updating number file');
    rewrite (numfile);
    writeln(numfile,number);
    close(numfile);
    writeln(' ');
    if v then writeln('Program finished.');
end. {of program}

