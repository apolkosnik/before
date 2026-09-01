/*
 *  im.c (former DiskImage.cpp)
 *  Previous
 *
 *  Created by Simon Schubiger on 03.03.19.
 *
 *  Rewritten in C by Andreas Grabher.
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include "rs.h"
#include "im.h"
#include "part.h"
#include "ufs.h"


static bool label_valid(const char* v) {
    return strncmp(v, "dlV3", 4) == 0 || strncmp(v, "dlV2", 4) == 0 || strncmp(v, "NeXT", 4) == 0;
}

struct im_t* diskimage_init(const char* path) {
    struct im_t* im = (struct im_t*)malloc(sizeof(struct im_t));
    
    im->imf        = fopen(path, "rb");
    im->error      = NULL;
    im->parts      = NULL;
    im->rawOptical = false;
    im->diskOffset = 0;
    im->readSize   = SECTOR_SIZE_MIN;
    im->sectorSize = SECTOR_SIZE_MIN;
    im->path       = path;
    if (im->imf == NULL) {
        im->error = strerror(errno);
        return im;
    }
    memset(&im->dl, 0, sizeof(im->dl));
    
    for (int i = 0; i < NLABELS; i++) {
        if (diskimage_read(im, i * 15 * 512, sizeof(im->dl), &im->dl)) {
            im->error = "Reading disk label failed";
            return im;
        }
        if (label_valid(im->dl.dl_version)) {
            if (i) printf("Found spare disk label at offset %d.\n\n", i * 15 * 512);
            break;
        }
    }
    if (label_valid(im->dl.dl_version) == false) {
        im->diskOffset = DISK_OFFSET_DCII;
        
        if (diskimage_read(im, 0, sizeof(im->dl), &im->dl)) {
            im->error = "Reading disk label failed";
            return im;
        }
        if (label_valid(im->dl.dl_version) == false) {
            im->rawOptical = true;
            im->diskOffset = DISK_OFFSET_MO;
            im->readSize   = SECTOR_SIZE_ECC;
            
            memset(im->bb.bad_blk, 0, sizeof(im->bb.bad_blk));
            memset(im->bm, 0, sizeof(im->bm));
            im->bbt      = im->dl.dl_un.dl_bad;
            im->bbt_size = 0;
            im->spa      = 1;
            
            diskimage_read(im, 0, sizeof(im->dl), &im->dl);
            if (label_valid(im->dl.dl_version) == false) {
                struct ufs_t* ufs = NULL;
                printf("No valid disk label found.\n");
                memset(&im->dl, 0, sizeof(im->dl));
                im->rawOptical = false;
                im->diskOffset = 0;
                im->readSize   = SECTOR_SIZE_MIN;
                partition_init(0, im, NULL);
                do {
                    printf("Looking for partition data. Trying %"PRId64" Byte sectors.\n", im->sectorSize);
                    if ((ufs = ufs_init(im->parts))) {
                        printf("Found partition data of type 4.3BSD with %"PRId64" Byte sectors.\n\n", im->sectorSize);
                        ufs_uninit(ufs);
                        return im;
                    }
                    im->sectorSize <<= 1;
                    im->readSize = im->sectorSize;
                } while (im->sectorSize <= SECTOR_SIZE_MAX);
                
                im->error = "No valid file system";
                return im;
            } else {
                int bad = 0;
                
                printf("Magneto-optical disk detected\n");
                
                if (strncmp(im->dl.dl_version, "NeXT", 4)) {
                    im->spa = 1;
                } else {
                    im->spa = ntohl(im->dl.dl_dt.d_nsectors) >> 1;
                }
                if (im->spa < 1) {
                    printf("Bad number of sectors per alternate\n");
                    im->spa = 1;
                }
                
                im->apag = ntohs(im->dl.dl_dt.d_ag_alts) / im->spa;
                if (im->apag < 1) {
                    printf("Bad number of alternates per alternate group\n");
                    im->apag = 1;
                }
                
                if (strncmp(im->dl.dl_version, "dlV3", 4)) {
                    im->bbt      = im->dl.dl_un.dl_bad;
                    im->bbt_off  = sizeof(struct disk_label) - sizeof(dl_un_t) - sizeof(uint16_t); /* 558 */;
                    im->bbt_size = NBAD;
                } else {
                    im->bbt      = im->bb.bad_blk;
                    im->bbt_off  = BAD_BLK_OFF * SECTOR_SIZE_MO;
                    im->bbt_size = NBAD_BLK;
                    if (diskimage_read(im, im->bbt_off, im->bbt_size * sizeof(uint32_t), im->bbt)) {
                        im->error = "Reading bad block table failed";
                        return im;
                    }
                }
                
                im->bm_off  = 16 * SECTOR_SIZE_MO;
                im->bm_size = 16 * SECTOR_SIZE_MO;
                if (diskimage_read(im, im->bm_off, im->bm_size * sizeof(uint32_t), im->bm)) {
                    im->error = "Reading bitmap failed";
                    return im;
                }
                
                for (int i = 0; i < im->bbt_size; i++) {
                    if (ntohl(im->bbt[i]) == 0xffffffff) {
                        im->bbt_size = i;
                        break;
                    }
                    if (ntohl(im->bbt[i]) > 0) {
                        bad++;
                    }
                }
                if (bad > 0) {
                    bad *= im->spa;
                    printf("Disk has %d bad blocks\n", bad);
                    printf("Label: %.4s\n", im->dl.dl_version);
                    printf("%d entries in bad block table\n", im->bbt_size);
                    printf("%d alternate groups of %d sectors each\n", ntohs(im->dl.dl_dt.d_ngroups), ntohs(im->dl.dl_dt.d_ag_size));
                    printf("%d sectors per alternate group at offset %d\n", ntohs(im->dl.dl_dt.d_ag_alts), ntohs(im->dl.dl_dt.d_ag_off));
                    printf("%d alternates per alternate group with %d sectors per alternate\n", im->apag, im->spa);
                }
                printf("\n");
            }
        } else {
            printf("DiskCopyII image detected. Skipping header.\n\n");
        }
    }
    im->sectorSize = ntohl(im->dl.dl_dt.d_secsize);
    if (im->sectorSize < SECTOR_SIZE_MIN || 
        im->sectorSize > SECTOR_SIZE_MAX || 
        (im->rawOptical && im->sectorSize != SECTOR_SIZE_MO)) {
        printf("Sector size: %"PRId64"\n", im->sectorSize);
        im->error = "Unsupported sector size";
        return im;
    }
    if (!(im->rawOptical)) {
        im->readSize = im->sectorSize;
    }

    /* Add partitions */
    for (int p = 0; p < NPART; p++) {
        if (ntohs(im->dl.dl_dt.d_partitions[p].p_bsize) == 0 || ntohs(im->dl.dl_dt.d_partitions[p].p_bsize) == 0xffff)
            continue;
        partition_init(p, im, &im->dl.dl_dt.d_partitions[p]);
    }
    return im;
}

