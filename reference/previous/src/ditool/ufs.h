/*
 *  ufs.h (former UFS.hpp)
 *  Previous
 *
 *  Created by Simon Schubiger on 03.03.19.
 *
 *  Rewritten in C by Andreas Grabher.
 */

#ifndef UFS_hpp
#define UFS_hpp

#include <stdint.h>

#include "part.h"
#include "fs.h"
#include "inode.h"
#include "fsdir.h"

#define BCACHE_SHIFT  8 /* block cache size as power-of-two */
#define BCACHE_SIZE   (1 << BCACHE_SHIFT)
#define BCACHE_MASK   (BCACHE_SIZE-1)

struct ufs_t {
    struct part_t* part;
    uint32_t       fsBShift;
    uint32_t       fsBMask;
    uint32_t       fsBSize;
    uint32_t       fsFrag;
    uint8_t*       blockCache[BCACHE_SIZE+1];
    uint32_t       cacheBlockNo[BCACHE_SIZE+1];

    struct ufs_super_block  superBlock;
};


struct dirlist_t {
    struct direct dir;
    struct dirlist_t* next;
};

void dirlist_delete(struct dirlist_t* dirs);


struct ufs_t* ufs_init(struct part_t* part);
void ufs_uninit(struct ufs_t* ufs);
int ufs_readInode(struct ufs_t* ufs, struct icommon* inode, uint32_t ino);
char* ufs_readlink(struct ufs_t* ufs, struct icommon* inode, int* needfree);
int ufs_readFile(struct ufs_t* ufs, struct icommon* inode, uint32_t start, uint32_t len, uint8_t* data);
struct dirlist_t* ufs_list(struct ufs_t* ufs, uint32_t ino);
char* ufs_mountPoint(struct ufs_t* ufs);
void ufs_print(struct ufs_t* ufs);

#endif /* UFS_hpp */
