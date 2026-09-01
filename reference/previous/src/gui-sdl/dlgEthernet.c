/*
  Previous - dlgEthernet.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgEthernet_fileid[] = "Previous dlgEthernet.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "paths.h"

Uint8 mac_addr[6];
char mac_addr_string[20] = "00:00:0f:00:00:00";
char nfs_root_string[64] = "";

#if HAVE_PCAP
#define DLGENET_ENABLE      4
#define DLGENET_THIN        5
#define DLGENET_TWISTED     6

#define DLGENET_PCAP        9
#define DLGENET_SLIRP       10
#define DLGENET_TIME        11

#define DLGENET_MAC         15

#define DLGENET_NFSBROWSE   18
#define DLGENET_NFSMORE     19
#define DLGENET_NFSROOT     20

#define DLGENET_EXIT        22

#define PCAP_INTERFACE_LEN  19

char pcap_interface[PCAP_INTERFACE_LEN] = "PCAP";


/* The Ethernet options dialog: */
static SGOBJ enetdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 51,29, NULL },
	{ SGTEXT, 0, 0, 18,1, 15,1, "Network options" },
	
	{ SGBOX, 0, 0, 1,3, 24,9, NULL },
	{ SGTEXT, 0, 0, 3,4, 15,1, "Guest interface" },
	{ SGCHECKBOX, SG_EXIT, 0, 3,6, 20,1, "Ethernet connected" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,8, 11,1, "Thin wire" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,10, 14,1, "Twisted pair" },
	
	{ SGBOX, 0, 0, 26,3, 24,9, NULL },
	{ SGTEXT, 0, 0, 28,4, 14,1, "Host interface" },
	{ SGRADIOBUT, SG_EXIT, 0, 29,6, PCAP_INTERFACE_LEN,1, pcap_interface },
	{ SGRADIOBUT, SG_EXIT, 0, 29,8, 7,1, "SLiRP" },
	{ SGCHECKBOX, SG_EXIT, 0, 31,10, 14,1, "Network time" },
	
	{ SGBOX, 0, 0, 1,13, 49,3, NULL },
	{ SGTEXT, 0, 0, 3,14, 12,1, "MAC address:" },
	{ SGTEXT, 0, 0, 16,14, 17,1, mac_addr_string },
	{ SGBUTTON, 0, 0, 37,14, 10,1, "Select" },
	
	{ SGBOX, 0, 0, 1,17, 49,5, NULL },
	{ SGTEXT, 0, 0, 3,18, 12,1, "NFS shared directory:" },
	{ SGBUTTON, 0, 0, 25,18, 10,1, "Browse" },
	{ SGBUTTON, 0, 0, 37,18, 10,1, "More" },
	{ SGTEXT, 0, 0, 3,20, 44,1, nfs_root_string },

	{ SGTEXT, 0, 0, 4,23, 22,1, "Note: PCAP requires super user privileges." },
	
	{ SGBUTTON, SG_DEFAULT, 0, 15,26, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};
#else /* !HAVE_PCAP */
#define DLGENET_ENABLE      3
#define DLGENET_THIN        4
#define DLGENET_TWISTED     5
#define DLGENET_TIME        7
#define DLGENET_MAC         9
#define DLGENET_NFSMORE     13
#define DLGENET_NFSBROWSE   14
#define DLGENET_NFSROOT     15
#define DLGENET_EXIT        16

/* The Ethernet options dialog: */
static SGOBJ enetdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 53,23, NULL },
	{ SGTEXT, 0, 0, 19,1, 16,1, "Network options" },
	
	{ SGBOX, 0, 0, 1,3, 25,9, NULL },
	{ SGCHECKBOX, SG_EXIT, 0, 3,5, 20,1, "Ethernet connected" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,7, 11,1, "Thin wire" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,9, 14,1, "Twisted pair" },
	
	{ SGBOX, 0, 0, 27,3, 25,9, NULL },
	{ SGCHECKBOX, 0, 0, 29,5, 18,1, "Use network time" },
	{ SGTEXT, 0, 0, 29,8, 12,1, "MAC address:" },
	{ SGBUTTON, 0, 0, 42,8, 8,1, "Select" },
	{ SGTEXT, 0, 0, 31,10, 17,1, mac_addr_string },
	
	{ SGBOX, 0, 0, 1,13, 51,5, NULL },
	{ SGTEXT, 0, 0, 3,14, 12,1, "NFS shared directory:" },
	{ SGBUTTON, 0, 0, 28,14, 10,1, "More" },
	{ SGBUTTON, 0, 0, 40,14, 10,1, "Browse" },
	{ SGTEXT, 0, 0, 3,16, 47,1, nfs_root_string },
	
	{ SGBUTTON, SG_DEFAULT, 0, 16,20, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};
#endif


/*-----------------------------------------------------------------------*/
/**
 * Show and process the Ethernet options dialog.
 */