void diskimage_uninit(struct im_t* im) {
    if (im->imf) fclose(im->imf);
    partition_uninit(im->parts);
    free(im);
}

int diskimage_read(struct im_t* im, int64_t offset, int64_t size, void* data) {
    int result        = ERR_NO;
    int64_t readOff   = 0;
    int64_t readSize  = 0;
    int64_t sector    = offset / im->sectorSize;
    int64_t sectorOff = offset % im->sectorSize;
    uint8_t* dataPos  = (uint8_t*)data;
    uint8_t* buffer   = (uint8_t*)malloc(im->readSize);
    while (size > 0) {
        const char* errstr;
        readOff = sector * im->readSize + im->diskOffset;
        if (fseeko(im->imf, (off_t)readOff, SEEK_SET) < 0) {
            result = ERR_FAIL;
            errstr = strerror(errno);
        } else if (fread(buffer, 1, im->readSize, im->imf) != im->readSize) {
            result = ferror(im->imf) ? ERR_FAIL : ERR_EOF;
            errstr = (result == ERR_EOF) ? "End of file" : "Read error";
        }
        if (result != ERR_NO) {
            printf("Can't read %"PRId64" bytes at offset %"PRId64" (%s).\n", size, readOff, errstr);
            break;
        }
        
        if (im->rawOptical) {
            int bmIndex = (int)(sector / im->spa);
            int bmShift = (bmIndex & 0xF) << 1;
            int bmValue = (ntohl(im->bm[bmIndex>>4]) >> bmShift) & 3;
            switch (bmValue) {
                case BM_UNTESTED:
                case BM_WRITTEN:
                    if (rs_decode(buffer) >= 0)
                        break;
                    if (bmValue != BM_UNTESTED)
                        printf("Warning: sector %"PRId64" not decodable\n", sector);
                case BM_BAD:
                {
                    bool mappedBlock = false;
                    for (int bbtIndex = 0; bbtIndex < im->bbt_size; bbtIndex++) {
                        if (ntohl(im->bbt[bbtIndex]) == 0)
                            continue;
                        if (ntohl(im->bbt[bbtIndex]) == sector) {
                            int64_t reserve = bbtIndex / im->apag;
                            if (reserve < ntohs(im->dl.dl_dt.d_ngroups)) {
                                reserve *= ntohs(im->dl.dl_dt.d_ag_size);
                                reserve += ntohs(im->dl.dl_dt.d_ag_off) + (bbtIndex % im->apag) * im->spa;
                            } else {
                                reserve = ntohs(im->dl.dl_dt.d_ngroups) * ntohs(im->dl.dl_dt.d_ag_size);
                                reserve += (bbtIndex - ntohs(im->dl.dl_dt.d_ngroups) * im->apag) * im->spa;
                            }
                            reserve += sector % im->spa;
                            reserve += ntohs(im->dl.dl_dt.d_front);
                            
                            printf("Mapping bad sector %"PRId64" to %"PRId64"\n", sector, reserve);
                            result = diskimage_read(im, reserve * im->sectorSize, im->sectorSize, buffer);
                            if (result == ERR_NO) {
                                mappedBlock = true;
                            }
                            break;
                        }
                    }
                    if (mappedBlock)
                        break;
                    if (bmValue != BM_UNTESTED)
                        printf("Unable to re-map bad sector %"PRId64"\n", sector);
                }
                case BM_ERASED:
                    memset(buffer, 0, im->readSize);
                    break;
                    
                default:
                    break;
            }
            if (result != ERR_NO) {
                break;
            }
        }
        readSize = size < (im->sectorSize - sectorOff) ? size : (im->sectorSize - sectorOff);
        memcpy(dataPos, buffer + sectorOff, readSize);
        size     -= readSize;
        dataPos  += readSize;
        sectorOff = 0;
        sector++;
    }
    free(buffer);
    
    return result;
}

bool diskimage_valid(struct im_t* im) {
    return !(im->error && strlen(im->error));
}

void diskimage_print(struct im_t* im) {
    struct part_t* part = im->parts;
    if (im->dl.dl_version[0]) {
        uint64_t size = im->sectorSize;
        size *= ntohl(im->dl.dl_dt.d_ntracks);
        size *= ntohl(im->dl.dl_dt.d_nsectors);
        size *= ntohl(im->dl.dl_dt.d_ncylinders);
        size >>= 20;
        printf("Disk '%.*s' '%.*s' %"PRIu64" MBytes\n", MAXDNMLEN, im->dl.dl_dt.d_name, MAXTYPLEN, im->dl.dl_dt.d_type, size);
        printf("  Version:     '%.*s'\n", 4, im->dl.dl_version);
        printf("  Label:       '%.*s'\n", MAXLBLLEN, im->dl.dl_label);
        printf("  Sector size: %"PRId64" Bytes\n\n", im->sectorSize);
    }
    while (part) {
        partition_print(part);
        part = part->next;
    }
}
