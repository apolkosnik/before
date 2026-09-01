cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

# Set build type to "Release" if user did not specify any build type yet
# Other possible values: Debug, Release, RelWithDebInfo and MinSizeRel
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Target minimum macOS version")
endif(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")

project(Previous)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(DistClean)
include(CheckTypeSize)

# set(CMAKE_VERBOSE_MAKEFILE 1)

find_package(PkgConfig)


######################################################################
# Check if Large File Support is available
# We test if the size of the type 'off_t' is at least 8 bytes
######################################################################

function (check_large_file)
	set (CMAKE_REQUIRED_FLAGS ${HOST_LFS_FLAGS} )
	check_type_size("off_t" SIZEOF_OFF_T )

	if ( SIZEOF_OFF_T GREATER 7 )
		set ( HAVE_HOST_LFS 1 CACHE INTERNAL "Large file Support" )
	else()
		set ( HAVE_HOST_LFS 0 CACHE INTERNAL "Large file Support" )
	endif()
endfunction (check_large_file)



# ##########################
# Conditional build features
# ##########################

set(ENABLE_DSP_EMU 1
    CACHE BOOL "Enable DSP 56k emulator")
set(ENABLE_TRACING 1
    CACHE BOOL "Enable tracing messages for debugging")

# Configure Previous with AddressSanitizer:
#   cd build; make clean; cmake -D ENABLE_ASAN:BOOL=1 -D ..
# If everything's fine, CMake output should include:
#  - ASAN : Compiler Address Sanitizer enabled
#
# After (re-)building Previous, run it as following to see
# available AddressSanitizer options (and that it works):
#   ASAN_OPTIONS=help=true src/previous
#
# For more info:
#   https://github.com/google/sanitizers/wiki/AddressSanitizer
#   https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
#
CHECK_C_COMPILER_FLAG("-fsanitize=address" ASAN_AVAILABLE)
if(ASAN_AVAILABLE)
	set(ENABLE_ASAN 0
	    CACHE BOOL "Enable GCC/LLVM run-time address debugging (~2x slowdown)")
endif(ASAN_AVAILABLE)

CHECK_C_COMPILER_FLAG("-fsanitize=undefined" UBSAN_AVAILABLE)
if(UBSAN_AVAILABLE)
	set(ENABLE_UBSAN 0
	    CACHE BOOL "Enable GCC/LLVM run-time undefined behavior debugging")
endif(UBSAN_AVAILABLE)

CHECK_C_COMPILER_FLAG("-fstack-protector-all -fsanitize=bounds-strict" SSP_AVAILABLE)
if(SSP_AVAILABLE)
	set(ENABLE_SSP 0
	    CACHE BOOL "Enable GCC/LLVM run-time stack protection")
endif(SSP_AVAILABLE)

CHECK_C_COMPILER_FLAG("-fhardened" HARDENED_AVAILABLE)
if(HARDENED_AVAILABLE)
	set(ENABLE_HARDENED 0
	    CACHE BOOL "Enable GCC/LLVM run-time hardening options (for production / distros not enabling them by default)")
endif(HARDENED_AVAILABLE)

if(APPLE)
	set(ENABLE_RENDERING_THREAD 1
	    CACHE BOOL "Enable rendering threads")
	set(ENABLE_OSX_BUNDLE 1
	    CACHE BOOL "Built Previous as macOS application bundle")
else()
	set(ENABLE_RENDERING_THREAD 0
	    CACHE BOOL "Enable rendering threads")
	set(ENABLE_OSX_BUNDLE 0
	    CACHE BOOL "Built Previous as macOS application bundle")
endif(APPLE)

if(ENABLE_OSX_BUNDLE)
	set(APP_NAME "Previous")
else()
	set(APP_NAME "previous")
endif(ENABLE_OSX_BUNDLE)

# ####################
# Check for libraries:
# ####################

find_package(SDL3 REQUIRED COMPONENTS SDL3)
if(NOT SDL3_FOUND)
	message(FATAL_ERROR "SDL3 library not found!")
endif(NOT SDL3_FOUND)

find_package(Math)

find_package(Readline)
if(Readline_FOUND)
	set(HAVE_LIBREADLINE 1)
endif(Readline_FOUND)

find_package(PNG)
if(PNG_FOUND)
	set(HAVE_LIBPNG 1)
endif(PNG_FOUND)

find_package(PCAP)
if(PCAP_FOUND)
	set(HAVE_PCAP 1)
endif(PCAP_FOUND)

# Check if Large File Support is available with the standard POSIX flags
set (HOST_LFS_FLAGS "-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64")
check_large_file()


# ################
# CPP Definitions:
# ################

# We still want to have assert() in RelWithDebInfo builds:
string(REPLACE " -DNDEBUG" ""
       CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

# Test for large file support:
execute_process(COMMAND getconf LFS_CFLAGS
                OUTPUT_VARIABLE DETECTED_LFS_CFLAGS
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DETECTED_LFS_CFLAGS)
	add_definitions(${DETECTED_LFS_CFLAGS})
	# message(STATUS "Large filesystem flags: ${DETECTED_LFS_CFLAGS}")
endif(DETECTED_LFS_CFLAGS)

# ###########################
# Check for optional headers:
# ###########################

check_include_files(byteswap.h HAVE_BYTESWAP_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(sys/time.h HAVE_SYS_TIME_H)
check_include_files(sys/xattr.h HAVE_SYS_XATTR_H)
check_include_files(arpa/inet.h HAVE_ARPA_INET_H)
check_include_files(netinet/in.h HAVE_NETINET_IN_H)

# #############################
# Check for optional functions:
# #############################

check_symbol_exists(bswap_16 "byteswap.h" HAVE_BSWAP_16)
check_symbol_exists(bswap_32 "byteswap.h" HAVE_BSWAP_32)
check_symbol_exists(setenv "stdlib.h" HAVE_SETENV)
check_symbol_exists(select "sys/select.h" HAVE_SELECT)
check_symbol_exists(gettimeofday "sys/time.h" HAVE_GETTIMEOFDAY)
check_symbol_exists(nanosleep "time.h" HAVE_NANOSLEEP)
check_symbol_exists(alphasort "dirent.h" HAVE_ALPHASORT)
check_symbol_exists(scandir "dirent.h" HAVE_SCANDIR)
check_symbol_exists(fseeko "stdio.h" HAVE_FSEEKO)
check_symbol_exists(ftello "stdio.h" HAVE_FTELLO)
check_symbol_exists(flock "sys/file.h" HAVE_FLOCK)
check_symbol_exists(strdup "string.h" HAVE_STRDUP)
check_symbol_exists(lsetxattr "sys/xattr.h" HAVE_LXETXATTR)
check_symbol_exists(posix_memalign "stdlib.h" HAVE_POSIX_MEMALIGN)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
check_symbol_exists(_aligned_malloc "stdlib.h" HAVE__ALIGNED_MALLOC)

# ###########################################
# Check for platform specific struct members:
# ###########################################
message("-- The following HAVE_STRUCT_* tests are checking for non-POSIX systems such as macOS; 'Failed' does not indicate a problem")
check_struct_has_member("struct stat" st_atimespec.tv_nsec
    "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_ATIMESPEC)
check_struct_has_member("struct stat" st_mtimespec.tv_nsec
    "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC)
check_struct_has_member("struct dirent" d_type dirent.h HAVE_DIRENT_D_TYPE)

# #############
# Other CFLAGS:
# #############

# GCC/Clang address pointer access debugging (~2x slowdown)
if(ENABLE_ASAN)
	# enable AddressSanitizer with global variable tracking
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-common")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif(ENABLE_ASAN)

# GCC/Clang undefined behavior debugging
if(ENABLE_UBSAN)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
	add_definitions(-DENABLE_UBSAN=1)
endif(ENABLE_UBSAN)

# GCC/Clang stack protection options (overlaps with HARDENED)
if(ENABLE_SSP)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all -fsanitize=bounds-strict")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all -fsanitize=bounds-strict")
endif(ENABLE_SSP)

# GCC/Clang production hardening option (overlaps with SSP)
if(ENABLE_HARDENED)
	if(ENABLE_SSP)
		message(FATAL_ERROR "Enabled either HARDENED or SSP, not both!")
	endif(ENABLE_SSP)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fhardened")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fhardened")
endif(ENABLE_HARDENED)

# Debug & protection options require better backtraces
if(ENABLE_ASAN OR ENABLE_SSP OR ENABLE_UBSAN)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-omit-frame-pointer")
endif(ENABLE_ASAN OR ENABLE_SSP OR ENABLE_UBSAN)

# GCC/Clang specific flags:
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
	# We want to allow ‘for’-loop initial declarations a la for(int i=0; ...)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
	# Warning flags:
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-qual -Wbad-function-cast -Wpointer-arith")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes -Wstrict-prototypes")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -Wsign-compare")
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wno-unused-parameter -Wno-empty-body")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
	# Enable and disable some warnings and flags for Previous:
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -D__USE_MINGW_ANSI_STDIO=1")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-conversion")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wtautological-compare")
 	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
endif()

if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
	# Silence linker warning with AppleClang 17
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-common")
endif()

CHECK_C_COMPILER_FLAG("-Wimplicit-fallthrough=0" WARN_FALLTRHOUGH_AVAILABLE)
if(WARN_FALLTRHOUGH_AVAILABLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wimplicit-fallthrough=0")
endif(WARN_FALLTRHOUGH_AVAILABLE)

CHECK_C_COMPILER_FLAG("-Wshadow=local" WARN_SHADOW_LOCAL_AVAILABLE)
if(WARN_SHADOW_LOCAL_AVAILABLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow=local")
endif(WARN_SHADOW_LOCAL_AVAILABLE)

CHECK_C_COMPILER_FLAG("-Wvla" WARN_VARIABLE_LENGTH_ARRAY_AVAILABLE)
if(WARN_VARIABLE_LENGTH_ARRAY_AVAILABLE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wvla")
endif(WARN_VARIABLE_LENGTH_ARRAY_AVAILABLE)

if(ENABLE_WERROR)
	if (ENABLE_TRACING)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wno-error=unused-function")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=unused-but-set-variable")
	endif()
endif(ENABLE_WERROR)

# -Wpointer-bool-conversion is not in GCC yet (as of 10.2)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpointer-bool-conversion")
endif()

# Building Previous w/o optimization is no fun...
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_C_FLAGS "-O ${CMAKE_C_FLAGS}")
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")

# Always add the Large File Support flags in case they are supported
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${HOST_LFS_FLAGS}")


# ####################
# Paths configuration:
# ####################

if(NOT BINDIR)
	set(BINDIR bin)
endif()

if(NOT DATADIR)
	set(DATADIR share/previous)
endif()

if(NOT BIN2DATADIR)
	if(WIN32)
		set(BIN2DATADIR "."
		    CACHE STRING "Relative path from bindir to datadir")
	elseif(ENABLE_OSX_BUNDLE)
		set(BIN2DATADIR "../Resources"
		    CACHE STRING "Relative path from bindir to datadir")
	else()
		set(BIN2DATADIR "../share/previous"
		    CACHE STRING "Relative path from bindir to datadir")
	endif(WIN32)
	mark_as_advanced(BIN2DATADIR)
endif()

if(NOT MANDIR)
	set(MANDIR share/man/man1)
endif()

if(NOT DOCDIR)
	set(DOCDIR share/doc/previous)
endif()

if(NOT ETCDIR)
	if(WIN32)
		set(ETCDIR .)
	else()
		set(ETCDIR /etc)
	endif()
endif()

if(NOT ICONDIR)
	set(ICONDIR share/icons/hicolor)
endif()

if(ENABLE_OSX_BUNDLE)
	# put the config files in the app's bundle
	add_definitions(-DCONFDIR=\"../Resources\")
else()
	add_definitions(-DCONFDIR=\"${ETCDIR}\")
endif()

# #########################################
# Create config.h and recurse into subdirs:
# #########################################

configure_file(${CMAKE_SOURCE_DIR}/cmake/config-cmake.h
		${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/Uninstall.cmake)


# ###################################################################
# Print a summary of the optional libraries with a short explanation:
# ###################################################################

message( "
Libraries summary :
-------------------
")

message( "  - sdl :     using SDL3 ${SDL3_VERSION}")

if(PNG_FOUND)
  message( "  - png :     found, allows to print to files using png" )
else()
  message( "  - png :     not found, install it to print to files using png" )
endif(PNG_FOUND)

if(PCAP_FOUND)
  message( "  - pcap :    found, allows networking without NAT" )
else()
  message( "  - pcap :    not found, install it to use networking without NAT" )
endif(PCAP_FOUND)

if(HAVE_SYS_XATTR_H)
  message( "  - xattr.h : found, allows netbooting from a folder" )
else()
  message( "  - xattr.h : not found, no netboot possible" )
endif(HAVE_SYS_XATTR_H)

if (HAVE_HOST_LFS)
  message( "  - off_t :   ${SIZEOF_OFF_T} byte, allows image files of 2 GiB and more" )
else()
  message( "  - off_t :   ${SIZEOF_OFF_T} byte, image files must be smaller than 2 GiB" )
endif(HAVE_HOST_LFS)


# #####################################
# Print a summary of the configuration:
# #####################################

message( "
Configuration summary :
-----------------------
")

message( "  - Build configuration : ${CMAKE_BUILD_TYPE}" )

if(ENABLE_OSX_BUNDLE)
  message( "  - Build macOS bundle :  Enabled" )
else()
  message( "  - Build macOS bundle :  Disabled" )
endif(ENABLE_OSX_BUNDLE)

if(ENABLE_TRACING)
  message( "  - Tracing :             Enabled" )
else()
  message( "  - Tracing :             Disabled" )
endif(ENABLE_TRACING)

if(ENABLE_RENDERING_THREAD)
  message( "  - Rendering thread :    Enabled" )
else()
  message( "  - Rendering thread :    Disabled" )
endif(ENABLE_RENDERING_THREAD)

message( "" )
