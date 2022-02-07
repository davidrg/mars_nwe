INSTALLATIE:              ( Mendel Mobach <mendel@mobach.nl>)
Dit is het bestand "INSTALL.nl", een stap-voor-stap handboek voor het
installeren van het mars_nwe-pakket.

(1) Pak een goede ipx-kernel-versie ne compileer die met IPX Support,
    Maar zonder 'Full internal network'. je kun dit doen door op de 
    volgende manier te antwoorden op de volgende vragen tijdens het 
    draaien van "make config":
    
    
       The IPX protocol (CONFIG_IPX) [N/m/y/?] y
       Full internal IPX network (CONFIG_IPX_INTERN) [N/y/?] n

    Voor algemen vragen over het compileren van een kernel, kijk in de
    Linux-Kernel-HOWTO.
    De beste kernels voor Mars_nwe zijn 2.0.x.
    Oudere kernels werken soms niet, omdat er bugs in de ipx-code 
    zitten. Kijk in de directorie 'examples' voor patches.

(2) Maak hier een keuze, of de mars_nwe je ipx-subsysteem moet initialiseren
    en routing beheren, of dat je dit doet met de hand met andere producten.
    Voordat je een probleem mailt naar de mailinglist over problemen met
    mars_nwe, probeer dit eerst.
    
(2a) Configureer het IPX-subsysteem met "mars_nwe"

    Je hebt geen andere ipx-tool of routers/daemons nodig
    in dir geval.
    Dit is gestest zodat "mars_nwe", "dosemu", "ncpfs" of
    Caldera's "nwclient" hiermee goed samenwerken.
    
    zet INTERNAL_RIP_SAP op "1" in `mars_nwe/config.h':

    #define INTERNAL_RIP_SAP  1

    Als je andere IPX/NCP server in je lokale netwerk hebt kun je 
    de "mars_nwe" automatisch de goede waarde laten invullen voor je
    ipx-subsysteem.
    Dit houd in dat je de volgende secties in 'nwserv.conf' zo invult:
     
    3  0x0                 # Gebruik je Ipx nummer voor het interne netwerk.
    4  0x0  *      AUTO    # autodetect je interafces.
    5  0x2                 # laat de kernel automatisch interfaces voor
    			   # creeeren.
    			   
    Zorg dat er geen enkele andere server is die je interne netwerk-
    nummer  gebruikt als zijn netwerk nummer.
    
    Als er geen andere IPX/NCP server is op je netwerk, dan kan het netwerk
    nummer in sectie '4' elk nummer zijn.
    
    4  0x10 eth0 ethernet_ii  # eth0 device met het netwerk nummer '0x10'
                              # en frametype: ETHERNET_II.
    4  0x20 eth0 802.3        # eth0 device met het netwerk nummer '0x20'
                              # en frametype: ETHERNET_802.3.
    			   

(2b) Handmatige configuratie van het IPX-subsysteem

    In deze mode moet je tools zoals "ipx-configure" en
    "ipxd" gebruiken voor het configureren van ipx-interfaces,
    ipx-routes en om rip/sap aanvragen af te handelen.

    zet INTERNAL_RIP_SAP op "0" in `mars_nwe/config.h':
    
    #define INTERNAL_RIP_SAP  0


(3) Compileer de programma's van het Mars_nwe pakker

    pak de source van "mars_nwe" uit en ga naar de directorie waarin
    die is uitgepakt ('mars_nwe').
    Type hget volgende comando in:

       make

    Dit command maakt de volgende bestanden: 'config.h' en 'mk.li'.
    Edit de zoals jij ze nodig hebt. 'mk.li' moet alleen veranderdt
    worden onder hele rare omstandigheden, of als je problemen hebt 
    met het compileren en/of het linken van dit pakket.

    draai nu "make" opnieuw:

       make
    

(4) Bewerk ("edit") het configuratie bestand 'nw.ini'.
    
    Zorg dat je zeker weet dat alle benodigde secties in je oude configuratie
    bestand staan, als je upgrade naar een nieuwere versie van de "mars_nwe".
    
    
(5) Installeer alles

   Zeg alleen:
   
   	make install

   en soms:
   
       make install_ini
       
   Deze laatste is alleen nodig als je je het _bestaande_ configuratie 
   bestand (nwserv.conf) wilt overschrijven met nw.ini

(6) Creer de directories die te zien moete zijn voor DOS-clients

    Op z'n minst moet het volume "SYS" zijn gedefineerd in het configuratie 
    bestand nwserv.conf. Creer de geassocieerde directory als die al niet 
    bestaat en zet de programma's "login.exe" en "slist.exe" in de 
    "LOGIN" / "login" directorie.
    Je kunt ook de vrij verkrijgbare mars_dosutils gebruiken.
    NIEUW !
    in nieuwere versies va de MARS_NWE worden deze directories automatisch
    gecreetd bij de eerste keer dat de mars_nwe server opstart.

(7) Start de server
    Start het volgende programma als root op:
    
       nwserv
       
(8) Stop de programma's (server plat)

    Als de mars_nwe server op de voorgrond loopt kun je de server stoppen
    met een ^C (CTRL-C), anders kill de PID van nwserv,tik in 'nwserv -k'
    Of gebruik het dos programma fconsole en kies "server down" als je als 
    supervisor (of gelijke) bent aangelogd.
    Sectie 210 in het "nw.ini" / "nwserv.conf" bestand geeft de tijd in
    seconden voordat de server echt stop.
    
Veel geluk  :-)

Martin Stover <mstover@compu-art.de>
