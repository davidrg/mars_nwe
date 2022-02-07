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

install(FILES marsmail.exe pmail.bat pmgate.sys ohlogscr.bat pmdflts.ini DESTINATION ${MARS_NWE_INSTALL_FULL_FILEDIR}/SYS/public)

