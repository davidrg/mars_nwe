project(mars_nwe)
cmake_minimum_required(VERSION 2.6.0)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

enable_language(C)

SET (VERSION_MAJOR "0")
SET (VERSION_MINOR "99")
SET (VERSION_PATCH "22")
SET (VERSION_SUB "0")
SET (VERSION_DEVEL "0")
SET (VERSION_SUFFIX ".0")


IF (VERSION_SUB)
    SET (VERSION_PATCH "${VERSION_PATCH}${VERSION_SUFFIX}")
ENDIF (VERSION_SUB)

IF (VERSION_DEVEL)
    # for snapshot Version Number
    find_package(Subversion)
    IF (Subversion_FOUND)
	Subversion_WC_INFO(${PROJECT_SOURCE_DIR} CASA)
	MESSAGE("Current revision is ${CASA_WC_REVISION}")
    ENDIF (Subversion_FOUND)
    SET (VERSION_PATCH "${VERSION_PATCH}.${CASA_WC_REVISION})${VERSION_SUFFIX}")
ENDIF (VERSION_DEVEL)

SET (MARS_NWE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.pl${VERSION_PATCH}")

INCLUDE(${CMAKE_ROOT}/Modules/GNUInstallDirs.cmake)
INCLUDE(${CMAKE_MODULE_PATH}/MarsNweInstallDirs.cmake)

# Add options for build
option(ENABLE_DEBUG "Should we build with Debug?" ON)
option(ENABLE_DEBUG_DOSUTILS "Should we build with Mars Nwe Dosutils Debugging?" OFF)
option(ENABLE_INTERNAL_RIP_SAP "Should we build Mars Nwe with Internal Router?" ON)
option(ENABLE_SHADOW_PWD "Should we build Mars Nwe with Shadow Password Support?" ON)
option(ENABLE_QUOTA_SUPPORT "Should we build Mars Nwe with Quota Support?" OFF)

IF (ENABLE_DEBUG)
    SET (MARS_NWE_DEBUG "1")
ELSE (ENABLE_DEBUG)
    SET (MARS_NWE_DEBUG "0")
ENDIF (ENABLE_DEBUG)

IF (ENABLE_DEBUG_DOSUTILS)
    SET (MARS_NWE_DEBUG_DOSUTILS "1")
ELSE (ENABLE_DEBUG_DOSUTILS)
    SET (MARS_NWE_DEBUG_DOSUTILS "0")
ENDIF (ENABLE_DEBUG_DOSUTILS)

IF (ENABLE_INTERNAL_RIP_SAP)
    SET (MARS_NWE_INTERNAL_RIP_SAP "1")
ELSE (ENABLE_INTERNAL_RIP_SAP)
    SET (MARS_NWE_INTERNAL_RIP_SAP "0")
ENDIF (ENABLE_INTERNAL_RIP_SAP)

IF (ENABLE_SHADOW_PWD)
    SET (MARS_NWE_SHADOW_PWD "1")
ELSE (ENABLE_SHADOW_PWD)
    SET (MARS_NWE_SHADOW_PWD "0")
ENDIF (ENABLE_SHADOW_PWD)

IF (ENABLE_QUOTA_SUPPORT)
    SET (MARS_NWE_QUOTA_SUPPORT "1")
ELSE (ENABLE_QUOTA_SUPPORT)
    SET (MARS_NWE_QUOTA_SUPPORT "0")
ENDIF (ENABLE_QUOTA_SUPPORT)

IF (NOT MAX_CONNECTIONS)
    SET (MAX_CONNECTIONS "50")
ENDIF (NOT MAX_CONNECTIONS)

IF (NOT MAX_VOLS)
    SET (MAX_VOLS "50")
ENDIF (NOT MAX_VOLS)

IF (NOT MAX_FILES)
    SET (MAX_FILES "256")
ENDIF (NOT MAX_FILES)



find_package( Crypt REQUIRED)
find_package( GDBM REQUIRED)



message(STATUS "Mars Nwe version: ${MARS_NWE_VERSION}")
message(STATUS "bin: ${CMAKE_INSTALL_FULL_BINDIR}")
message(STATUS "sbin: ${CMAKE_INSTALL_FULL_SBINDIR}")
message(STATUS "lib: ${CMAKE_INSTALL_FULL_LIBDIR}")
message(STATUS "include: ${CMAKE_INSTALL_FULL_INCLUDEDIR}")
message(STATUS "libexec: ${CMAKE_INSTALL_FULL_LIBEXECDIR}")
message(STATUS "doc: ${CMAKE_INSTALL_FULL_DOCDIR}")
message(STATUS "man: ${CMAKE_INSTALL_FULL_MANDIR}")
message(STATUS "sysconf: ${CMAKE_INSTALL_FULL_SYSCONFDIR}")
message(STATUS "Mars Nwe libexec: ${MARS_NWE_INSTALL_FULL_LIBEXEC}")
message(STATUS "Mars Nwe config: ${MARS_NWE_INSTALL_FULL_CONFDIR}")
message(STATUS "Mars Nwe data: ${MARS_NWE_DATA_DIR}")
message(STATUS "Mars Nwe log: ${MARS_NWE_LOG_DIR}")
message(STATUS "Mars Nwe pid: ${MARS_NWE_PID_DIR}")



# put the include dirs which are in the source or build tree
# before all other include dirs, so the headers in the sources
# are prefered over the already installed ones
# since cmake 2.4.1
set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)

add_custom_target(uninstall
    "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")

add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)


add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(opt)
add_subdirectory(sys)
add_subdirectory(dosutils)
add_subdirectory(mail)

SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MARtin Stovers NetWare-Emulator.")
SET(CPACK_PACKAGE_VENDOR "http://www.compu-art.de/mars_nwe/")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "pl${VERSION_PATCH}")
set(CPACK_SOURCE_GENERATOR "TBZ2")
SET(CPACK_PACKAGE_INSTALL_DIRECTORY "mars_nwe-${VERSION}")
SET(CPACK_SOURCE_IGNORE_FILES CMakeCache.txt CMakeFiles progress.make cmake_install.cmake CPackConfig.cmake CPackSourceConfig.cmake "\\\\.git" "\\\\.svn" "\\\\.swp$" "\\\\.cvs" "\\\\.tar.gz" "\\\\.o")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
include(CPack)
