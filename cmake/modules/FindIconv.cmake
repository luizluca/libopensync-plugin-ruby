
# This original file is from eiskaltdc at http://code.google.com/p/eiskaltdc/.
# The file itself does not contain a copyright notice
# but the whole project is released under GPL v3 (or any later version).
# Timestamp: 20101-Jan-05
#
#  Copyright (c) 2010 Michael Bell <michael.bell@web.de>

if (ICONV_INCLUDE_DIR AND ICONV_LIBRARIES)
  # Already in cache, be silent
  set(ICONV_FIND_QUIETLY TRUE)
endif (ICONV_INCLUDE_DIR AND ICONV_LIBRARIES)

find_path(ICONV_INCLUDE_DIR iconv.h)

find_library(ICONV_LIBRARIES NAMES iconv libiconv libiconv-2 c)

if (ICONV_INCLUDE_DIR AND ICONV_LIBRARIES)
   set (ICONV_FOUND TRUE)
endif (ICONV_INCLUDE_DIR AND ICONV_LIBRARIES)

set(CMAKE_REQUIRED_INCLUDES ${ICONV_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${ICONV_LIBRARIES})

if (ICONV_FOUND)
  include(CheckCSourceCompiles)
  CHECK_C_SOURCE_COMPILES("
  #include <iconv.h>
  int main(){
    iconv_t conv = 0;
    const char* in = 0;
    size_t ilen = 0;
    char* out = 0;
    size_t olen = 0;
    iconv(conv, &in, &ilen, &out, &olen);
    return 0;
  }
" ICONV_SECOND_ARGUMENT_IS_CONST )
endif (ICONV_FOUND)

set (CMAKE_REQUIRED_INCLUDES)
set (CMAKE_REQUIRED_LIBRARIES)

if (ICONV_FOUND)
  if (NOT ICONV_FIND_QUIETLY)
    message (STATUS "Found Iconv: ${ICONV_LIBRARIES}")
  endif (NOT ICONV_FIND_QUIETLY)
else (ICONV_FOUND)
  if (Iconv_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find Iconv")
  endif (Iconv_FIND_REQUIRED)
endif (ICONV_FOUND)

MARK_AS_ADVANCED(
  ICONV_INCLUDE_DIR
  ICONV_LIBRARIES
  ICONV_SECOND_ARGUMENT_IS_CONST
)
