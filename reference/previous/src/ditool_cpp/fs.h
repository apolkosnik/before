#ifndef fs_h
#define fs_h

/*
 * Mach Operating System
 * Copyright (c) 1987 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */
/*
 * HISTORY
 * 2-Mar-2019 Simon Schubiger, adapted for Previous NeXT emulator
 *
 * 27-Sep-89  Morris Meyer (mmeyer) at NeXT
 *	NFS 4.0 Changes.
 *
 * 25-Jan-86  Avadis Tevanian (avie) at Carnegie-Mellon University
 *	Upgraded to 4.3.
 *
 * 03-Aug-85  Mike Accetta (mja) at Carnegie-Mellon University
 *	CS_RPAUSE:  Added freefrags() and freeinodes() macros and
 *	FS_FLOWAT, FS_FHIWAT, FS_ILOWAT, FS_IHIWAT, FS_FNOSPC and
 *	FS_INOSPC definitions.
 *
 */

#pragma pack(push, 1)
#define NeXT 1

/* This will pull in the __u8, __s8, __u16, ..., __s64 definitions on Linux and many Unices, although not part of POSIX */
#if HAVE_SYS_TYPES_H
  #include <sys/types.h>
#endif

#ifdef _WIN32
typedef uint8_t u_char;
#endif

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)fs.h	7.1 (Berkeley) 6/4/86
 */

/*	@(#)fs.h	2.1 88/05/20 4.0NFSSRC SMI;	from UCB 7.1 6/4/86	*/

/*
 * Each disk drive contains some number of file systems.
 * A file system consists of a number of cylinder groups.
 * Each cylinder group has inodes and data.
 *
 * A file system is described by its super-block, which in turn
 * describes the cylinder groups.  The super-block is critical
 * data and is replicated in each cylinder group to protect against
 * catastrophic loss.  This is done at mkfs time and the critical
 * super-block data does not change, so the copies need not be
 * referenced further unless disaster strikes.
 *
 * For file system fs, the offsets of the various blocks of interest
 * are given in the super block as:
 *	[fs->fs_sblkno]		Super-block
 *	[fs->fs_cblkno]		Cylinder group block
 *	[fs->fs_iblkno]		Inode blocks
 *	[fs->fs_dblkno]		Data blocks
 * The beginning of cylinder group cg in fs, is given by
 * the ``cgbase(fs, cg)'' macro.
 *
 * The first boot and super blocks are given in absolute disk addresses.
 */
#define MAXBSIZE    8192
#define BBSIZE		8192
#define SBSIZE		8192
#define MAXFRAG     8
#define	BBLOCK		((int32_t)(0))
#if	NeXT
/*
 *  SBLOCK gives the address in bytes.  It's up to code to convert to
 *  device blocks based on the blocksize of the device...
 */
#define	SBLOCK		((int32_t)(BBLOCK + BBSIZE))
#else	// NeXT
#define	SBLOCK		((int32_t)(BBLOCK + BBSIZE) / DEV_BSIZE)
#endif	// NeXT

/*
 * Addresses stored in inodes are capable of addressing fragments
 * of `blocks'. File system blocks of at most size MAXBSIZE can 
 * be optionally broken into 2, 4, or 8 pieces, each of which is
 * addressible; these pieces may be DEV_BSIZE, or some multiple of
 * a DEV_BSIZE unit.
 *
 * Large files consist of exclusively large data blocks.  To avoid
 * undue wasted disk space, the last data block of a small file may be
 * allocated as only as many fragments of a large block as are
 * necessary.  The file system format retains only a single pointer
 * to such a fragment, which is a piece of a single large block that
 * has been divided.  The size of such a fragment is determinable from
 * information in the inode, using the ``blksize(fs, ip, lbn)'' macro.
 *
 * The file system records space availability at the fragment level;
 * to determine block availability, aligned fragments are examined.
 *
 * The root inode is the root of the file system.
 * Inode 0 can't be used for normal purposes and
 * historically bad blocks were linked to inode 1,
 * thus the root inode is 2. (inode 1 is no int32_ter used for
 * this purpose, however numerous dump tapes make this
 * assumption, so we are stuck with it)
 * The lost+found directory is given the next available
 * inode when it is created by ``mkfs''.
 */
