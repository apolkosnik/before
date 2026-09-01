/*
 *  ufs.c (former UFS.cpp)
 *  Previous
 *
 *  Created by Simon Schubiger on 03.03.19.
 *
 *  Rewritten in C by Andreas Grabher.
 */

#include <stdlib.h>
#include <string.h>

#include "ufs.h"


const uint32_t BLOCK_INVALID = ~0;

struct ufs_t* ufs_init(struct part_t* part) {
    struct ufs_t* ufs = NULL;
    
    if (part->part == NULL || strncmp(&part->part->p_type[1], "4.3BSD", 6) == 0) {
        uint8_t* sectors = (uint8_t*)malloc(SBSIZE);
        if (partition_readSectors(part, 8, SBSIZE / part->im->sectorSize, sectors) == 0) {
            struct ufs_super_block* superblock = (struct ufs_super_block*)sectors;
            if (ntohl(superblock->fs_magic) == FS_MAGIC &&
                ntohl(superblock->fs_bsize) == 0x2000 &&
                ntohl(superblock->fs_fsize) == part->im->sectorSize
                ) {
                ufs = (struct ufs_t*)malloc(sizeof(struct ufs_t));
                memcpy(&ufs->superBlock, superblock, sizeof(ufs->superBlock));
                ufs->fsBShift = ntohl(ufs->superBlock.fs_bshift);
                ufs->fsBMask  = ntohl(~ufs->superBlock.fs_bmask);
                ufs->fsBSize  = ntohl(ufs->superBlock.fs_bsize);
                ufs->fsFrag   = ntohl(ufs->superBlock.fs_frag);
                ufs->part     = part;
                
                for (int i = 0; i <= BCACHE_SIZE; i++) {
                    ufs->blockCache[i]   = (uint8_t*)malloc(part->im->sectorSize * ufs->fsFrag);
                    ufs->cacheBlockNo[i] = BLOCK_INVALID;
                }
            } else {
                printf("No valid file system found (%X).\n", (uint32_t)ntohl(superblock->fs_magic));
            }
        } else {
            printf("Can't read super block.\n");
        }
        free(sectors);
    } else {
        printf("Partition type %.*s not supported.\n", MAXFSTLEN - 1, &part->part->p_type[1]);
    }
    
    return ufs;
}

void ufs_uninit(struct ufs_t* ufs) {
    if (ufs) {
        for (int i = 0; i <= BCACHE_SIZE; i++)
            free(ufs->blockCache[i]);
        free(ufs);
    }
}

int ufs_readInode(struct ufs_t* ufs, struct icommon* inode, uint32_t ino) {
    int err;
    int idx;
    struct inb indsb;
    
    if (INOPB(&ufs->superBlock)) {
        err = partition_readSectors(ufs->part, itod(&ufs->superBlock, ino), ufs->fsFrag, (uint8_t*)&indsb);
    } else {
        err = ERR_FAIL;
    }
    if (err) {
        printf("Can't read inode %d.\n", ino);
        return err;
    }
    
    idx = itoo(&ufs->superBlock, ino);
    memcpy(inode, &indsb.ibs[idx], sizeof(struct icommon));
    return ERR_NO;
}

static int ufs_fillCacheWithBlock(struct ufs_t* ufs, uint32_t blockNum) {
    int cacheIndex = blockNum & BCACHE_MASK;
    if (ufs->cacheBlockNo[cacheIndex] != blockNum) {
        int err = partition_readSectors(ufs->part, blockNum, ufs->fsFrag, ufs->blockCache[cacheIndex]);
        if (err)
            return err;
        ufs->cacheBlockNo[cacheIndex] = blockNum;
    }
    return cacheIndex;
}

static int32_t ufs_bmap(struct ufs_t* ufs, struct icommon* inode, uint32_t fBlk) {
    uint32_t iPtrCnt = ufs->fsBSize >> 2;
    if (fBlk >= NDADDR) {
        int lvl1CacheIndex = -1;
        int lvl2CacheIndex = -1;
        fBlk -= NDADDR;
        if (fBlk >= iPtrCnt) {
            fBlk -= iPtrCnt;
            uint32_t lvl2Idx = fBlk & (ufs->fsBMask >> 2);
            uint32_t lvl1Idx = fBlk >> (ufs->fsBShift - 2);
            if ((lvl1CacheIndex = ufs_fillCacheWithBlock(ufs, ntohl(inode->ic_ib[1]))) < 0 ) {
                printf("error in lvl1 bmap(%d)\n", fBlk);
                return -1;
            }
            if ((lvl2CacheIndex = ufs_fillCacheWithBlock(ufs, ntohl(((struct idb*)ufs->blockCache[lvl1CacheIndex])->idbs[lvl1Idx]))) < 0)  {
                printf("error in lvl2 bmap(%d)\n", fBlk);
                return -1;
            }
            return ntohl(((struct idb*)ufs->blockCache[lvl2CacheIndex])->idbs[lvl2Idx]);
        } else {
            if ((lvl1CacheIndex = ufs_fillCacheWithBlock(ufs, ntohl(inode->ic_ib[0]))) < 0 ) {
                printf("error in lvl1 bmap(%d)\n", fBlk);
                return -1;
            }
            return ntohl(((struct idb*)ufs->blockCache[lvl1CacheIndex])->idbs[fBlk]);
        }
    }
    else return ntohl(inode->ic_db[fBlk]);
}

