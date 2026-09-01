/*
  Previous - grab.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Grab video or sound output and save it to a PNG or AIFF file.
*/
const char Grab_fileid[] = "Previous grab.c";

#include "main.h"
#include "configuration.h"
#include "log.h"
#include "dimension.hpp"
#include "file.h"
#include "paths.h"
#include "statusbar.h"
#include "m68000.h"
#include "host.h"
#include "screen.h"
#include "grab.h"

#if HAVE_LIBPNG
#include <png.h>
#endif

#if ND_STEP
#define ND_OFFSET 0
#else
#define ND_OFFSET (4*4)
#endif

struct grab_format {
	uint32_t w;
	uint32_t h;
	int dpi;
	int rgb;
	int alpha;
	int depth;
	int print;
};

#if HAVE_LIBPNG
/**
 * Create PNG file.
 */
static bool Grab_MakePNG(FILE* fp, uint8_t* src_ptr, struct grab_format* format) {
	png_structp png_ptr    = NULL;
	png_infop   info_ptr   = NULL;
	png_text    pngtext;
	
	char        key[8]     = "Title";
	char        text[16]   = "Previous Screen";
	
	int         color_type = 0;
	int         bpp        = 0;
	uint32_t    res        = 0;
	uint32_t    y          = 0;
	bool        result     = false;
	
	/* Setup variable parameters from requested format */
	res = (format->dpi * 10000) / 254;
	if (format->rgb) {
		if (format->alpha) {
			color_type = PNG_COLOR_TYPE_RGB_ALPHA;
			bpp = format->depth * 4;
		} else {
			color_type = PNG_COLOR_TYPE_RGB;
			bpp = format->depth * 3;
		}
	} else {
		if (format->print) {
			snprintf(text, sizeof(text), "%s", "Previous Print");
			color_type = PNG_COLOR_TYPE_PALETTE;
		} else {
			color_type = PNG_COLOR_TYPE_GRAY;
		}
		bpp = format->depth;
	}

	if (src_ptr) {
		/* Create and initialize the png_struct with error handler functions. */
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (png_ptr) {
			/* Allocate/initialize the image information data. */
			info_ptr = png_create_info_struct(png_ptr);
			if (info_ptr) {
				/* libpng ugliness: Set error handling when not supplying own
				 * error handling functions in the png_create_write_struct() call.
				 */
				if (!setjmp(png_jmpbuf(png_ptr))) {
					/* initialize the png structure */
					png_init_io(png_ptr, fp);
					
					/* image data properties */
					png_set_IHDR(png_ptr, info_ptr, format->w, format->h, format->depth, color_type,
					             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
					
					/* image info */
					pngtext.key = key;
					pngtext.text = text;
					pngtext.compression = PNG_TEXT_COMPRESSION_NONE;
#ifdef PNG_iTXt_SUPPORTED
					pngtext.lang = NULL;
#endif
					png_set_text(png_ptr, info_ptr, &pngtext, 1);
					
					/* image resolution */
					png_set_pHYs(png_ptr, info_ptr, res, res, 1);
					
					/* color palette */
					if (color_type == PNG_COLOR_TYPE_PALETTE) {
						png_color palette[2];
						palette[0].red = palette[0].green = palette[0].blue = 0xff;
						palette[1].red = palette[1].green = palette[1].blue = 0x00;
						png_set_PLTE(png_ptr, info_ptr, palette, 2);
					}
					
					/* write the file header information */
					png_write_info(png_ptr, info_ptr);
					
					/* write image data */
					for (y = 0; y < format->h; y++) {
						png_write_row(png_ptr, src_ptr);
						src_ptr += (format->w * bpp) / 8;
					}
					
					/* write the additional chunks to the PNG file */
					png_write_end(png_ptr, info_ptr);
					
					result = true;
				}
			}
			/* handles info_ptr being NULL */
			png_destroy_write_struct(&png_ptr, &info_ptr);
		}
	}	
	return result;
}
#endif


/**
 * Create TIFF file.
 */
struct ifd_t {
	uint8_t* data;
	uint32_t datapos;
	uint32_t datasize;
	uint32_t size;
	
	uint16_t num_entries;
	uint16_t width;
	uint16_t height;
	uint16_t bits_per_sample[4];
	uint16_t compression;
	uint16_t photometric_interpretation;
	uint32_t strip_offset;
	uint16_t samples_per_pixel;
	uint32_t strip_byte_count;
	uint32_t x_resolution[2];
	uint32_t y_resolution[2];
	uint16_t planar_configuration;
	uint16_t resolution_unit;
	uint16_t extra_samples;
	
	struct ifd_t* next;
};

struct tif_t {
	uint8_t data[256];
	uint8_t* write;
	uint8_t* limit;
	uint32_t curpos;

	uint16_t byte_order;
	uint16_t version;
	struct ifd_t* ifd;
};

static void tiff_write16(struct tif_t* tif, uint16_t val) {
	if (tif->limit - tif->write < 2) {
		return;
	}
	val = be_swap16(val);
	memcpy(tif->write, &val, sizeof(uint16_t));
	tif->write += 2;
}

static void tiff_write32(struct tif_t* tif, uint32_t val) {
	if (tif->limit - tif->write < 4) {
		return;
	}
	val = be_swap32(val);
	memcpy(tif->write, &val, sizeof(uint32_t));
	tif->write += 4;
}

static void tiff_write_field_data(struct tif_t* tif, int count, int size, uint8_t* data) {
	struct ifd_t* ifd = tif->ifd;
	
	if (count * size > 4) {
		tiff_write32(tif, ifd->datapos + ifd->datasize);
		while (count-- && (tif->limit - (ifd->data + ifd->datasize) >= size)) {
			if (size == 2) {
				uint16_t val = be_swap16(*(uint16_t*)data);
				memcpy(ifd->data + ifd->datasize, &val, size);
			} else if (size == 4) {
				uint32_t val = be_swap32(*(uint32_t*)data);
				memcpy(ifd->data + ifd->datasize, &val, size);
			}
			ifd->datasize += size;
			data += size;
		}
	} else {
		if (size == 2) {
			tiff_write16(tif, *(uint16_t*)data);
			tiff_write16(tif, (count > 1) ? (*(uint16_t*)(data + 2)) : 0);
		} else if (size == 4) {
			tiff_write32(tif, *(uint32_t*)data);
		}
	}
}

static void tiff_write_field(struct tif_t* tif, uint16_t tag, uint16_t type, int count, uint8_t* data) {
	tiff_write16(tif, tag);
	tiff_write16(tif, type);
	tiff_write32(tif, count);
	
	switch (type) {
		case 3:
			tiff_write_field_data(tif, count, 2, data);
			break;
			
		case 5:
			count *= 2;
		case 4:
			tiff_write_field_data(tif, count, 4, data);
			break;
			
		default:
			break;
	}
}

static void tiff_write_ifd(struct tif_t* tif) {
	struct ifd_t* ifd = tif->ifd;
	
	tiff_write16(tif, ifd->num_entries);
	
	tiff_write_field(tif, 0x0100, 3, 1, (uint8_t*)&ifd->width);
	tiff_write_field(tif, 0x0101, 3, 1, (uint8_t*)&ifd->height);
	tiff_write_field(tif, 0x0102, 3, ifd->samples_per_pixel, (uint8_t*)ifd->bits_per_sample);
	tiff_write_field(tif, 0x0103, 3, 1, (uint8_t*)&ifd->compression);
	tiff_write_field(tif, 0x0106, 3, 1, (uint8_t*)&ifd->photometric_interpretation);
	tiff_write_field(tif, 0x0111, 4, 1, (uint8_t*)&ifd->strip_offset);
	tiff_write_field(tif, 0x0115, 3, 1, (uint8_t*)&ifd->samples_per_pixel);
	tiff_write_field(tif, 0x0117, 4, 1, (uint8_t*)&ifd->strip_byte_count);
	tiff_write_field(tif, 0x011A, 5, 1, (uint8_t*)ifd->x_resolution);
	tiff_write_field(tif, 0x011B, 5, 1, (uint8_t*)ifd->y_resolution);
	tiff_write_field(tif, 0x011C, 3, 1, (uint8_t*)&ifd->planar_configuration);
	tiff_write_field(tif, 0x0128, 3, 1, (uint8_t*)&ifd->resolution_unit);
	if (ifd->extra_samples) {
		tiff_write_field(tif, 0x0152, 3, 1, (uint8_t*)&ifd->extra_samples);
	}
	
	if (ifd->next) {
		tiff_write32(tif, tif->curpos + ifd->size + ifd->datasize + ifd->next->strip_byte_count);
	} else {
		tiff_write32(tif, 0);
	}
	
	/* jump over field data as it is already present in write buffer */
	tif->write += ifd->datasize;
}

static int tiff_file_write(struct tif_t* tif, FILE* fp, uint8_t* data, uint32_t size) {
	int result;
	result = File_Write(data, size, tif->curpos, fp) == false;
	tif->curpos += size;
	return result;
}

static bool Grab_MakeTIFF(FILE* fp, uint8_t* buf, struct grab_format* format) {
	struct tif_t* tif;
	struct ifd_t* ifd;
	uint32_t imgsz;
	int i, bpp, status;
	
	/* calculate image size */
	bpp = format->depth;
	if (format->rgb) {
		bpp *= format->alpha ? 4 : 3;
	}
	imgsz = (format->w * format->h * bpp) / 8;
	
	tif = (struct tif_t*)calloc(1, sizeof(struct tif_t));
	if (tif) {
		/* setup write buffer and initial file position */
		tif->write  = tif->data;
		tif->limit  = tif->data + sizeof(tif->data);
		tif->curpos = 0;
		
		/* set default header info */
		tif->byte_order = 0x4D4D; /* big endian */
		tif->version    = 42;
		
		/* write header to buffer */
		tiff_write16(tif, tif->byte_order);
		tiff_write16(tif, tif->version);
		tiff_write32(tif, 8 + imgsz);
		
		/* write header to file */
		status = tiff_file_write(tif, fp, tif->data, tif->write - tif->data);
		
		ifd = tif->ifd = (struct ifd_t*)calloc(1, sizeof(struct ifd_t));
		if (ifd) {
			/* rewind buffer for re-use */
			tif->write = tif->data;
			memset(tif->data, 0, sizeof(tif->data));
			
			/* set number of IFD entries and IFD size (without data) */
			ifd->num_entries = 12 + format->alpha;
			ifd->size = 2 + ifd->num_entries * 12 + 4;
			
			/* image data is located in front of IFD */
			ifd->strip_offset = tif->curpos;
			
			/* write image data to file */
			status |= tiff_file_write(tif, fp, buf, imgsz);
			
			/* IFD data is located after IFD entries */
			ifd->datapos = tif->curpos + ifd->size;
			ifd->data    = tif->data + ifd->size;
			
			/* set IFD entries from requested format */
			ifd->width   = format->w;
			ifd->height  = format->h;
			ifd->samples_per_pixel = format->rgb ? (format->alpha ? 4 : 3) : 1;
			for (i = 0; i < ifd->samples_per_pixel; i++) {
				ifd->bits_per_sample[i] = format->depth;
			}
			ifd->compression = 1;
			ifd->photometric_interpretation = format->rgb ? 2 : (format->print ? 0 : 1);
			ifd->strip_byte_count = imgsz;
			ifd->x_resolution[0] = ifd->y_resolution[0] = format->dpi * 10000;
			ifd->x_resolution[1] = ifd->y_resolution[1] = 10000;
			ifd->planar_configuration = format->rgb ? 1 : 2;
			ifd->resolution_unit = 2;
			ifd->extra_samples = format->alpha ? 2 : 0;
			
			/* only one IFD for now */
			ifd->next = NULL;
			
			/* write IFD entries and IFD data to buffer */
			tiff_write_ifd(tif);
			
			/* write IFD entries and IFD data to file */
			status |= tiff_file_write(tif, fp, tif->data, tif->write - tif->data);
			
			free(tif->ifd);
		} else {
			status = 1;
		}
		free(tif);
	} else {
		status = 1;
	}
	
	return (status == 0);
}

/**
 * Create path from file name and add file type extension.
 */
static char* Grab_GetPath(const char* szName, int nCount, int nDigits) {
	int i;
	char szFileName[32];
	char *szPathName = NULL;
	
	if (File_DirExists(ConfigureParams.Printer.szPrintToFileName)) {
		for (i = 0; i < nCount; i++) {
			snprintf(szFileName, sizeof(szFileName), "%s_%.*d", szName, nDigits, i);
#if HAVE_LIBPNG
			if (ConfigureParams.Printer.nFileFormat == FORMAT_PNG) {
				szPathName = File_MakePath(ConfigureParams.Printer.szPrintToFileName, szFileName, "png");
			} else
#endif
			{
				szPathName = File_MakePath(ConfigureParams.Printer.szPrintToFileName, szFileName, "tiff");
			}
			
			if (!File_Exists(szPathName)) {
				return szPathName;
			}
			free(szPathName);
		}
	}
	Log_Printf(LOG_WARN, "[Grab] Error: Maximum image count exceeded (%d)", nCount);
	
	return NULL;
}

/**
 * Open file and save image data to it.
 */
static bool Grab_SaveFile(const char* szName, int nCount, int nDigits, uint8_t* buf, struct grab_format* format) {
	bool result = false;
	char* szPathName = NULL;
	FILE* fp = NULL;
	
	szPathName = Grab_GetPath(szName, nCount, nDigits);
	if (szPathName) {
		fp = File_Open(szPathName, "wb");
		if (fp) {
#if HAVE_LIBPNG
			if (ConfigureParams.Printer.nFileFormat == FORMAT_PNG) {
				result = Grab_MakePNG(fp, buf, format);
			} else
#endif
			{
				result = Grab_MakeTIFF(fp, buf, format);
			}
			if (result == false) {
				Log_Printf(LOG_WARN, "[Grab] Error: Could not create image file");
			}
			File_Close(fp);
		} else {
			Log_Printf(LOG_WARN, "[Grab] Error: Could not open file %s", szPathName);
		}
		free(szPathName);
	}
	return result;
}

/**
 * Set format, convert framebuffer data and fill buffer.
 */
static bool Grab_ScreenFormat(int slot, uint8_t* buf, struct grab_format* format) {
	uint8_t* src;
	uint8_t* dst;
	
	int x, y;
	
	format->w     = screen_w;
	format->h     = screen_h;
	format->dpi   = 72; /* Better use MegaPixel Display resolution (92 dpi)? */
	format->print = 0;
	
	if (slot < 0) {
		int m, xoff, yoff;
		
		format->rgb   = 1;
		format->alpha = 1;
		format->depth = 8;
		
		for (m = 0; m < NUM_MONITORS; m++) {
			if (ConfigureParams.Screen.nGroupModePos[m] < 0) {
				continue;
			}
			xoff = (ConfigureParams.Screen.nGroupModePos[m] % NUM_MONITORS) * NeXT_SCRN_W;
			yoff = (ConfigureParams.Screen.nGroupModePos[m] / NUM_MONITORS) * NeXT_SCRN_H;
			slot = m * 2;
			
			dst = buf + screen_w * yoff * 4;
			src = (slot > 0) ? (uint8_t*)(nd_vram_for_slot(slot)) : NEXTVideo;
			if (!src || (xoff + NeXT_SCRN_W) > screen_w || (yoff + NeXT_SCRN_H) > screen_h) {
				return false;
			}
			
			src += (slot > 0) ? ND_OFFSET : 0;
			
			for (y = 0; y < NeXT_SCRN_H; y++) {
				dst += xoff * 4;
				if (slot > 0) {
					for (x = 0; x < NeXT_SCRN_W; x++, src += 4, dst += 4) {
						dst[0] = src[2]; /* r */
						dst[1] = src[1]; /* g */
						dst[2] = src[0]; /* b */
						dst[3] = 0xff;   /* a */
					}
					src += 32 * 4;
				} else if (ConfigureParams.System.bColor) {
					for (x = 0; x < NeXT_SCRN_W; x++, src += 2, dst += 4) {
						dst[0] = ((src[0] & 0xf0) >> 4) * 0x11; /* r */
						dst[1] = ((src[0] & 0x0f) >> 0) * 0x11; /* g */
						dst[2] = ((src[1] & 0xf0) >> 4) * 0x11; /* b */
						dst[3] = 0xff;                          /* a */
					}
					src += ConfigureParams.System.bTurbo ? 0 : (32 * 2);
				} else {
					for (x = 0; x < NeXT_SCRN_W; x += 4, src++, dst += 4 * 4) {
						dst[0]  = dst[1]  = dst[2]  = (~(*src >> 6) & 3) * 0x55; dst[3]  = 0xff; /* rgba */
						dst[4]  = dst[5]  = dst[6]  = (~(*src >> 4) & 3) * 0x55; dst[7]  = 0xff; /* rgba */
						dst[8]  = dst[9]  = dst[10] = (~(*src >> 2) & 3) * 0x55; dst[11] = 0xff; /* rgba */
						dst[12] = dst[13] = dst[14] = (~(*src >> 0) & 3) * 0x55; dst[15] = 0xff; /* rgba */
					}
					src += ConfigureParams.System.bTurbo ? 0 : (32 / 4);
				}
				dst += (screen_w - (xoff + NeXT_SCRN_W)) * 4;
			}
		}
	} else {
		src = (slot > 0) ? (uint8_t*)(nd_vram_for_slot(slot)) : NEXTVideo;
		dst = buf;
		if (!src || NeXT_SCRN_W > screen_w || NeXT_SCRN_H > screen_h) {
			return false;
		}
		
		if (slot > 0) {
			format->rgb   = 1;
			format->alpha = 0;
			format->depth = 8;
			
			src += ND_OFFSET;
			
			for (y = 0; y < NeXT_SCRN_H; y++) {
				for (x = 0; x < NeXT_SCRN_W; x++, src += 4, dst += 3) {
					dst[0] = src[2]; /* r */
					dst[1] = src[1]; /* g */
					dst[2] = src[0]; /* b */
				}
				src += 32 * 4;
			}
		} else if (ConfigureParams.System.bColor) {
			format->rgb   = 1;
			format->alpha = 0;
			format->depth = 4;
#if HAVE_LIBPNG
			/* PNG does not support RGB444 */
			if (ConfigureParams.Printer.nFileFormat == FORMAT_PNG) {
				format->depth = 8;
				
				for (y = 0; y < NeXT_SCRN_H; y++) {
					for (x = 0; x < NeXT_SCRN_W; x++, src += 2, dst += 3) {
						dst[0] = ((src[0] & 0xf0) >> 4) * 0x11; /* r */
						dst[1] = ((src[0] & 0x0f) >> 0) * 0x11; /* g */
						dst[2] = ((src[1] & 0xf0) >> 4) * 0x11; /* b */
					}
					src += ConfigureParams.System.bTurbo ? 0 : (32 * 2);
				}
			} else
#endif
			{
				for (y = 0; y < NeXT_SCRN_H; y++) {
					for (x = 0; x < NeXT_SCRN_W; x += 2, src += 4, dst += 3) {
						dst[0] = src[0];                          /* rg */
						dst[1] = (src[1] & 0xf0) | (src[2] >> 4); /* br */
						dst[2] = (src[2] << 4) | (src[3] >> 4);   /* gb */
					}
					src += ConfigureParams.System.bTurbo ? 0 : (32 * 2);
				}
			}
		} else {
			format->rgb   = 0;
			format->alpha = 0;
			format->depth = 2;
			
			for (y = 0; y < NeXT_SCRN_H; y++) {
				for (x = 0; x < NeXT_SCRN_W; x += 4) {
					*dst++ = ~*src++; /* 2-bit gray */
				}
				src += ConfigureParams.System.bTurbo ? 0 : (32 / 4);
			}
		}
	}
	return true;
}

/**
 * Grab screen from board at certain slot.
 */
static void Grab_ScreenFromSlot(int slot) {
	uint8_t* buffer = (uint8_t*)calloc(1, screen_w * screen_h * 4);
	if (buffer) {
		struct grab_format format;
		if (Grab_ScreenFormat(slot, buffer, &format)) {
			if (Grab_SaveFile("next_screen", 1000, 3, buffer, &format)) {
				Statusbar_AddMessage("Saving screen to file", 0);
			}
		}
		free(buffer);
	}
}

/**
 * Grab screen.
 */
void Grab_Screen(void) {
	if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
		Grab_ScreenFromSlot(-1);
	} else if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) {
		Grab_ScreenFromSlot(ConfigureParams.Screen.nSingleModeSlot);
	} else {
		int i;
		Grab_ScreenFromSlot(0);
		for (i = 0; i < ND_MAX_BOARDS; i++) {
			if (ConfigureParams.Dimension.board[i].bEnabled) {
				Grab_ScreenFromSlot(ND_SLOT(i));
			}
		}
	}
}

