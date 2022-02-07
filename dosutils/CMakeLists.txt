#################################
# Project
##############

#################################
# Dependencies
##############

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

#add_executable( net logon.c ... )

#################################
# Linking
##############

#################################
# Install Files
##############

# install the system utils
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME login.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME profile.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME spawn.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME passwd.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME path.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME pathins.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME pathdel.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME map.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME mapdel.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME logout.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME slist.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME capture.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public RENAME endcap.exe)

# and the minimal login utils
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/login RENAME login.exe)
install(FILES net.exe DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/login RENAME map.exe)


