/*
  Hatari - file_archive.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILE_ARCHIVE_H
#define HATARI_FILE_ARCHIVE_H


#include <dirent.h>

typedef struct
{
	char **names;
	int nfiles;
} archive_dir;

/* Modified for Previous: Define some empty functions as we do not use archives. */

static inline bool Archive_FileNameIsSupported(const char *FileName)
{
	return false;
}
static inline struct dirent **Archive_GetFilesDir(const archive_dir *pArcDir, const char *dir, int *pEntries)
{
	return NULL;
}
static inline archive_dir *Archive_GetFiles(const char *pszFileName)
{
	return NULL;
}
static inline void Archive_FreeArcDir(archive_dir *pArcDir)
{
}
static inline uint8_t *Archive_ReadFirstFile(const char *FileName, long *pImageSize, const char * const Exts[])
{
	return NULL;
}

#endif  /* HATARI_FILE_ARCHIVE_H */