/**
 * Grab print.
 */
void Grab_Print(uint8_t* data, int width, int height, int dpi) {
	if (data) {
		struct grab_format format;
		format.w     = width;
		format.h     = height;
		format.dpi   = dpi;
		format.rgb   = 0;
		format.alpha = 0;
		format.depth = 1;
		format.print = 1;
		Grab_SaveFile("next_print", 1000000, 6, data, &format);
	}
}


/*
 AIFF file output
 
 We simply save out the AIFF format headers and then write the sample data. 
 When we stop recording we complete the size information in the headers and 
 close up.
 
 All data is stored in big endian byte order.
 
 Format Chunk (12 bytes in length total) Byte Number
 0 - 3    "FORM" (ASCII Characters)
 4 - 7    Total Length Of Package To Follow (Binary, big endian)
 8 - 11   "AIFF" (ASCII Characters)
 
 Common Chunk (24 bytes in length total) Byte Number
 0 - 3    "COMM" (ASCII Characters)
 4 - 7    Length Of COMM Chunk (Binary, always 18)
 8 - 9    Number Of Channles (1=Mono, 2=Stereo)
 10 - 13  Number Of Samples
 14 - 15  Bits Per Sample
 16 - 25  Sample Rate (80-bit IEEE 754 floating point number, in Hz)
 
 SSND Chunk Byte Number
 0 - 3    "SSND" (ASCII Characters)
 4 - 7    Length Of Data To Follow
 8 - 11   Offset
 12 - 15  Block Size
 16 - end Data (Samples)
 */

