Een aantal vragen en antwoorden. ( Mendel Mobach <mendel@mobach.nl> )

V: Welke Novell DOS programma's moet ik minimaal hebben?
A: Het minimun vereiste is LOGIN.EXE
   Andere handige programma's zijn:
   MAP.EXE      wordt gebruikt om server volume's te verbinden met een
                schijfletter.
   CAPTURE.EXE  wordt gebruikt om een printer-poort te verbinden met
                een printer op de mars/novell server.
   SYSCON.EXE   wordt gebruikt voor het beheren van de server.

V: Heb ik het device '/dev/ipx' nodig? Ik zie verwijzingen vanuit de mars_nwe.
A: Als je Linux gebruikt, NEE. Dit device is alleen nodig indien men een OS
   gebruikt met TLI-interfaces, b.v. Unixware (tm).

V: Ik begrijp entry 12/13 in de configuratie niet.
   Wat gebeurt er als ik hier niks opgeef?
   Wordt de '/etc/passwd' of '/etc/shadow' gebruikt?
A: Het wachtwoord in 12/13 wordt versleuteld opgeslagen in de bindery,
   De wachtwoorden uit de '/etc/passwd' of '/etc/shadow' worden alleen gebruikt
   als men werkt met 'unencryted login calls', oftewel ongecodeerde wachtwoorden
   vanaf de client.

V: Welke rol speelt entry '15' in de configuratie?
A: In deze entry staat een wachtwoord, wat alleen wordt gebruikt als men
   automatische UNIX naar mars_nwe usermapping heeft aanstaan, oftewel als
   men in unix een gebruiker toevoegt, en daarna de mars_nwe herstart wordt
   die gebruiker ook in de bindery opgenomen en krijgt hij het standaard
   wachtwoord.

