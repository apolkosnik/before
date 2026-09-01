project (ditool)

include_directories(../includes ../slirp)

add_executable (ditool ditool.c netboot.c im.c part.c ufs.c 
                ../rs.c ../slirp/rpc/vfs.c)
if(WIN32)
	target_link_libraries(ditool ws2_32 Iphlpapi)
endif(WIN32)

install(TARGETS ditool RUNTIME DESTINATION ${BINDIR})
