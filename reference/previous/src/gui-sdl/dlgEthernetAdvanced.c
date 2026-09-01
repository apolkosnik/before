/*
  Previous - dlgEthernetAdvanced.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgEthernetAdvanced_fileid[] = "Previous dlgEthernetAdvanced.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "paths.h"

#if HAVE_PCAP
#include <pcap.h>

#define DLGENETPCAP_CANCEL  3
#define DLGENETPCAP_SELECT  4
#define DLGENETPCAP_NEXT    5

#define PCAP_LIST_LEN       29

char pcap_list[PCAP_LIST_LEN] = "";

/* The PCAP Ethernet options dialog: */
static SGOBJ enetpcapdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 36,8, NULL },
	{ SGTEXT, 0, 0, 2,1, 31,1, "Select Host Ethernet Interface:" },
	{ SGTEXT, 0, 0, 3,3, PCAP_LIST_LEN,1, pcap_list },
	
	{ SGBUTTON, 0, 0, 4,6, 8,1, "Cancel" },
	{ SGBUTTON, SG_DEFAULT, 0, 14,6, 8,1, "Select" },
	{ SGBUTTON, 0, 0, 24,6, 8,1, "Next" },

	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Show and process the PCAP Ethernet options dialog.
 */
bool DlgEthernetAdvanced_ConfigurePCAP(void)
{
	int but;
	pcap_if_t *alldevs;
	pcap_if_t *dev;
	char name[FILENAME_MAX];
	char errbuf[PCAP_ERRBUF_SIZE];
	bool bNone;
	
	SDLGui_CenterDlg(enetpcapdlg);
	
	/* Set up the variables */
	if (pcap_findalldevs(&alldevs, errbuf) == -1)
	{
		printf("Error in pcap_findalldevs: %s\n", errbuf);
	}
	dev = alldevs;
	
	/* Draw and process the dialog */
	
	do
	{
		if (dev && dev->name) {
#ifdef _WIN32
			/* on windows use dev->description instead of dev->name */
			if (dev->description) {
				strcpy(name, dev->description);
			} else {
				strcpy(name, dev->name);
			}
#else
			strcpy(name, dev->name);
#endif
			File_ShrinkName(pcap_list, name, PCAP_LIST_LEN);
			bNone = false;
		} else {
			snprintf(pcap_list, sizeof(pcap_list), "no interface found");
			bNone = true;
		}

		but = SDLGui_DoDialog(enetpcapdlg);
		
		switch (but) {
			case DLGENETPCAP_SELECT:
				if (!bNone) {
					snprintf(ConfigureParams.Ethernet.szInterfaceName, FILENAME_MAX, "%s", dev->name);
				}
				break;
			case DLGENETPCAP_NEXT:
				if (!bNone) {
					dev = dev->next;
				}
				break;
			case DLGENETPCAP_CANCEL:
				bNone = true;
				break;
				
			default:
				break;
		}
	}
	while (but != DLGENETPCAP_SELECT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram && !bNone);
	
	pcap_freealldevs(alldevs);
	
	return !bNone;
}
#endif


#define DLGENETMAC_CUSTOM   9
#define DLGENETMAC_DEFAULT  10

char mac_input[6][4];

/* The MAC Ethernet options dialog: */
static SGOBJ enetmacdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 57,8, NULL },
	{ SGTEXT, 0, 0, 2,1, 53,1, "Set custom MAC address or use ROM programmed default:" },
	
	{ SGTEXT, 0, 0, 14,3, 29,1, "    :    :    :    :    :    " },
	
	{ SGTEXT, 0, 0, 15,3, 2,1, mac_input[0] },
	{ SGTEXT, 0, 0, 20,3, 2,1, mac_input[1] },
	{ SGTEXT, 0, 0, 25,3, 2,1, mac_input[2] },
	{ SGEDITFIELD, 0, 0, 30,3, 2,1, mac_input[3] },
	{ SGEDITFIELD, 0, 0, 35,3, 2,1, mac_input[4] },
	{ SGEDITFIELD, 0, 0, 40,3, 2,1, mac_input[5] },
	
	{ SGBUTTON, 0, 0, 15,6, 11,1, "Customise" },
	{ SGBUTTON, SG_DEFAULT, 0, 31,6, 11,1, "Default" },
	
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

static bool DlgEthernetAdvanced_GetRomMAC(Uint8 *mac)
{
	FILE* rom;
	
	/* Loading ROM depending on emulated system */
	if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
		rom = File_Open(ConfigureParams.Rom.szRom030FileName,"rb");
	} else if (ConfigureParams.System.bTurbo == true) {
		rom = File_Open(ConfigureParams.Rom.szRomTurboFileName,"rb");
	} else {
		rom = File_Open(ConfigureParams.Rom.szRom040FileName,"rb");
	}
	if (rom == NULL) {
		return false;
	}
	
	if (!File_Read(mac, 6, 8, rom)) {
		fclose(rom);
		return false;
	}
	
	fclose(rom);
	
	return true;
}

void DlgEthernetAdvanced_GetMAC(Uint8 *mac)
{
	int i;
	
	if (ConfigureParams.Rom.bUseCustomMac) {
		for (i = 0; i < 6; i++) {
			mac[i] = ConfigureParams.Rom.nRomCustomMac[i];
		}
	} else if (!DlgEthernetAdvanced_GetRomMAC(mac)) {
		for (i = 0; i < 6; i++) {
			mac[i] = 0;
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process the MAC Ethernet options dialog.
 */
void DlgEthernetAdvanced_ConfigureMAC(Uint8 *mac)
{
	int i, but;
	
	SDLGui_CenterDlg(enetmacdlg);
	
	/* Set values from ROM or preferences */
	for (i = 0; i < 6; i++) {
		snprintf(mac_input[i], sizeof(mac_input[0]), "%02x", mac[i]);
	}
	
	do
	{
		but = SDLGui_DoDialog(enetmacdlg);
		
		switch (but) {
				
			default:
				break;
		}
	}
	while (but != DLGENETMAC_CUSTOM && but != DLGENETMAC_DEFAULT &&
		   but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
	
	/* Read values from dialog */
	if (but == DLGENETMAC_CUSTOM) {
		for (i = 0; i < 6; i++) {
			ConfigureParams.Rom.nRomCustomMac[i] = (int)strtol(mac_input[i], NULL, 16);
		}
		ConfigureParams.Rom.bUseCustomMac = true;
	} else {
		ConfigureParams.Rom.bUseCustomMac = false;
	}
}