static lock_t GrabSoundLock;               /* Protect AIFF file and variables */

static FILE*  AiffFileHndl;                /* Pointer to our AIFF file */
static int    nAiffOutputBytes;            /* Number of sample bytes saved */
volatile bool bRecordingAiff = false;      /* Is an AIFF file open and recording? */

static uint8_t AiffHeader[54] =
{
	/* Format chunk */
	'F', 'O', 'R', 'M',      /* "FORM" (ASCII Characters) */
	0, 0, 0, 0,              /* Total Length Of Package To Follow (patched when file is closed) */
	'A', 'I', 'F', 'F',      /* "AIFF" (ASCII Characters) */
	/* Common chunk */
	'C', 'O', 'M', 'M',      /* "COMM" (ASCII Characters) */
	0, 0, 0, 18,             /* Length Of COMM Chunk (18) */
	0, 2,                    /* Number of channels (2 for stereo) */
	0, 0, 0, 0,              /* Number of samples (patched when file is closed) */
	0, 16,                   /* Bits per sample (16 bit) */
	0x40, 0x0e, 0xac, 0x44,  /* Sample rate (44,1 kHz) */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
	/* Sound data chunk */
	'S', 'S', 'N', 'D',      /* "SSND" (ASCII Characters) */
	0, 0, 0, 0,              /* Length of SSND Chunk (patched when file is closed) */
	0, 0, 0, 0,              /* Offset */
	0, 0, 0, 0               /* Block size */
};