void DlgEthernet_Main(void)
{
	int but;

	SDLGui_CenterDlg(enetdlg);

	/* Set up the dialog from actual values */
	
	enetdlg[DLGENET_ENABLE].state &= ~SG_SELECTED;
	enetdlg[DLGENET_THIN].state &= ~SG_SELECTED;
	enetdlg[DLGENET_TWISTED].state &= ~SG_SELECTED;
	enetdlg[DLGENET_TIME].state &= ~SG_SELECTED;

	if (ConfigureParams.Ethernet.bEthernetConnected) {
		enetdlg[DLGENET_ENABLE].state |= SG_SELECTED;
		if (ConfigureParams.Ethernet.bTwistedPair) {
			enetdlg[DLGENET_TWISTED].state |= SG_SELECTED;
		} else {
			enetdlg[DLGENET_THIN].state |= SG_SELECTED;
		}
	}
#if HAVE_PCAP
	enetdlg[DLGENET_SLIRP].state &= ~SG_SELECTED;
	enetdlg[DLGENET_PCAP].state &= ~SG_SELECTED;

	if (ConfigureParams.Ethernet.nHostInterface == ENET_PCAP) {
		enetdlg[DLGENET_PCAP].state |= SG_SELECTED;
		snprintf(pcap_interface, PCAP_INTERFACE_LEN, "PCAP: %.12s", ConfigureParams.Ethernet.szInterfaceName);
	} else {
		enetdlg[DLGENET_SLIRP].state |= SG_SELECTED;
		snprintf(pcap_interface, sizeof(pcap_interface), "PCAP");
	}
#endif

	if (ConfigureParams.Ethernet.bNetworkTime) {
		enetdlg[DLGENET_TIME].state |= SG_SELECTED;
	}

	/* Draw and process the dialog */
	
	do
	{
		DlgEthernetAdvanced_GetMAC(mac_addr);
		
		snprintf(mac_addr_string, sizeof(mac_addr_string), "%02x:%02x:%02x:%02x:%02x:%02x",
		         mac_addr[0],mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

		File_ShrinkName(nfs_root_string, ConfigureParams.Ethernet.nfs[0].szPathName,
						enetdlg[DLGENET_NFSROOT].w);

		but = SDLGui_DoDialog(enetdlg);
		
		switch (but) {
			case DLGENET_ENABLE:
				if (enetdlg[DLGENET_ENABLE].state & SG_SELECTED) {
					if (ConfigureParams.Ethernet.bTwistedPair) {
						enetdlg[DLGENET_TWISTED].state |= SG_SELECTED;
					} else {
						enetdlg[DLGENET_THIN].state |= SG_SELECTED;
					}
				} else {
					enetdlg[DLGENET_THIN].state &= ~SG_SELECTED;
					enetdlg[DLGENET_TWISTED].state &= ~SG_SELECTED;
				}
				break;
			case DLGENET_THIN:
			case DLGENET_TWISTED:
				if (!(enetdlg[DLGENET_ENABLE].state & SG_SELECTED)) {
					enetdlg[DLGENET_ENABLE].state |= SG_SELECTED;
				}
				break;
#if HAVE_PCAP
			case DLGENET_PCAP:
				if (DlgEthernetAdvanced_ConfigurePCAP()) {
					snprintf(pcap_interface, sizeof(pcap_interface), "PCAP: %.12s", ConfigureParams.Ethernet.szInterfaceName);
					enetdlg[DLGENET_TIME].state &= ~SG_SELECTED;
					break;
				}
			case DLGENET_TIME:
				enetdlg[DLGENET_PCAP].state &= ~SG_SELECTED;
				enetdlg[DLGENET_SLIRP].state |= SG_SELECTED;
			case DLGENET_SLIRP:
				snprintf(pcap_interface, sizeof(pcap_interface), "PCAP");
				break;
#endif
			case DLGENET_MAC:
				DlgEthernetAdvanced_ConfigureMAC(mac_addr);
				break;
			case DLGENET_NFSBROWSE:
				SDLGui_DirConfSelect(nfs_root_string,
				                     ConfigureParams.Ethernet.nfs[0].szPathName,
				                     enetdlg[DLGENET_NFSROOT].w);
				break;
			case DLGENET_NFSMORE:
				DlgNFS();
				break;
				
			default:
				break;
		}
	}
	while (but != DLGENET_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
	
	
	/* Read values from dialog */
	ConfigureParams.Ethernet.bEthernetConnected = enetdlg[DLGENET_ENABLE].state & SG_SELECTED;
	ConfigureParams.Ethernet.bTwistedPair = enetdlg[DLGENET_TWISTED].state & SG_SELECTED;
	ConfigureParams.Ethernet.bNetworkTime = enetdlg[DLGENET_TIME].state & SG_SELECTED;
#if HAVE_PCAP
	if (enetdlg[DLGENET_PCAP].state & SG_SELECTED) {
		ConfigureParams.Ethernet.nHostInterface = ENET_PCAP;
	} else {
		ConfigureParams.Ethernet.nHostInterface = ENET_SLIRP;
	}
#endif
}
