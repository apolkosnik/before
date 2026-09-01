
set(SOURCES
	adb.c bmap.c cfgopts.c configuration.c change.c cycInt.c dialog.c dma.c 
	esp.c enet_slirp.c enet_pcap.c ethernet.c file.c floppy.c grab.c ioMem.c 
	ioMemTabNEXT.c ioMemTabTurbo.c kms.c m68000.c main.c mo.c nbic.c ncc.c 
	paths.c printer.c queue.c ramdac.c reset.c rom.c rs.c rtcnvram.c scandir.c 
	scc.c scsi.c shortcut.c snd.c str.c sysReg.c tablet.c timing.c tmc.c video.c 
	NextBus.cpp)

# When building for macOS, define specific sources for gui and resources
if(ENABLE_OSX_BUNDLE)
	set(GUIOSX_SOURCES
	    gui-osx/paths.m gui-osx/shortcut.m)
	set_source_files_properties(${GUIOSX_SOURCES} PROPERTIES LANGUAGE C)
	set(GUIOSX_RSRCS
	    gui-osx/Previous.icns gui-osx/en.lproj gui-osx/fr.lproj)
elseif(APPLE)
	set(SOURCES ${SOURCES} gui-osx/paths.m gui-osx/shortcut.m)
endif()

# When building for Windows, define specific sources for gui and resources
# and set the subsystem of the resulting .exe to "windows GUI" instead of "console"
if(WIN32)
	set(GUIWIN_SOURCES gui-win/opencon.c)
	set(GUIWIN_RES gui-win/previous-winicon.rc)
	#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
	add_definitions(-DSDL_MAIN_HANDLED)
endif(WIN32)

include_directories(includes debug dsp softfloat dimension slirp
		    ${CMAKE_BINARY_DIR} ${SDL3_INCLUDE_DIRS} cpu)

if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIR})
endif(PNG_FOUND)

if(PCAP_FOUND)
	include_directories(${PCAP_INCLUDE_DIR})
endif(PCAP_FOUND)

link_directories(${CMAKE_CURRENT_BINARY_DIR}/debug
		 ${CMAKE_CURRENT_BINARY_DIR}/softfloat
		 ${CMAKE_CURRENT_BINARY_DIR}/gui-sdl
		 ${CMAKE_CURRENT_BINARY_DIR}/cpu
		 ${CMAKE_CURRENT_BINARY_DIR}/dsp
		 ${CMAKE_CURRENT_BINARY_DIR}/dimension
		 ${CMAKE_CURRENT_BINARY_DIR}/slirp)

add_subdirectory(debug)
add_subdirectory(softfloat)
add_subdirectory(gui-sdl)
add_subdirectory(cpu)
add_subdirectory(dsp)
add_subdirectory(dimension)
add_subdirectory(slirp)
add_subdirectory(ditool)