/**
 * Write sizes to AIFF header, then close the AIFF file.
 */
static void Grab_CloseSoundFile(void)
{
	uint32_t nAiffFileBytes;
	uint32_t nAiffDataBytes;
	uint32_t nAiffSamples;
	
	if (bRecordingAiff)
	{
		bRecordingAiff = false;
		
		/* Update headers with sizes */
		nAiffFileBytes = 46+nAiffOutputBytes; /* length of headers minus 8 bytes plus length of data */
		nAiffDataBytes = 8+nAiffOutputBytes;  /* length of data plus 8 bytes */
		nAiffSamples   = nAiffOutputBytes/2;  /* length of data divided by bytes per sample */
		
		/* Patch length of file in header structure */
		AiffHeader[4] = (uint8_t)((nAiffFileBytes >> 24) & 0xff);
		AiffHeader[5] = (uint8_t)((nAiffFileBytes >> 16) & 0xff);
		AiffHeader[6] = (uint8_t)((nAiffFileBytes >>  8) & 0xff);
		AiffHeader[7] = (uint8_t)((nAiffFileBytes >>  0) & 0xff);
		
		/* Patch number of samples in header structure */
		AiffHeader[22] = (uint8_t)((nAiffSamples >> 24) & 0xff);
		AiffHeader[23] = (uint8_t)((nAiffSamples >> 16) & 0xff);
		AiffHeader[24] = (uint8_t)((nAiffSamples >>  8) & 0xff);
		AiffHeader[25] = (uint8_t)((nAiffSamples >>  0) & 0xff);

		/* Patch length of data in header structure */
		AiffHeader[42] = (uint8_t)((nAiffDataBytes >> 24) & 0xff);
		AiffHeader[43] = (uint8_t)((nAiffDataBytes >> 16) & 0xff);
		AiffHeader[44] = (uint8_t)((nAiffDataBytes >>  8) & 0xff);
		AiffHeader[45] = (uint8_t)((nAiffDataBytes >>  0) & 0xff);
		
		/* Write updated header to file */
		if (!File_Write(AiffHeader, sizeof(AiffHeader), 0, AiffFileHndl))
		{
			perror("[Grab] Grab_CloseSoundFile:");
		}
		
		/* Close file */
		AiffFileHndl = File_Close(AiffFileHndl);
		
		/* And inform user */
		Log_Printf(LOG_WARN, "[Grab] Stopping sound record");
		Statusbar_AddMessage("Stop saving sound to file", 0);
	}
}