#define	ROOTINO		((ino_t)2)	/* i number of all roots */
#define LOSTFOUNDINO	(ROOTINO + 1)

/*
 * Cylinder group related limits.
 *
 * For each cylinder we keep track of the availability of blocks at different
 * rotational positions, so that we can lay out the data to be picked
 * up with minimum rotational latency.  NRPOS is the number of rotational
 * positions which we distinguish.  With NRPOS 8 the resolution of our
 * summary information is 2ms for a typical 3600 rpm drive.
 */
#define	NRPOS		8	/* number distinct rotational positions */

/*
 * MAXIPG bounds the number of inodes per cylinder group, and
 * is needed only to keep the structure simpler by having the
 * only a single variable size element (the free bit map).
 *
 * N.B.: MAXIPG must be a multiple of INOPB(fs).
 */
#define	MAXIPG		2048	/* max number inodes/cyl group */

/*
 * MINBSIZE is the smallest allowable block size.
 * In order to insure that it is possible to create files of size
 * 2^32 with only two levels of indirection, MINBSIZE is set to 4096.
 * MINBSIZE must be big enough to hold a cylinder group block,
 * thus changes to (struct cg) must keep its size within MINBSIZE.
 * MAXCPG is limited only to dimension an array in (struct cg);
 * it can be made larger as int32_t as that structures size remains
 * within the bounds dictated by MINBSIZE.
 * Note that super blocks are always of size SBSIZE,
 * and that both SBSIZE and MAXBSIZE must be >= MINBSIZE.
 */
#define MINBSIZE	4096
#define	MAXCPG		32	/* maximum fs_cpg */

/*
 * The path name on which the file system is mounted is maintained
 * in fs_fsmnt. MAXMNTLEN defines the amount of space allocated in 
 * the super block for this name.
 * The limit on the amount of summary information per file system
 * is defined by MAXCSBUFS. It is currently parameterized for a
 * maximum of two million cylinders.
 */
#define MAXMNTLEN 512
#define MAXCSBUFS 32

typedef uint64_t  __fs64;
typedef uint32_t  __fs32;
typedef uint16_t  __fs16;
typedef uint8_t   __fs8;

/* Without the guard, GCC 10 will generate an error for conflicting definitions on 64-bit architectures  */
#if !defined(linux) && !defined(__GNUC__)
typedef uint64_t  __u64;
typedef int64_t  __s64;
#endif

typedef uint32_t  __u32;
typedef uint16_t  __u16;
typedef uint8_t   __u8;

typedef int32_t  __s32;
typedef int16_t  __s16;
typedef int8_t   __s8;

struct ufs_timeval {
    __fs32    tv_sec;
    __fs32    tv_usec;
};

struct ufs_csum {
    __fs32    cs_ndir;    /* number of directories */
    __fs32    cs_nbfree;    /* number of free blocks */
    __fs32    cs_nifree;    /* number of free inodes */
    __fs32    cs_nffree;    /* number of free frags */
};

struct ufs2_csum_total {
    __fs64    cs_ndir;    /* number of directories */
    __fs64    cs_nbfree;    /* number of free blocks */
    __fs64    cs_nifree;    /* number of free inodes */
    __fs64    cs_nffree;    /* number of free frags */
    __fs64   cs_numclusters;    /* number of free clusters */
    __fs64   cs_spare[3];    /* future expansion */
};

#define UFS_MAXNAMLEN  255
#define UFS_MAXMNTLEN  512
#define UFS2_MAXMNTLEN 468
#define UFS2_MAXVOLLEN 32
#define UFS_MAXCSBUFS  31
#define UFS_LINK_MAX   32000
#define UFS2_NOCSPTRS  28

#define    FS_MAGIC    0x011954

