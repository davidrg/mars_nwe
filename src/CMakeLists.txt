#################################
# Project
##############

#project(mars-nwe)

#################################
# Dependencies
##############

if (CMAKE_SYSTEM_NAME MATCHES Linux)
    add_definitions(
        -pipe -Wall -DLINUX -D_GNU_SOURCE
    )
endif (CMAKE_SYSTEM_NAME MATCHES Linux)

add_definitions(
        -D_VERS_H_=${VERSION_MAJOR}
        -D_VERS_L_=${VERSION_MINOR}
        -D_VERS_P_=${VERSION_PATCH}
    )

IF(ENABLE_QUOTA_SUPPORT)
    add_definitions(
	-DQTAINSYS
    )
ENDIF(ENABLE_QUOTA_SUPPORT)

IF(GDBM_FOUND)
    add_definitions(
	-DUSE_GDBM
    )
ENDIF(GDBM_FOUND)

#################################
# Compiler Switches
##############

INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

#################################
# Source Files
##############

IF(ENABLE_INTERNAL_RIP_SAP)
    SET(EMUTLI emutli.c)
    SET(EMUTLI1 emutli1.c)
    SET(NWROUTE_0 nwroute.c)
ELSE(ENABLE_INTERNAL_RIP_SAP)
    SET(EMUTLI emutli.c)
    SET(EMUTLI1 empty.c)
    SET(NWROUTE_0 nwroute1.c)
ENDIF(ENABLE_INTERNAL_RIP_SAP)

add_executable(nwserv nwserv.c net1.c tools.c ${EMUTLI} ${EMUTLI1} ${NWROUTE_0} )
add_executable(nwconn nwconn.c net1.c tools.c connect.c namspace.c nwvolume.c nwfile.c unxfile.c nwqconn.c nameos2.c nwfname.c nwshare.c extpipe.c nwattrib.c trustee.c ${EMUTLI} )
add_executable(ncpserv ncpserv.c net1.c tools.c ${EMUTLI} )
add_executable(nwclient nwclient.c net1.c tools.c ${EMUTLI} )
add_executable(nwbind nwbind.c net1.c tools.c nwdbm.c nwcrypt.c unxlog.c sema.c nwqueue.c unxfile.c ${EMUTLI} )
IF(ENABLE_INTERNAL_RIP_SAP)
add_executable(nwrouted nwrouted.c net1.c tools.c ${EMUTLI} ${EMUTLI1} )
ENDIF(ENABLE_INTERNAL_RIP_SAP)
add_executable(dbmtool dbmtool.c net1.c tools.c nwdbm.c nwcrypt.c unxlog.c sema.c nwqueue.c unxfile.c ${EMUTLI} )
add_executable(ftrustee ftrustee.c tools.c nwfname.c unxfile.c nwvolume.c nwattrib.c trustee.c )


#################################
# Linking
##############

target_link_libraries(nwserv ${CRYPT_LIBRARIES} )
target_link_libraries(nwconn ${CRYPT_LIBRARIES} )
target_link_libraries(ncpserv ${CRYPT_LIBRARIES} )
target_link_libraries(nwclient ${CRYPT_LIBRARIES} )
target_link_libraries(nwbind ${CRYPT_LIBRARIES} ${GDBM_LIBRARIES} )
IF(ENABLE_INTERNAL_RIP_SAP)
target_link_libraries(nwrouted ${CRYPT_LIBRARIES} )
ENDIF(ENABLE_INTERNAL_RIP_SAP)
target_link_libraries(dbmtool ${CRYPT_LIBRARIES} ${GDBM_LIBRARIES} )
target_link_libraries(ftrustee ${CRYPT_LIBRARIES} )

#################################
# Install Files
##############

install(TARGETS nwserv DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS nwconn DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS ncpserv DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS nwclient DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS nwbind DESTINATION ${CMAKE_INSTALL_SBINDIR})
IF(ENABLE_INTERNAL_RIP_SAP)
install(TARGETS nwrouted DESTINATION ${CMAKE_INSTALL_SBINDIR})
ENDIF(ENABLE_INTERNAL_RIP_SAP)
install(TARGETS dbmtool DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS ftrustee DESTINATION ${CMAKE_INSTALL_SBINDIR})

