# FindANTLR4.cmake
# Locate the antlr4-runtime library

set(_ANTLR4_HINT_INCLUDE_DIRS
    /usr/local/include
    /usr/include
)

set(_ANTLR4_HINT_LIBRARY_DIRS
    /usr/local/lib
    /usr/lib
    /usr/lib/x86_64-linux-gnu
)

find_path(ANTLR4_INCLUDE_DIR antlr4-runtime.h
    HINTS ${_ANTLR4_HINT_INCLUDE_DIRS}
    PATH_SUFFIXES antlr4-runtime
)

find_library(ANTLR4_LIBRARY NAMES antlr4-runtime
    HINTS ${_ANTLR4_HINT_LIBRARY_DIRS}
    PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ANTLR4 DEFAULT_MSG
    ANTLR4_INCLUDE_DIR ANTLR4_LIBRARY
)

mark_as_advanced(ANTLR4_INCLUDE_DIR ANTLR4_LIBRARY)

unset(_ANTLR4_HINT_INCLUDE_DIRS)
unset(_ANTLR4_HINT_LIBRARY_DIRS)
