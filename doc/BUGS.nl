Als je problemen heb:
-----------------
- Kijk of je in de kernel wel IPX heb geinstalleerd
  CONFIG_IPX=y ( of m en het ipx module is geladen (insmod ipx))
- Kijk of je geen intern IPX netwerk hebt geinstalleerd 'full 
  internal net'. (kernel optie)
  CONFIG_IPX_ITERN=n
- Lees altijd de documentatie: doc/INSTALL[.ger/.nl],
  doc/README[.ger/.nl], doc/FAQS en examples/nw.ini
  Zeer belangrijke secties in nwserv.conf(nw.ini) zijn:
  Sectie  1: Volumes
  Sectie  3: Number of the internal network
  Sectie  4: IPX-devices
  Sectie  5: device flags
  Sectie  6: version-"spoofing"
  Sectie 12: supervisor-login
  Als je niet kunt inloggen, probeer het dan eens met de meegeleverde
  configuratie bestanden, zonder deze te veranderen.
- Belangrijk nieuws staat in doc/NEWS.
- zet de debug switches in sectie 101 .. 10x op '1',
  probeer het opnieuw met het herstarten van nwserv en kijk in het
  log bestand in /tmp/nw.log
  Misschien begrijp je het probleem dan. :)

Sommige kleine aantekeningen voor probleem- of bug-'reporters'.
---------------------------------------------------------------
- Geef je systeem waarop je draait door en de volledige mars_nwe
  versie, bijvoorbeeld:
  mars_nwe versie: 0.99.pl14
  linux-kernel:    2.2.0p9
  het type client: (NCPFS, DOS (NETX/VLM), OS2, WIN, Novell client 
  32 WIN ..)
  de veranderingen in nwserv.conf(nw.ini) en de config.h
- als je nwserv.conf opstuurd, haal het commentaar eruit.
  doe dit door in te tikken 'make showconf' in de mars_nwe source dir
  of doe 'grep "^[ \t*[0-9]" /etc/nwserv.conf'

- vermeld of je client wel loopt met een 'echte Novell-server' 
  (als je er een hebt. ;) )

Bekende problemen / oplossingen:
--------------------------------
- kijk is doc/FAQS
