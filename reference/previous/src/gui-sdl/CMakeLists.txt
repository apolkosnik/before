
include_directories(. ../.. ../debug ../includes ${SDL3_INCLUDE_DIRS})

if(PCAP_FOUND)
    include_directories(${PCAP_INCLUDE_DIR})
endif(PCAP_FOUND)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings")
endif()

add_library(GuiSdl 
    sdlaudio.c sdlevent.c sdlgui.c sdlhost.c sdlkeymap.c sdlscreen.c sdlstatusbar.c
    dlgAbout.c dlgAdvanced.c dlgAlert.c dlgBoot.c dlgDimension.c
    dlgEthernet.c dlgEthernetAdvanced.c dlgNFS.c dlgFileSelect.c
    dlgFloppy.c dlgGraphics.c dlgKeyboard.c dlgMain.c dlgMemory.c 
    dlgMemoryAdvanced.c dlgMissingFile.c dlgMouse.c dlgOpticalDisk.c 
    dlgPrinter.c dlgRom.c dlgSCSI.c dlgSound.c dlgSystem.c
)

target_link_libraries(GuiSdl PRIVATE ${SDL3_LIBRARIES})
