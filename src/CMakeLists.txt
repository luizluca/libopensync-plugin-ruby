LINK_DIRECTORIES( ${OPENSYNC_LIBRARY_DIRS} ${GLIB2_LIBRARY_DIRS} ${LIBXML2_LIBRARY_DIRS} ${RUBY_LIBRARY})
INCLUDE_DIRECTORIES( ${OPENSYNC_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS} ${LIBXML2_INCLUDE_DIRS} ${RUBY_INCLUDE_DIRS})

INCLUDE( ${SWIG_USE_FILE} )
SET(CMAKE_SWIG_FLAGS -Werror -Wall)

cmake_policy(VERSION 2.6)
ADD_DEFINITIONS( -DOPENSYNC_RUBYPLUGIN_DIR="${OPENSYNC_PLUGINDIR}" )
ADD_DEFINITIONS( -DOPENSYNC_RUBYFORMATS_DIR="${OPENSYNC_FORMATSDIR}" )
ADD_DEFINITIONS( -DOPENSYNC_RUBYLIB_DIR="${LIB_INSTALL_DIR}" )

IF (WIN32)
        # Execute Win32 Specific commands - none yet.
ELSE (WIN32)
        # Execute commands for other platforms
        ADD_CUSTOM_COMMAND(
                OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/callbacks.h
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gencallbacks.rb > ${CMAKE_CURRENT_BINARY_DIR}/callbacks.h
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Generate callback code"
		MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/gencallbacks.rb
                )
ENDIF (WIN32)

# TODO: How to make swig not build opensync.so.so???
SWIG_ADD_MODULE( opensync-swig ruby opensync.i )
# HACK: Rename swig module in order to ignore it as a plugin/format module
SET_TARGET_PROPERTIES(${SWIG_MODULE_opensync-swig_REAL_NAME} PROPERTIES SUFFIX ".tmp")

# Include SWIG in include in order to compile it with ruby_module
INCLUDE_DIRECTORIES( ${swig_outdir} )

OPENSYNC_PLUGIN_ADD( ruby-module ruby_module.c opensync.i ${CMAKE_CURRENT_BINARY_DIR}/callbacks.h)

TARGET_LINK_LIBRARIES( ruby-module ${OPENSYNC_LIBRARIES} ${GLIB2_LIBRARIES} ${LIBXML2_LIBRARIES} ${RUBY_LIBRARY})

###### INSTALL ###################
OPENSYNC_PLUGIN_INSTALL( ruby-module )
# BUG install as symblink only
OPENSYNC_FORMAT_INSTALL( ruby-module )
OPENSYNC_PLUGIN_CONFIG( ruby-module )

#this will install in RUBYLIB
#INSTALL( FILES opensync.rb DESTINATION ${RUBY_RUBY_LIB_DIR} )
INSTALL( FILES opensync.rb DESTINATION ${LIB_INSTALL_DIR}/ruby/ )

#INSTALL( FILES ../ruby-file-sync.rb DESTINATION ${OPENSYNC_RUBYPLUGIN_DIR} )
#INSTALL( FILES ${CMAKE_CURRENT_BINARY_DIR}/opensync-swig.tmp DESTINATION ${RUBY_ARCH_DIR} RENAME opensync${CMAKE_SHARED_MODULE_SUFFIX} )
