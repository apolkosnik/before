/*
 *  im.h (former DiskImage.hpp)
 *  Previous
 *
 *  Created by Simon Schubiger on 03.03.19.
 *
 *  Rewritten in C by Andreas Grabher.
 *
 *  Portion of it Copyright (c) 1982, 1986 Regents of the University of California
 */

#ifndef DiskImage_h
#define DiskImage_h

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef _WIN32
#include <Winsock2.h>
#endif


#pragma pack(push, 1)

struct disk_partition {
    int32_t    p_base;            /* base sector# of partition */
    int32_t    p_size;            /* #sectors in partition */
    int16_t    p_bsize;           /* block size in bytes */
    int16_t    p_fsize;           /* frag size in bytes */
    uint16_t   p_opt;             /* 's'pace/'t'ime optimization pref */
    int16_t    p_cpg;             /* cylinders per group */
    int16_t    p_density;         /* bytes per inode density */
    uint8_t    p_minfree;         /* minfree (%) */
    uint8_t    p_newfs;           /* run newfs during init */
#define    MAXMPTLEN    16
    char       p_mountpt[MAXMPTLEN]; /* mount point */
#define    MAXFSTLEN    10
    char       p_type[MAXFSTLEN]; /* file system type p_type[0] is automount flag */
};

struct    disktab {
#define    MAXDNMLEN    24
    char       d_name[MAXDNMLEN];      /* drive name */
#define    MAXTYPLEN    24
    uint8_t    d_type[MAXTYPLEN];      /* drive type */
    int32_t    d_secsize;              /* sector size in bytes */
    int32_t    d_ntracks;              /* # tracks/cylinder */
    int32_t    d_nsectors;             /* # sectors/track */
    int32_t    d_ncylinders;           /* # cylinders */
    int32_t    d_rpm;                  /* revolutions/minute */
    int16_t    d_front;                /* size of front porch (sectors) */
    int16_t    d_back;                 /* size of back porch (sectors) */
    int16_t    d_ngroups;              /* number of alt groups */
    int16_t    d_ag_size;              /* alt group size (sectors) */
    int16_t    d_ag_alts;              /* alternate sectors / alt group */
    int16_t    d_ag_off;               /* sector offset to first alternate */
#define    NBOOTS    2
    int32_t        d_boot0_blkno[NBOOTS];    /* "blk 0" boot locations */
#define    MAXBFLEN 24
    char    d_bootfile[MAXBFLEN];     /* default bootfile */
#define    MAXHNLEN 32
    char    d_hostname[MAXHNLEN];     /* host name */
    char    d_rootpartition;          /* root partition e.g. 'a' */
    char    d_rwpartition;            /* r/w partition e.g. 'b' */
#define    NPART    8
    struct  disk_partition d_partitions[NPART];
};

#define    NLABELS        4           /* # of labels on a disk */

typedef union {
    uint16_t dl_v3_checksum;
#define    NBAD     1670              /* sized to make label ~= 8KB */
    int32_t  dl_bad[NBAD];            /* block number that is bad */
} dl_un_t;

struct disk_label {
    char                    dl_version[4];            /* label version number */
    int32_t                 dl_label_blkno;           /* block # where this label is */
    int32_t                 dl_size;                  /* size of media area (sectors) */
#define    MAXLBLLEN    24
    char                    dl_label[MAXLBLLEN];      /* media label */
    uint32_t                dl_flags;                 /* flags */
#define    DL_UNINIT    0x80000000                    /* label is uninitialized */
    uint32_t                dl_tag;                   /* volume tag */
    struct    disktab       dl_dt;                    /* common info in disktab */
    dl_un_t                 dl_un;
    uint16_t                dl_checksum;              /* ones complement checksum */
};

#define    BAD_BLK_OFF    4           /* offset of bad blk tbl from label */
struct bad_block {                    /* bad block table, sized to be 12KB */
#define        NBAD_BLK    (12 * 1024 / sizeof (int32_t))
    int32_t    bad_blk[NBAD_BLK];
};

#pragma pack(pop)


#define SECTOR_SIZE_MO   1024
#define SECTOR_SIZE_ECC  1296
#define SECTOR_SIZE_MIN  SECTOR_SIZE_MO
#define SECTOR_SIZE_MAX  8192

#define DISK_OFFSET_DCII 46
#define DISK_OFFSET_MO   (53*16*SECTOR_SIZE_ECC)

#define BM_UNTESTED 0
#define BM_BAD      1
#define BM_WRITTEN  2
#define BM_ERASED   3

struct im_t {
    FILE*             imf;
    size_t            diskOffset;
    size_t            readSize;
    bool              rawOptical;

    struct disk_label dl;
    struct bad_block  bb;
    uint32_t          bm[16*SECTOR_SIZE_MO];
    int               bm_off;
    int               bm_size;
    int32_t*          bbt;
    int               bbt_off;
    int               bbt_size;
    int32_t           spa;
    int16_t           apag;
    struct part_t*    parts;
    int64_t           sectorSize;
    const char*       path;
    const char*       error;
};

struct im_t* diskimage_init(const char* path);
void diskimage_uninit(struct im_t* img);
bool diskimage_valid(struct im_t* img);
int diskimage_read(struct im_t* img, int64_t offset, int64_t size, void* data);
void diskimage_print(struct im_t* img);

#endif /* DiskImage_hpp */
