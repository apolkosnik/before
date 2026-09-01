//
//  UFS.h
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#ifndef UFS_hpp
#define UFS_hpp

#include <fstream>
#include <stdint.h>

#include "Partition.h"
#include "fs.h"
#include "inode.h"
#include "fsdir.h"

const int BCACHE_SHIFT = 8; // block cache size as power-of-two
const int BCACHE_SIZE  = 1 << BCACHE_SHIFT;
const int BCACHE_MASK  = BCACHE_SIZE-1;

class UFS {
    const Partition& part;
    uint32_t         fsBShift;
    uint32_t         fsBMask;
    uint32_t         fsBSize;
    uint32_t         fsFrag;
    uint8_t*         blockCache[BCACHE_SIZE+1];
    uint32_t         cacheBlockNo[BCACHE_SIZE+1];
    
    int32_t          bmap(const icommon& inode, uint32_t fBlk);
    int              fillCacheWithBlock(uint32_t blkNo);
    int              readBlock(uint32_t dBlk);
public:
    ufs_super_block  superBlock;
    
    UFS(const Partition& part);
    ~UFS(void);
    
    std::string         mountPoint(void) const;
    int                 readInode(icommon& inode, uint32_t ino);
    uint32_t            fileSize(const icommon& inode);
    int                 readFile(const icommon& inode, uint32_t start, uint32_t len, uint8_t* dst);
    bool                findInode(icommon& inode, const std::string& path, bool followSymlink);
    std::string         readlink(const icommon& inode);
    std::vector<direct> list(uint32_t ino);
};

std::ostream& operator<< (std::ostream& os, const UFS& ufs);

#endif /* UFS_hpp */
