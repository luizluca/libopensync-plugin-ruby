# Try and find libldap.
# As soon as libldap has been found, the following variables will be defined:
#
# LIBLDAP_FOUND
# LDAP_INCLUDE_DIR
# LDAP_LIBRARY:FILEPATH
#
#
# Copyright (c) 2009 Juergen Leising <jleising@users.sourceforge.net>
#
# Redistribution and use is allowed according to the terms of the New
# BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


MESSAGE(STATUS "checking for libldap...")

FIND_PATH(LDAP_INCLUDE_DIR NAMES ldap.h 
                           PATHS /include /usr/include /usr/local/include /usr/share/include /opt/include 
                           DOC "Try and find the header file ldap.h")

FIND_LIBRARY(LDAP_LIBRARY NAMES ldap
                          PATHS /usr/lib /lib /usr/local/lib /usr/share/lib /opt/lib /opt/share/lib /var/lib /usr/lib64 /lib64 /usr/local/lib64 /usr/share/lib64 /opt/lib64 /opt/share/lib64 /var/lib64
                          DOC "Try and find libldap")

IF (LDAP_INCLUDE_DIR AND LDAP_LIBRARY)
	SET( LIBLDAP_FOUND 1 )
  get_filename_component(LDAP_LIBRARY_DIRS ${LDAP_LIBRARY} PATH)
  MESSAGE(STATUS "  Found ${LDAP_LIBRARY}")
ELSE (LDAP_INCLUDE_DIR AND LDAP_LIBRARY)
	IF ( LibLdap_FIND_REQUIRED )
	  MESSAGE( FATAL_ERROR "  Could NOT find libldap.  The ldap plugin needs this library.")
	ELSE ( LibLdap_FIND_REQUIRED )
		MESSAGE( STATUS "   Could NOT find libldap.")
	ENDIF ( LibLdap_FIND_REQUIRED )
ENDIF (LDAP_INCLUDE_DIR AND LDAP_LIBRARY)

