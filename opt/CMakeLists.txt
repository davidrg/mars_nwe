#################################
# Project
##############

#################################
# Dependencies
##############

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/nw.ini.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nwserv.conf"
    IMMEDIATE @ONLY)

#################################
# Compiler Switches
##############

INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

if (CMAKE_SYSTEM_NAME MATCHES Linux)
    add_definitions(
        -DLINUX -D_GNU_SOURCE -Dsignal=__sysv_signal
    )
endif (CMAKE_SYSTEM_NAME MATCHES Linux)

add_definitions(
        -D_VERS_H_=\"${VERSION_MAJOR}\"
        -D_VERS_L_=\"${VERSION_MINOR}\"
        -D_VERS_P_=\"${VERSION_PATCH}\"
    )

#################################
# Source Files
##############

add_executable( comm comm.c )
add_executable( sendm sendm.c )
add_executable( unxcomm unxcomm.c )
add_executable( unxsendm unxsendm.c )
# add_executable( xsockrt xsockrt.c )

#################################
# Linking
##############

#################################
# Install Files
##############

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nwserv.conf DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR})
install(FILES nwserv.stations DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR})
install(FILES nw.ini.cnv.437 DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.437)
install(FILES nw.ini.cnv.cyr DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.cyr)
install(FILES nw.ini.cnv.min DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.min)
install(FILES nw.ini.cnv.pl.852-iso2 DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.pl.852-iso2)
install(FILES nw.ini.cnv.pl.852-none DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.pl.852-none)
install(FILES nw.ini.cnv.pl.cp1250-iso2 DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.pl.cp1250-iso2)
install(FILES nw.ini.cnv.pl.mazovia-iso2 DESTINATION ${MARS_NWE_INSTALL_FULL_CONFDIR} RENAME nwserv.cnv.pl.mazovia-iso2)

install(FILES nwopt DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS RENAME .nwopt.vol)
install(FILES nwopt DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/skel RENAME .nwopt)

install(TARGETS comm DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
install(TARGETS sendm DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
install(TARGETS unxcomm DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
install(TARGETS unxsendm DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
# install(TARGETS xsockrt DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})

install(FILES prn DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
install(FILES prn-clr DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})
install(FILES uptime DESTINATION ${MARS_NWE_INSTALL_FULL_LIBEXECDIR})


