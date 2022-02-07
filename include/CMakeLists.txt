#################################
# Project
##############

#################################
# Dependencies
##############

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    IMMEDIATE @ONLY)


#################################
# Compiler Switches
##############

INCLUDE_DIRECTORIES(
)

link_directories(
)

add_definitions(
)

#################################
# Source Files
##############


#SET (include_src
#    cbmcopy.h
#    d64copy.h
#    opencbm.h
#    )

#################################
# Linking
##############

#################################
# Install Files
##############

#install(FILES ${include_src} DESTINATION ${INCLUDE_INSTALL_DIR})