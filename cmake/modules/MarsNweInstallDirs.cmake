# - Define mars_nwe standard installation directories
# Inclusion of this module defines the following variables:
#  MARS_NWE_INSTALL_<dir>      - destination for files of a given type
#  MARS_NWE_INSTALL_FULL_<dir> - corresponding absolute path
# where <dir> is one of:
#  WEBDIR           - user executables (bin)
#  WEBBINDIR           - user executables (bin)
#  MODULESDIR          - system admin executables (sbin)
#  WEBACCESSDIR       - read-only single-machine data (etc)
#  ADMINDIR    - modifiable single-machine data (var)
#  CONFDIR           - object code libraries (lib or lib64)
#  BOOTSTRAPDIR    - C header files for non-gcc (/usr/include)
# Each MARS_NWE_INSTALL_<dir> value may be passed to the DESTINATION options of
# install() commands for the corresponding file type.  If the includer does
# not define a value the above-shown default will be used and the value will
# appear in the cache for editing by the user.
# Each MARS_NWE_INSTALL_FULL_<dir> value contains an absolute path constructed
# from the corresponding destination by prepending (if necessary) the value
# of MARS_NWE_INSTALL_PREFIX.

#=============================================================================
# Copyright 2011 Nikita Krupen'ko <krnekit@gmail.com>
# Copyright 2011 Kitware, Inc.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

# Values whose defaults are relative to WEBDIR.  Store empty values in
# the cache and store the defaults in local variables if the cache values are
# not set explicitly.  This auto-updates the defaults as WEBDIRDIR changes.

if(NOT MARS_NWE_INSTALL_LIBEXECDIR)
  set(MARS_NWE_INSTALL_LIBEXECDIR "" CACHE PATH "Shared libs (LIBDIR/mars_nwe)")
  set(MARS_NWE_INSTALL_LIBEXECDIR "${CMAKE_INSTALL_LIBDIR}/mars_nwe")
endif()

if(NOT MARS_NWE_INSTALL_CONFDIR)
  set(MARS_NWE_INSTALL_CONFDIR "" CACHE PATH "Mars Nwe config (SYSCONFDIR/mars_nwe)")
  set(MARS_NWE_INSTALL_CONFDIR "${CMAKE_INSTALL_SYSCONFDIR}/mars_nwe")
endif()

if(NOT MARS_NWE_INSTALL_FILEDIR)
  set(MARS_NWE_INSTALL_FILEDIR "" CACHE PATH "Mars Nwe file (LOCALSTATEDIR/mars_nwe)")
  set(MARS_NWE_INSTALL_FILEDIR "/${CMAKE_INSTALL_LOCALSTATEDIR}/mars_nwe")
endif()

if(NOT MARS_NWE_INSTALL_SPOOLDIR)
  set(MARS_NWE_INSTALL_SPOOLDIR "" CACHE PATH "Mars Nwe spool (LOCALSTATEDIR/spool/mars_nwe)")
  set(MARS_NWE_INSTALL_SPOOLDIR "/${CMAKE_INSTALL_LOCALSTATEDIR}/spool/mars_nwe")
endif()

if(NOT MARS_NWE_DATA_DIR)
  set(MARS_NWE_DATA_DIR "" CACHE PATH "Mars Nwe data (LOCALSTATEDIR/lib/mars_nwe)")
  set(MARS_NWE_DATA_DIR "/${CMAKE_INSTALL_LOCALSTATEDIR}/lib/mars_nwe")
endif()

if(NOT MARS_NWE_LOG_DIR)
  set(MARS_NWE_LOG_DIR "" CACHE PATH "Mars Nwe log (LOCALSTATEDIR/log/mars_nwe)")
  set(MARS_NWE_LOG_DIR "/${CMAKE_INSTALL_LOCALSTATEDIR}/log/mars_nwe")
endif()

if(NOT MARS_NWE_PID_DIR)
  set(MARS_NWE_PID_DIR "" CACHE PATH "Mars Nwe pid (LOCALSTATEDIR/run/mars_nwe)")
  set(MARS_NWE_PID_DIR "/${CMAKE_INSTALL_LOCALSTATEDIR}/run/mars_nwe")
endif()


#-----------------------------------------------------------------------------

mark_as_advanced(
  MARS_NWE_INSTALL_LIBEXEC
  MARS_NWE_INSTALL_CONFDIR
  MARS_NWE_INSTALL_FILEDIR
  MARS_NWE_INSTALL_SPOOLDIR
  MARS_NWE_DATA_DIR
  MARS_NWE_LOG_DIR
  MARS_NWE_PID_DIR
  )

# Result directories
#
foreach(dir
  LIBEXECDIR
  CONFDIR
  FILEDIR
  SPOOLDIR
    )
  if(NOT IS_ABSOLUTE ${MARS_NWE_INSTALL_${dir}})
    set(MARS_NWE_INSTALL_FULL_${dir} "${CMAKE_INSTALL_PREFIX}/${MARS_NWE_INSTALL_${dir}}")
  else()
    set(MARS_NWE_INSTALL_FULL_${dir} "${MARS_NWE_INSTALL_${dir}}")
  endif()
endforeach()
