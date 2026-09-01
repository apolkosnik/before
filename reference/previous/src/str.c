/*
  Hatari - str.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_fileid[] = "Hatari str.c";

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "str.h"


/**
 * Remove whitespace from beginning and end of a string.
 * Returns the trimmed string (string content is moved
 * so that it still starts from the same address)
 */
char *Str_Trim(char *buffer)
{
	int i, linelen;

	if (buffer == NULL)
		return NULL;

	linelen = strlen(buffer);

	for (i = 0; i < linelen; i++)
	{
		if (!isspace((unsigned char)buffer[i]))
			break;
	}

	if (i > 0 && i < linelen)
	{
		linelen -= i;
		memmove(buffer, buffer + i, linelen);
	}

	for (i = linelen; i > 0; i--)
	{
		if (!isspace((unsigned char)buffer[i-1]))
			break;
	}

	buffer[i] = '\0';

	return buffer;
}


/**
 * Convert a string to uppercase in place.
 */
char *Str_ToUpper(char *pString)
{
	char *str = pString;
	while (*str)
	{
		*str = toupper((unsigned char)*str);
		str++;
	}
	return pString;
}


/**
 * Convert string to lowercase in place.
 */
char *Str_ToLower(char *pString)
{
	char *str = pString;
	while (*str)
	{
		*str = tolower((unsigned char)*str);
		str++;
	}
	return pString;
}

/**
 * Allocate memory for a string and check for out-of memory (and exit the
 * program in that case, since there is likely nothing we can do if we even
 * can not allocate small strings anymore).
 *
 * @param len  Length of the string (without the trailing NUL character)
 */
char *Str_Alloc(int len)
{
	char *newstr = malloc(len + 1);

	if (!newstr)
	{
		perror("string allocation failed");
		exit(1);
	}

	newstr[0] = newstr[len] = 0;

	return newstr;
}

/**
 * This function is like strdup, but also checks for out-of memory and exits
 * the program in that case (there is likely nothing we can do if we even can
 * not allocate small strings anymore).
 */
char *Str_Dup(const char *str)
{
	char *newstr;

	if (!str)
		return NULL;

	newstr = strdup(str);
	if (!newstr)
	{
		perror("string duplication failed");
		exit(1);
	}

	return newstr;
}

/**
 * Copy string from pSrc to pDest, taking the destination buffer size
 * into account.
 * This function is similar to strscpy() in the Linux-kernel, it
 * is a replacement for strlcpy() (which cannot be used on untrusted
 * source strings since it tries to find out its length). Our
 * function here returns -E2BIG instead if the string does not
 * fit the destination buffer.
 */
long Str_Copy(char *pDest, const char *pSrc, long nBufLen)
{
	long nCount = 0;

	if (nBufLen == 0)
		return -E2BIG;

	while (nBufLen) {
		char c;

		c = pSrc[nCount];
		pDest[nCount] = c;
		if (!c)
			return nCount;
		nCount++;
		nBufLen--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (nCount > 0)
		pDest[nCount - 1] = '\0';

	return -E2BIG;
}

#if 0
/**
 * truncate string at first unprintable char (e.g. newline).
 */
char *Str_Trunc(char *pString)
{
	int i = 0;
	char *str = pString;
	while (str[i] != '\0')
	{
		if (!isprint((unsigned)str[i]))
		{
			str[i] = '\0';
			break;
		}
		i++;
	}
	return pString;
}
#endif

#if 0
/**
 * check if string is valid hex number.
 */
bool Str_IsHex(const char *str)
{
	int i = 0;
	while (str[i] != '\0' && str[i] != ' ')
	{
		if (!isxdigit((unsigned)str[i]))
			return false;
		i++;
	}
	return true;
}
#endif

/**
 * Search for a string (\0 terminated) into a memory region
 */
uint8_t *Str_FindInMem(const char *pString , uint8_t *Mem , int MemLen)
{
	uint8_t *p;
	int StringLen = strlen(pString);

	for ( p=Mem ; p <= Mem + MemLen - StringLen ; p++ )
	{
		if ( memcmp ( p , pString , StringLen ) == 0 )
			return p;		/* match */
	}
	return NULL;
}

/**
 * Convert "\e", "\n", "\t", "\\" backslash escapes in given string to
 * corresponding byte values, anything else as left as-is.
 */
void Str_UnEscape(char *s1)
{
	char *s2 = s1;
	for (; *s1; s1++)
	{
		if (*s1 != '\\')
		{
			*s2++ = *s1;
			continue;
		}
		s1++;
		switch(*s1)
		{
		case 'e':
			*s2++ = '\x1b';
			break;
		case 'n':
			*s2++ = '\n';
			break;
		case 't':
			*s2++ = '\t';
			break;
		case '\\':
			*s2++ = '\\';
			break;
		default:
			s1--;
			*s2++ = '\\';
		}
	}
	assert(s2 <= s1);
	*s2 = '\0';
}
