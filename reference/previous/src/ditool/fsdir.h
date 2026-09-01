#ifndef fsdir_h
#define fsdir_h

/*
 * Mach Operating System
 * Copyright (c) 1987 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 **********************************************************************
 * HISTORY
 *  2-Mar-2019 Simon Schubiger, adapted for Previous NeXT emulator
 *  7-Jan-93  Mac Gillon (mgillon) at NeXT
 *	Integrated POSIX changes
 * 24-Jan-89  Peter King (king) at NeXT
 *	NFS 4.0 Changes.  Cleaned out old directory cruft.
 *
 **********************************************************************
 */

/* 
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 * @(#) from SUN 2.6; from UCB 4.5 82/11/13
 */

/*
 * A directory consists of some number of blocks of DIRBLKSIZ
 * bytes, where DIRBLKSIZ is chosen such that it can be transferred
 * to disk in a single atomic operation (e.g. 512 bytes on most machines).
 *
 * Each DIRBLKSIZ byte block contains some number of directory entry
 * structures, which are of variable length.  Each directory entry has
 * a struct direct at the front of it, containing its inode number,
 * the length of the entry, and the length of the name contained in
 * the entry.  These are followed by the name padded to a 4 byte boundary
 * with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 *
 * The macro DIRSIZ(dp) gives the amount of space required to represent
 * a directory entry.  Free space in a directory is represented by
 * entries which have dp->d_reclen > DIRSIZ(dp).  All DIRBLKSIZ bytes
 * in a directory block are claimed by the directory entries.  This
 * usually results in the last entry in a directory having a large
 * dp->d_reclen.  When entries are deleted from a directory, the
 * space is returned to the previous entry in the same directory
 * block by increasing its dp->d_reclen.  If the first entry of
 * a directory block is free, then its dp->d_ino is set to 0.
 * Entries other than the first in a directory do not normally have
 * dp->d_ino set to 0.
 */

#include <stdint.h>

#pragma pack(push, 1)
#define NeXT 1

#define DIRBLKSIZ	1024		/* a convenient number */
#ifdef MAXNAMLEN
#undef MAXNAMLEN
#endif /* MAXNAMLEN */
#define	MAXNAMLEN	255

struct	direct {
	uint32_t	d_inonum;		/* inode number of entry */
	uint16_t	d_reclen;		/* length of this record */
	uint16_t	d_namlen;		/* length of string in d_name */
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
};

/*
 * The DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 4 byte boundary.
 */
#undef DIRSIZ
#define DIRSIZ(dp) \
    ((sizeof (struct direct) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3))

#ifdef KERNEL
/*
 * Template for manipulating directories.
 * Should use struct direct's, but the name field
 * is MAXNAMLEN - 1, and this just won't do.
 */
struct dirtemplate {
	uint32_t	dot_ino;
	uint16_t	dot_reclen;
	uint16_t	dot_namlen;
	char	dot_name[4];		/* must be multiple of 4 */
	uint32_t	dotdot_ino;
	uint16_t	dotdot_reclen;
	uint16_t	dotdot_namlen;
	char	dotdot_name[4];		/* ditto */
};
#endif

#pragma pack(pop)

#endif