/**
 * Open AIFF output file and write header.
 */
static void Grab_OpenSoundFile(void)
{
	int i;
	char szFileName[32];
	char *szPathName = NULL;
	
	if (bRecordingAiff) {
		Grab_CloseSoundFile();
	}
	
	nAiffOutputBytes = 0;
	
	if (File_DirExists(ConfigureParams.Printer.szPrintToFileName)) {
		
		/* Build file name */
		for (i = 0; i < 1000; i++) {
			snprintf(szFileName, sizeof(szFileName), "next_sound_%03d", i);
			szPathName = File_MakePath(ConfigureParams.Printer.szPrintToFileName, szFileName, "aiff");
			
			if (File_Exists(szPathName)) {
				free(szPathName);
				continue;
			}
			
			/* Create our file */
			AiffFileHndl = File_Open(szPathName, "wb");
			if (AiffFileHndl) {
				
				/* Write header to file */
				if (File_Write(AiffHeader, sizeof(AiffHeader), 0, AiffFileHndl)) {
					bRecordingAiff = true;
					Log_Printf(LOG_WARN, "[Grab] Starting sound record");
					Statusbar_AddMessage("Start saving sound to file", 0);
				} else {
					perror("[Grab] Grab_OpenSoundFile:");
				}
			} else {
				Log_Printf(LOG_WARN, "[Grab] Failed to create sound file %s: ", szPathName);
			}
			free(szPathName);
			return;
		}
		
		Log_Printf(LOG_WARN, "[Grab] Error: Maximum sound grab count exceeded (%d)", i);
	}
}

/**
 * Update AIFF file with current samples.
 */
void Grab_Sound(uint8_t* samples, int len)
{
	host_lock(&GrabSoundLock);
	if (bRecordingAiff)
	{
		/* Append samples to our AIFF file */
		if (fwrite(samples, len, 1, AiffFileHndl) != 1)
		{
			perror("[Grab] Grab_Sound:");
			Grab_CloseSoundFile();
		}

		/* Add samples to AIFF file length counter */
		nAiffOutputBytes += len;
	}
	host_unlock(&GrabSoundLock);
}

/**
 * Start/Stop recording sound.
 */
void Grab_SoundToggle(void) {
	host_lock(&GrabSoundLock);
	if (bRecordingAiff) {
		Grab_CloseSoundFile();
	} else {
		Grab_OpenSoundFile();
	}
	host_unlock(&GrabSoundLock);
}

/**
 * Stop any recording activities.
 */
void Grab_Stop(void) {
	host_lock(&GrabSoundLock);
	Grab_CloseSoundFile();
	host_unlock(&GrabSoundLock);
}
