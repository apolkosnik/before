/*
  Previous - dlgPrinter.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgPrinter_fileid[] = "Previous dlgPrinter.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "paths.h"


#define DLGPRINT_CONNECTED  3
#define DLGPRINT_PNG        6
#define DLGPRINT_TIFF       7
#define DLGPRINT_A4         10
#define DLGPRINT_LETTER     11
#define DLGPRINT_B5         12
#define DLGPRINT_LEGAL      13

#define DLGPRINT_BROWSE     16
#define DLGPRINT_DIRECTORY  17

#define DLGPRINT_EXIT       18

char dlgprint_dirname[64];

/* The Printer options dialog: */
static SGOBJ printerdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 47,22, NULL },
	{ SGTEXT, 0, 0, 16,1, 9,1, "Printer options" },

	{ SGBOX, 0, 0, 1,3, 22,3, NULL },
	{ SGCHECKBOX, 0, 0, 2,4, 19,1, "Printer connected" },
	
	{ SGBOX, 0, 0, 1,7, 22,4, NULL },
	{ SGTEXT, 0, 0, 2,8, 12,1, "File format:" },
	{ SGRADIOBUT, 0, 0, 15,8, 5,1, "PNG" },
	{ SGRADIOBUT, 0, 0, 15,9, 6,1, "TIFF" },

	{ SGBOX, 0, 0, 24,3, 22,8, NULL },
	{ SGTEXT, 0, 0, 25,4, 30,1, "Paper size:" },
	{ SGRADIOBUT, 0, 0, 25,6, 4,1, "A4" },
	{ SGRADIOBUT, 0, 0, 25,7, 8,1, "Letter" },
	{ SGRADIOBUT, 0, 0, 25,8, 4,1, "B5" },
	{ SGRADIOBUT, 0, 0, 25,9, 7,1, "Legal" },
	{ SGBOX, 0, 0, 1,12, 45,5, NULL },
	{ SGTEXT, 0, 0, 2,13, 19,1, "Directory to save printer output:" },
	{ SGBUTTON, 0, 0, 37,13, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 2,15, 43,1, dlgprint_dirname },
	{ SGBUTTON, SG_DEFAULT, 0, 13,19, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Show and process the Printer options dialog.
 */
void DlgPrinter_Main(void)
{
	int but;

	SDLGui_CenterDlg(printerdlg);

	/* Set up the dialog from actual values */
	if (ConfigureParams.Printer.bPrinterConnected)
		printerdlg[DLGPRINT_CONNECTED].state |= SG_SELECTED;
	else
		printerdlg[DLGPRINT_CONNECTED].state &= ~SG_SELECTED;
	
	printerdlg[DLGPRINT_PNG].state &= ~SG_SELECTED;
	printerdlg[DLGPRINT_TIFF].state &= ~SG_SELECTED;
	
	if (ConfigureParams.Printer.nFileFormat == FORMAT_PNG)
		printerdlg[DLGPRINT_PNG].state |= SG_SELECTED;
	else
		printerdlg[DLGPRINT_TIFF].state |= SG_SELECTED;
	
	printerdlg[DLGPRINT_A4].state &= ~SG_SELECTED;
	printerdlg[DLGPRINT_LETTER].state &= ~SG_SELECTED;
	printerdlg[DLGPRINT_B5].state &= ~SG_SELECTED;
	printerdlg[DLGPRINT_LEGAL].state &= ~SG_SELECTED;
	
	switch (ConfigureParams.Printer.nPaperSize) {
		case PAPER_LETTER:
			printerdlg[DLGPRINT_LETTER].state |= SG_SELECTED;
			break;
		case PAPER_B5:
			printerdlg[DLGPRINT_B5].state |= SG_SELECTED;
			break;
		case PAPER_LEGAL:
			printerdlg[DLGPRINT_LEGAL].state |= SG_SELECTED;
			break;
			
		default:
			printerdlg[DLGPRINT_A4].state |= SG_SELECTED;
			break;
	}
	
	File_ShrinkName(dlgprint_dirname, ConfigureParams.Printer.szPrintToFileName,
					printerdlg[DLGPRINT_DIRECTORY].w);
	

	/* Draw and process the dialog */
	
	do
	{
		but = SDLGui_DoDialog(printerdlg);
		
		if (but == DLGPRINT_BROWSE) {
			SDLGui_DirConfSelect(dlgprint_dirname,
			                     ConfigureParams.Printer.szPrintToFileName,
			                     printerdlg[DLGPRINT_DIRECTORY].w);
		}
	}
	while (but != DLGPRINT_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
	
	
	/* Read values from dialog */
	ConfigureParams.Printer.bPrinterConnected = printerdlg[DLGPRINT_CONNECTED].state & SG_SELECTED;
	
	if (printerdlg[DLGPRINT_PNG].state & SG_SELECTED)
		ConfigureParams.Printer.nFileFormat = FORMAT_PNG;
	else
		ConfigureParams.Printer.nFileFormat = FORMAT_TIFF;

	if (printerdlg[DLGPRINT_A4].state & SG_SELECTED)
		ConfigureParams.Printer.nPaperSize = PAPER_A4;
	else if (printerdlg[DLGPRINT_LETTER].state & SG_SELECTED)
		ConfigureParams.Printer.nPaperSize = PAPER_LETTER;
	else if (printerdlg[DLGPRINT_B5].state & SG_SELECTED)
		ConfigureParams.Printer.nPaperSize = PAPER_B5;
	else if (printerdlg[DLGPRINT_LEGAL].state & SG_SELECTED)
		ConfigureParams.Printer.nPaperSize = PAPER_LEGAL;
	else
		ConfigureParams.Printer.nPaperSize = PAPER_A4;
}