struct ufs_super_block {
    union {
        struct {
            __fs32    fs_link;    /* UNUSED */
        } fs_42;
        struct {
            __fs32    fs_state;    /* file system state flag */
        } fs_sun;
    } fs_u0;
    __fs32    fs_rlink;    /* UNUSED */
    __fs32    fs_sblkno;    /* addr of super-block in filesys */
    __fs32    fs_cblkno;    /* offset of cyl-block in filesys */
    __fs32    fs_iblkno;    /* offset of inode-blocks in filesys */
    __fs32    fs_dblkno;    /* offset of first data after cg */
    __fs32    fs_cgoffset;    /* cylinder group offset in cylinder */
    __fs32    fs_cgmask;    /* used to calc mod fs_ntrak */
    __fs32    fs_time;    /* last time written -- time_t */
    __fs32    fs_size;    /* number of blocks in fs */
    __fs32    fs_dsize;    /* number of data blocks in fs */
    __fs32    fs_ncg;        /* number of cylinder groups */
    __fs32    fs_bsize;    /* size of basic blocks in fs */
    __fs32    fs_fsize;    /* size of frag blocks in fs */
    __fs32    fs_frag;    /* number of frags in a block in fs */
    /* these are configuration parameters */
    __fs32    fs_minfree;    /* minimum percentage of free blocks */
    __fs32    fs_rotdelay;    /* num of ms for optimal next block */
    __fs32    fs_rps;        /* disk revolutions per second */
    /* these fields can be computed from the others */
    __fs32    fs_bmask;    /* ``blkoff'' calc of blk offsets */
    __fs32    fs_fmask;    /* ``fragoff'' calc of frag offsets */
    __fs32    fs_bshift;    /* ``lblkno'' calc of logical blkno */
    __fs32    fs_fshift;    /* ``numfrags'' calc number of frags */
    /* these are configuration parameters */
    __fs32    fs_maxcontig;    /* max number of contiguous blks */
    __fs32    fs_maxbpg;    /* max number of blks per cyl group */
    /* these fields can be computed from the others */
    __fs32    fs_fragshift;    /* block to frag shift */
    __fs32    fs_fsbtodb;    /* fsbtodb and dbtofsb shift constant */
    __fs32    fs_sbsize;    /* actual size of super block */
    __fs32    fs_csmask;    /* csum block offset */
    __fs32    fs_csshift;    /* csum block number */
    __fs32    fs_nindir;    /* value of NINDIR */
    __fs32    fs_inopb;    /* value of INOPB */
    __fs32    fs_nspf;    /* value of NSPF */
    /* yet another configuration parameter */
    __fs32    fs_optim;    /* optimization preference, see below */
    /* these fields are derived from the hardware */
    union {
        struct {
            __fs32    fs_npsect;    /* # sectors/track including spares */
        } fs_sun;
        struct {
            __fs32    fs_state;    /* file system state time stamp */
        } fs_sunx86;
    } fs_u1;
    __fs32    fs_interleave;    /* hardware sector interleave */
    __fs32    fs_trackskew;    /* sector 0 skew, per track */
    /* a unique id for this filesystem (currently unused and unmaintained) */
    /* In 4.3 Tahoe this space is used by fs_headswitch and fs_trkseek */
    /* Neither of those fields is used in the Tahoe code right now but */
    /* there could be problems if they are.                            */
    __fs32    fs_id[2];    /* file system id */
    /* sizes determined by number of cylinder groups and their sizes */
    __fs32    fs_csaddr;    /* blk addr of cyl grp summary area */
    __fs32    fs_cssize;    /* size of cyl grp summary area */
    __fs32    fs_cgsize;    /* cylinder group size */
    /* these fields are derived from the hardware */
    __fs32    fs_ntrak;    /* tracks per cylinder */
    __fs32    fs_nsect;    /* sectors per track */
    __fs32    fs_spc;        /* sectors per cylinder */
    /* this comes from the disk driver partitioning */
    __fs32    fs_ncyl;    /* cylinders in file system */
    /* these fields can be computed from the others */
    __fs32    fs_cpg;        /* cylinders per group */
    __fs32    fs_ipg;        /* inodes per cylinder group */
    __fs32    fs_fpg;        /* blocks per group * fs_frag */
    /* this data must be re-computed after crashes */
    struct ufs_csum fs_cstotal;    /* cylinder summary information */
    /* these fields are cleared at mount time */
    __s8    fs_fmod;    /* super block modified flag */
    __s8    fs_clean;    /* file system is clean flag */
    __s8    fs_ronly;    /* mounted read-only flag */
    __s8    fs_flags;
    union {
        struct {
            __s8    fs_fsmnt[UFS_MAXMNTLEN];/* name mounted on */
            __fs32    fs_cgrotor;    /* last cg searched */
            __fs32    fs_csp[UFS_MAXCSBUFS];/*list of fs_cs info buffers */
            __fs32    fs_maxcluster;
            __fs32    fs_cpc;        /* cyl per cycle in postbl */
            __fs16    fs_opostbl[16][8]; /* old rotation block list head */
        } fs_u1;
        struct {
            __s8  fs_fsmnt[UFS2_MAXMNTLEN];    /* name mounted on */
            __u8   fs_volname[UFS2_MAXVOLLEN]; /* volume name */
            __fs64  fs_swuid;        /* system-wide uid */
            __fs32  fs_pad;    /* due to alignment of fs_swuid */
            __fs32   fs_cgrotor;     /* last cg searched */
            __fs32   fs_ocsp[UFS2_NOCSPTRS]; /*list of fs_cs info buffers */
            __fs32   fs_contigdirs;/*# of contiguously allocated dirs */
            __fs32   fs_csp;    /* cg summary info buffer for fs_cs */
            __fs32   fs_maxcluster;
            __fs32   fs_active;/* used by snapshots to track fs */
            __fs32   fs_old_cpc;    /* cyl per cycle in postbl */
            __fs32   fs_maxbsize;/*maximum blocking factor permitted */
            __fs64   fs_sparecon64[17];/*old rotation block list head */
            __fs64   fs_sblockloc; /* byte offset of standard superblock */
            struct  ufs2_csum_total fs_cstotal;/*cylinder summary information*/
            struct  ufs_timeval    fs_time;        /* last time written */
            __fs64    fs_size;        /* number of blocks in fs */
            __fs64    fs_dsize;    /* number of data blocks in fs */
            __fs64   fs_csaddr;    /* blk addr of cyl grp summary area */
            __fs64    fs_pendingblocks;/* blocks in process of being freed */
            __fs32    fs_pendinginodes;/*inodes in process of being freed */
        } fs_u2;
    }  fs_u11;
    union {
        struct {
            __fs32    fs_sparecon[53];/* reserved for future constants */
            __fs32    fs_reclaim;
            __fs32    fs_sparecon2[1];
            __fs32    fs_state;    /* file system state time stamp */
            __fs32    fs_qbmask[2];    /* ~usb_bmask */
            __fs32    fs_qfmask[2];    /* ~usb_fmask */
        } fs_sun;
        struct {
            __fs32    fs_sparecon[53];/* reserved for future constants */
            __fs32    fs_reclaim;
            __fs32    fs_sparecon2[1];
            __fs32    fs_npsect;    /* # sectors/track including spares */
            __fs32    fs_qbmask[2];    /* ~usb_bmask */
            __fs32    fs_qfmask[2];    /* ~usb_fmask */
        } fs_sunx86;
        struct {
            __fs32    fs_sparecon[50];/* reserved for future constants */
            __fs32    fs_contigsumsize;/* size of cluster summary array */
            __fs32    fs_maxsymlinklen;/* max length of an internal symlink */
            __fs32    fs_inodefmt;    /* format of on-disk inodes */
            __fs32    fs_maxfilesize[2];    /* max representable file size */
            __fs32    fs_qbmask[2];    /* ~usb_bmask */
            __fs32    fs_qfmask[2];    /* ~usb_fmask */
            __fs32    fs_state;    /* file system state time stamp */
        } fs_44;
    } fs_u2;
    __fs32    fs_postblformat;    /* format of positional layout tables */
    __fs32    fs_nrpos;        /* number of rotational positions */
    __fs32    fs_postbloff;        /* (__s16) rotation block list head */
    __fs32    fs_rotbloff;        /* (__u8) blocks for each rotation */
    __fs32    fs_magic;        /* magic number */
    __u8      fs_space[1];        /* list of blocks for each rotation */
};
/*
 * Per cylinder group information; summarized in blocks allocated
 * from first cylinder group data blocks.  These blocks have to be
 * read in from fs_csaddr (size fs_cssize) in addition to the
 * super block.
 *
 * N.B. sizeof(struct csum) must be a power of two in order for
 * the ``fs_cs'' macro to work (see below).
 */