char* ufs_readlink(struct ufs_t* ufs, struct icommon* inode, int* needfree) {
    if (ntohl(inode->ic_flags) & IC_FASTLINK)
        return inode->ic_Mun.ic_Msymlink;
    else {
        uint32_t size = ntohl(inode->ic_size);
        uint8_t* buffer = (uint8_t*)malloc(size+1);
        *needfree = 1;
        ufs_readFile(ufs, inode, 0, size, buffer);
        buffer[size] = '\0';
        return (char*)buffer;
    }
}

static int ufs_readBlock(struct ufs_t* ufs, uint32_t dBlk) {
    if (!(ufs->cacheBlockNo[BCACHE_SIZE] == dBlk)) {
        int err = partition_readSectors(ufs->part, dBlk, ufs->fsFrag, ufs->blockCache[BCACHE_SIZE]);
        if (err)
            return err;
        ufs->cacheBlockNo[BCACHE_SIZE] = dBlk;
    }
    return ERR_NO;
}

int ufs_readFile(struct ufs_t* ufs, struct icommon* inode, uint32_t start, uint32_t len, uint8_t* data) {
    int err;
    int32_t  dBlk;
    uint32_t fBlk;
    uint32_t sOff;
    uint32_t tLen;
    
    if (start + len > ntohl(inode->ic_size))
        len = ntohl(inode->ic_size) - start;
    
    fBlk = start >> ufs->fsBShift;
    sOff = start & ufs->fsBMask;
    
    if ((dBlk = ufs_bmap(ufs, inode, fBlk)) < 0)
        return ERR_BMAP;
    
    if (sOff + len < ufs->fsBSize) {
        err = ufs_readBlock(ufs, dBlk);
        if (err)
            return err;
        memcpy(data, ufs->blockCache[BCACHE_SIZE] + sOff, len);
        return ERR_NO;
    } else {
        tLen = ufs->fsBSize - sOff;
        err = ufs_readBlock(ufs, dBlk);
        if (err)
            return err;
        memcpy(data, ufs->blockCache[BCACHE_SIZE] + sOff, tLen);
        data += tLen;
        len -= tLen;
        fBlk ++;
    }
    
    tLen = ufs->fsBSize;
    while (len >= tLen) {
        if ((dBlk = ufs_bmap(ufs, inode, fBlk)) < 0)
            return dBlk;
        err = partition_readSectors(ufs->part, dBlk, ufs->fsFrag, data);
        if (err)
            return err;
        data += tLen;
        len -= tLen;
        fBlk++;
    }
    
    if (len > 0) {
        if ((dBlk = ufs_bmap(ufs, inode, fBlk)) < 0)
            return dBlk;
        err = ufs_readBlock(ufs, dBlk);
        if (err)
            return err;
        memcpy(data, ufs->blockCache[BCACHE_SIZE], len);
    }
    
    return ERR_NO;
}

static void dirlist_add(struct dirlist_t** dirs, struct direct* direct, uint32_t maxlen) {
    uint16_t len = ntohs(direct->d_reclen);
    if (len > sizeof(struct direct)) {
        len = sizeof(struct direct);
    }
    if (len > maxlen) {
        len = maxlen;
    }
    
    while (*dirs) {
        dirs = &(*dirs)->next;
    }
    *dirs = (struct dirlist_t*)malloc(sizeof(struct dirlist_t));
    memcpy(&(*dirs)->dir, direct, len);
    (*dirs)->next = NULL;
}

void dirlist_delete(struct dirlist_t* dirs) {
    struct dirlist_t* next;
    
    while (dirs) {
        next = dirs->next;
        free(dirs);
        dirs = next;
    }
}

struct dirlist_t* ufs_list(struct ufs_t* ufs, uint32_t ino) {
    struct icommon inode;
    struct dirlist_t* result = NULL;
    
    if (ufs_readInode(ufs, &inode, ino) == 0) {
        if ((ntohs(inode.ic_mode) & IFMT) == IFDIR) {
            uint32_t size = ntohl(inode.ic_size);
            uint8_t* directory = (uint8_t*)malloc(size);
            if (ufs_readFile(ufs, &inode, 0, size, directory) == ERR_NO) {
                struct direct* dirEnt;
                for (uint32_t start = 0; start < size; start += ntohs(dirEnt->d_reclen)) {
                    dirEnt = (struct direct*)&directory[start];
                    if (ntohl(dirEnt->d_inonum) == 0 || ntohs(dirEnt->d_reclen) == 0)
                        break;
                    dirlist_add(&result, dirEnt, size - start);
                }
            }
            free(directory);
        }
    }
    return result;
}

char* ufs_mountPoint(struct ufs_t* ufs) {
    return (char*)ufs->superBlock.fs_u11.fs_u1.fs_fsmnt;
}

void ufs_print(struct ufs_t* ufs) {
    printf("    Mount point:   '%.*s'\n", UFS_MAXMNTLEN, ufs_mountPoint(ufs));
    printf("    Fragment size: %d Bytes\n", (int)ntohl(ufs->superBlock.fs_fsize));
    printf("    Block size:    %d Bytes\n\n", (int)ntohl(ufs->superBlock.fs_bsize));
}
