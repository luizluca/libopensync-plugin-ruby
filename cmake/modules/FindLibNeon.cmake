# - Try and find libneon
# Once done this will define
#
#  NEON_FOUND - system has libneon
#  NEON_INCLUDE_DIR - the libneon include directory
#  NEON_LIBRARIES - Link these to use libneon
#  NEON_DEFINITIONS - Compiler switches required for using libneon

# Copyright (c) 2011, Juergen Leising, <jleising@users.sourceforge.net>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.


IF (NEON_INCLUDE_DIR AND NEON_LIBRARIES)
    SET(NEON_FIND_QUIETLY TRUE)
ENDIF (NEON_INCLUDE_DIR AND NEON_LIBRARIES)

# Useful for finding out CPPFLAGS, LDFLAGS and features:
FIND_PROGRAM( NEON_CONFIG_EXECUTABLE NAMES neon-config )


# 1. Search for the include dirs and for the library by means of cmake
FIND_PATH(NEON_INCLUDE_DIR NAMES neon/ne_session.h 
                           PATHS /include /usr/include /usr/local/include /usr/share/include /opt/include /include/neon /usr/include/neon /usr/local/include/neon /usr/share/include/neon /opt/include/neon
         )

FIND_LIBRARY(NEON_LIBRARIES NAMES neon )


# 2. Search for the include dirs and for the library by means of pkg-config,
#    if necessary and if possible
IF (NOT NEON_INCLUDE_DIR OR NOT NEON_LIBRARIES)
	FIND_PROGRAM( PKGCONFIG_EXECUTABLE NAMES pkg-config )

	IF ( PKG_CONFIG_FOUND )
  	MESSAGE (STATUS "  Trying to invoke pkg-config...")
  	PKG_CHECK_MODULES ( NEON ${_pkgconfig_REQUIRED} neon )
	  IF ( NEON_FOUND )
  	  MESSAGE (STATUS "    pkg-config found libneon.")
			SET (NEON_INCLUDE_DIR ${NEON_INCLUDEDIR} )
	  ELSE ( NEON_FOUND ) 
  	  MESSAGE (STATUS "    pkg-config did NOT find libneon.")
	  ENDIF ( NEON_FOUND )
	ENDIF ( PKG_CONFIG_FOUND )

ENDIF (NOT NEON_INCLUDE_DIR OR NOT NEON_LIBRARIES)


# 3. Search for the include dirs and for the library by means of neon-config,
#    if necessary and if possible
IF (NOT NEON_INCLUDE_DIR OR NOT NEON_LIBRARIES)
	
	IF ( NEON_CONFIG_EXECUTABLE )
		execute_process( COMMAND ${NEON_CONFIG_EXECUTABLE} "--libs"
		                 OUTPUT_VARIABLE NEON_LIBRARIES )
		execute_process( COMMAND ${NEON_CONFIG_EXECUTABLE} "--cflags"
		                 OUTPUT_VARIABLE NEON_DEFINITIONS )	
		execute_process( COMMAND ${NEON_CONFIG_EXECUTABLE} "--cflags"
		                 OUTPUT_VARIABLE NEON_INCLUDE_DIR )	
		MESSAGE(STATUS "NEON_INCLUDE_DIR = \"${NEON_INCLUDE_DIR}\"")
		MESSAGE(STATUS "NEON_LIBRARIES = \"${NEON_LIBRARIES}\"")
		MESSAGE(STATUS "NEON_DEFINITIONS = \"${NEON_DEFINITIONS}\"")
	
	ELSE ( NEON_CONFIG_EXECUTABLE )
		MESSAGE(STATUS "neon-config could NOT be found, either.")

	ENDIF ( NEON_CONFIG_EXECUTABLE )
	
ENDIF (NOT NEON_INCLUDE_DIR OR NOT NEON_LIBRARIES)



# handle the QUIETLY and REQUIRED arguments and set NEON_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NEON DEFAULT_MSG NEON_LIBRARIES NEON_INCLUDE_DIR)

IF (NEON_FOUND)
	INCLUDE(CheckLibraryExists)
	CHECK_LIBRARY_EXISTS(${NEON_LIBRARIES} ne_session_create "" NEON_NEED_PREFIX)

	IF ( NEON_CONFIG_EXECUTABLE )
	# neon-config --support TS_SSL
		execute_process( COMMAND ${NEON_CONFIG_EXECUTABLE} "--support" "ts_ssl"
		                 RESULT_VARIABLE TS_SSL )

		IF (TS_SSL EQUAL 0)
			MESSAGE(STATUS "libneon has been compiled with the --enable-threadsafe-ssl flag. You can use SSL/TLS in a multi-threaded application.")
			SET( THREAD_SAFE_SSL 1 )
		ELSE (TS_SSL EQUAL 0) 	
			MESSAGE(STATUS "libneon has NOT been compiled with the --enable-threadsafe-ssl flag. You should NOT use SSL/TLS in a multithreaded application.")
			SET( THREAD_SAFE_SSL 0 )
		ENDIF (TS_SSL EQUAL 0)

	ENDIF ( NEON_CONFIG_EXECUTABLE )
	
ENDIF (NEON_FOUND)

MARK_AS_ADVANCED(NEON_INCLUDE_DIR NEON_LIBRARIES)

