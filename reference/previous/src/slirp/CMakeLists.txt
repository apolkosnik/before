check_include_files(sys/filio.h HAVE_SYS_FILIO_H)
if(HAVE_SYS_FILIO_H)
	add_definitions(-DHAVE_SYS_FILIO_H)
endif(HAVE_SYS_FILIO_H)

check_include_files(sys/ioctl.h HAVE_SYS_IOCTL_H)
if(HAVE_SYS_IOCTL_H)
	add_definitions(-DHAVE_SYS_IOCTL_H)
endif(HAVE_SYS_IOCTL_H)

check_include_files(unistd.h HAVE_UNISTD_H)
if(HAVE_UNISTD_H)
	add_definitions(-DHAVE_UNISTD_H)
endif(HAVE_UNISTD_H)

add_definitions(-DHAVE_STRERROR)

# Supress some warnings for SLIRP
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-compare -Wno-strict-overflow")
endif()

include_directories(${SDL3_INCLUDE_DIRS})

add_library(Slirp
            bootp.c cksum.c if.c ip_icmp.c ip_input.c ip_output.c 
            mbuf.c misc.c ntp.c rip.c sbuf.c slirp.c slirpdebug.c socket.c 
            tcp_input.c tcp_output.c tcp_subr.c tcp_timer.c tftp.c udp.c

            rpc/xdr.c rpc/rpc.c rpc/portmap.c rpc/bootparam.c rpc/mount.c 
            rpc/nfs.c rpc/netinfo.c rpc/netinfobind.c rpc/dns.c 
            rpc/csocket.c rpc/udpsocket.c rpc/tcpsocket.c 
            rpc/filetable.c rpc/vfs.c
            )

target_link_libraries(Slirp PRIVATE ${SDL3_LIBRARIES})