struct csum {
	__fs32	cs_ndir;	/* number of directories */
	__fs32	cs_nbfree;	/* number of free blocks */
	__fs32	cs_nifree;	/* number of free inodes */
	__fs32	cs_nffree;	/* number of free frags */
};

#define    INOPB(fs)    (fsv((fs)->fs_inopb))

#define blkstofrags(fs, blks)    /* calculates (blks * fs->fs_frag) */ \
((blks) << fsv((fs)->fs_fragshift))

#define    cgbase(fs, c)    ((int32_t)(fsv((fs)->fs_fpg) * (c)))

#define cgstart(fs, c) \
(cgbase(fs, c) + fsv((fs)->fs_cgoffset) * ((c) & ~(fsv((fs)->fs_cgmask))))

#define    cgimin(fs, c)    (cgstart(fs, c) + fsv((fs)->fs_iblkno))    /* inode blk */

#define    itoo(fs, x)    ((x) % INOPB(fs))
#define    itog(fs, x)    ((x) / fsv((fs)->fs_ipg))
#define    itod(fs, x) \
((int32_t)(cgimin(fs, itog(fs, x)) + \
(blkstofrags((fs), (((x) % fsv((fs)->fs_ipg)) / INOPB(fs))))))

/*
 * Preference for optimization.
 */
