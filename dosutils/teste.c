int main()
{
   char *fn="F.$LN";
   char *string="ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890abcdefghijklmnopqrstuvwxyz";
   int  result;
   struct stat stbuff;
   long offset;
   char buff[1024];
   char readbuff[500];
   int j;
   int  fd=creatnew(fn, S_IREAD |S_IWRITE);
   if (fd > -1) {
     printf("Konnte Date erzeugen\n");
     close(fd);
   } else {
     printf("Konnte Datei nicht erzeugen\n");
   }
   fd = open(fn, O_RDWR|O_CREAT|O_TRUNC|O_DENYNONE, 0666);
   memset(buff, 0, sizeof(buff) );
   strcpy(buff, string);
   write(fd, buff, strlen(buff));
   close(fd);
   _chmod(fn, 1, _chmod(fn, 0) | 0x80 );
   stat(fn, &stbuff);
   printf("Filesize Åber stat =%ld\n", stbuff.st_size);
   fd = open(fn, O_RDWR | O_BINARY |O_DENYNONE);

   offset = lseek(fd, 0L, SEEK_END);
   printf("Filesize Åber lseek =%ld\n", offset);
   write(fd, buff, strlen(buff));

   lseek(fd, 0L, SEEK_SET);
   for (j=0; j < strlen(buff)*2; j++){
     read(fd, readbuff, 1);
     printf("BUFF = %c\n",  readbuff[0]);
   }
   lseek(fd, 1L, SEEK_SET);
   for (j = 0; j < 20; j++){
     write(fd, buff+j, 1);
   }

   for (j=0; j <60; j++){
     lseek(fd, (long) j, SEEK_SET);
     read(fd, readbuff, 2);
     printf("BUFF = %c, %c \n", readbuff[0], readbuff[1]);
   }

   lseek(fd, 10L, SEEK_SET);
   read(fd, buff, 1);
   printf("BUFF[10] = %c\n", buff[0]);
   result=lock(fd,  100L, 1L);
   printf("lock result = %d\n", result);
   result=unlock(fd,  100L, 1L);
   printf("unlock result = %d\n", result);
   close(fd);

   fd = open(fn, O_BINARY|O_RDWR|O_CREAT|O_TRUNC|O_DENYNONE, 0666);
   if (fd > -1) {
     int bufflen;
     strcpy(buff, "d:..\\marlib\\c0l.obj+");
     strcat(buff, "\r\n");
     strcat(buff, "x");
     strcat(buff, "\r\n");
     strcat(buff, "x");
     strcat(buff, "\r\n");
     strcat(buff, "/c/x");
     strcat(buff, "\r\n");
     strcat(buff, "d:..\\marlib\\EMU.LIB+");
     strcat(buff, "\r\n");
     strcat(buff, "d:..\\marlib\\mathl.lib+");
     strcat(buff, "\r\n");
     strcat(buff, "d:..\\marlib\\cl.lib");
     bufflen=strlen(buff);
     printf("bufflen = %d, buff=%s\n", bufflen, buff);
     write(fd, buff, bufflen);
     close(fd);
     fd = open(fn, O_TEXT|O_RDONLY);
     if (fd > -1) {
       char *p=readbuff;
       int  anz = 0;
       memset(readbuff, 0, sizeof(readbuff) );
       while (read(fd, p, 1) == 1){
         anz++;
         p++;
       }
       printf("read = %d, buff=%s\n", anz, readbuff);
       close(fd);
     }
   }
  /*
  result = detach(1);
  printf("Detach result=0x%x", result);
  */
  return(0);
}
