ADD_TEST( check_data ${CMAKE_CURRENT_SOURCE_DIR}/check_data ${CMAKE_BINARY_DIR} )
#set_property(TEST check_data PROPERTY
#    ENVIRONMENT "LD_LIBRARY_PATH=${LIB_INSTALL_DIR}"
#    ENVIRONMENT "PATH=${BIN_INSTALL_DIR}:$ENV{PATH}"
#)