#define FS_OPTTIME	0	/* minimize allocation time */
#define FS_OPTSPACE	1	/* minimize disk fragmentation */

#ifdef	NeXT
/*
 *	File system states.
 *
 *	A cleanly unmounted FS goes to clean state if it was in dirty
 *	state.  Mounting a clean FS makes it dirty.  Mounting a dirty FS
 *	makes it corrupted.  fsck (or equivalent) will change a dirty or
 *	corrupted FS to clean state.  NOTE: a FS that has never had this
 *	state set (state == 0) will be treated as corrupted.
 */

#define FS_STATE_CLEAN		1	/* cleanly unmounted */
#define FS_STATE_DIRTY		2	/* dirty */
#define FS_STATE_CORRUPTED	3	/* mounted while dirty */
#endif	// NeXT

/* As noted in cpu/sysdeps.h, "if a char has more than 8 bits, good night" */
#ifndef NBBY
    #define NBBY 8
#endif

/*
 * Cylinder group block for a file system.
 */
#define	CG_MAGIC	0x090255
struct	cg {
	struct	cg *cg_link;		/* linked list of cyl groups */
	struct	cg *cg_rlink;		/*     used for incore cyl groups */
	time_t	cg_time;		/* time last written */
	int32_t	cg_cgx;			/* we are the cgx'th cylinder group */
	int16_t	cg_ncyl;		/* number of cyl's this cg */
	int16_t	cg_niblk;		/* number of inode blocks this cg */
	int32_t	cg_ndblk;		/* number of data blocks this cg */
	struct	csum cg_cs;		/* cylinder summary information */
	int32_t	cg_rotor;		/* position of last used block */
	int32_t	cg_frotor;		/* position of last used frag */
	int32_t	cg_irotor;		/* position of last used inode */
	int32_t	cg_frsum[MAXFRAG];	/* counts of available frags */
	int32_t	cg_btot[MAXCPG];	/* block totals per cylinder */
	int16_t	cg_b[MAXCPG][NRPOS];	/* positions of free blocks */
	char	cg_iused[MAXIPG/NBBY];	/* used inode map */
	int32_t	cg_magic;		/* magic number */
	u_char	cg_free[1];		/* free block map */
/* actually int32_ter */
};

#if	NeXT
/*
 * This macro controls whether the file system format is byte swapped or not.
 * At NeXT, all little endian machines read and write big endian file systems.
 */
#define	BIG_ENDIAN_FS	(__LITTLE_ENDIAN__)
#endif	// NeXT

#pragma pack(pop)

#endif