# When building for macOS, add specific sources
if(ENABLE_OSX_BUNDLE)
	add_executable(${APP_NAME} MACOSX_BUNDLE ${GUIOSX_RSRCS} ${SOURCES} ${GUIOSX_SOURCES})
	set_target_properties(${APP_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST
			      ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/Info-Previous.plist)
	set_target_properties(${APP_NAME} PROPERTIES INSTALL_RPATH
			      "@executable_path/../Frameworks")
	set_target_properties(${APP_NAME} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
	set(MACOSX_BUNDLE_ICON_FILE Previous.icns)
	set(MACOSX_BUNDLE_GUI_IDENTIFIER com.sourceforge.previous)
	if(CMAKE_GENERATOR MATCHES "Xcode")
		set_target_properties(${APP_NAME} PROPERTIES
				      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
				      "com.sourceforge.previous")
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/\${CONFIGURATION}/${APP_NAME}.app/Contents)
	else()
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.app/Contents)
	endif()
	if(PNG_FOUND)
		set(COPY_PNG cp -R ${PNG_LIBRARY} ${BUNDLE_CONTENTS}/Frameworks/)
	endif()
	# Create Previous.app bundle
	add_custom_target(osx_bundle_dirs
			COMMAND rm -rf ${BUNDLE_CONTENTS}/Resources
			COMMAND rm -rf ${BUNDLE_CONTENTS}/Frameworks
			COMMAND rm -rf ${BUNDLE_CONTENTS}/MacOS
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Frameworks
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/MacOS
			COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${MACOSX_BUNDLE_ICON_FILE}
				   ${BUNDLE_CONTENTS}/Resources/${MACOSX_BUNDLE_ICON_FILE}
			# Copy Localized .nib to Bundle
			COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/*.lproj ${BUNDLE_CONTENTS}/Resources/
			# Copy ROM Images to Bundle
			COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/*.BIN ${BUNDLE_CONTENTS}/Resources/
			# Copy Frameworks to Bundle
			COMMAND cp -R $<TARGET_FILE_DIR:SDL3::SDL3-shared> ${BUNDLE_CONTENTS}/Frameworks/
			COMMAND ${COPY_PNG}
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${MACOSX_BUNDLE_ICON_FILE}
	)
	add_dependencies(${APP_NAME} osx_bundle_dirs)
	set_source_files_properties(${GUIOSX_RSRCS} PROPERTIES
				    MACOSX_PACKAGE_LOCATION Resources)

# When building for Windows, add specific sources + method to compile .rc files
elseif(WIN32)
	# Set a default rc compiler if it was not defined yet
	if(NOT CMAKE_RC_COMPILER)
		set(CMAKE_RC_COMPILER windres)
	endif(NOT CMAKE_RC_COMPILER)
	ENABLE_LANGUAGE(RC)
	set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -Ocoff -o <OBJECT> <SOURCE>")
	set_source_files_properties(${GUIWIN_RES} PROPERTIES LANGUAGE RC)
	add_executable(${APP_NAME} WIN32 ${GUIWIN_RES} ${SOURCES} ${GUIWIN_SOURCES})

# Other targets, use default sources
else()
	add_executable(${APP_NAME} ${SOURCES})
endif(ENABLE_OSX_BUNDLE)

target_link_libraries(${APP_NAME} Debug GuiSdl UaeCpu DSP Dimension Slirp SoftFloat ${SDL3_LIBRARIES})

if(Math_FOUND AND NOT APPLE)
	target_link_libraries(${APP_NAME} ${MATH_LIBRARY})
endif()

if(Readline_FOUND)
	target_link_libraries(${APP_NAME} ${READLINE_LIBRARY})
endif(Readline_FOUND)

if(PNG_FOUND)
	target_link_libraries(${APP_NAME} ${PNG_LIBRARY})
endif(PNG_FOUND)

if(PCAP_FOUND)
	target_link_libraries(${APP_NAME} ${PCAP_LIBRARY})
endif(PCAP_FOUND)

if(APPLE)
	target_link_libraries(${APP_NAME} "-framework Cocoa")
endif()

if(WIN32)
	# Needed for socket() on Windows
	target_link_libraries(${APP_NAME} ws2_32 Iphlpapi)
endif(WIN32)

if(HAIKU)
	# Needed for socket() on Haiku
	target_link_libraries(${APP_NAME} network)
endif(HAIKU)

if(ENABLE_OSX_BUNDLE)
	install(TARGETS ${APP_NAME} BUNDLE DESTINATION /Applications)
else()
	install(TARGETS ${APP_NAME} RUNTIME DESTINATION ${BINDIR})
	install(FILES Previous-icon.bmp DESTINATION ${DATADIR})
	install(FILES ND_step1_v43.BIN DESTINATION ${DATADIR})
	install(FILES Rev_0.8_v31.BIN DESTINATION ${DATADIR})
	install(FILES Rev_1.0_v41.BIN DESTINATION ${DATADIR})
	install(FILES Rev_2.5_v66.BIN DESTINATION ${DATADIR})
	install(FILES Rev_3.3_v74.BIN DESTINATION ${DATADIR})
endif(ENABLE_OSX_BUNDLE)
