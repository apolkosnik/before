//
//  UFS.cpp
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#include <iostream>
#include <cstring>
#include "UFS.h"
#include "fsdir.h"
#include "VirtualFS.h"

using namespace std;

const uint32_t BLOCK_INVALID = ~0;

UFS::UFS(const Partition& part) : part(part) {
    for(int i = 0; i <= BCACHE_SIZE; i++) {
        blockCache[i]   = NULL;
        cacheBlockNo[i] = BLOCK_INVALID;
    }
    
    if(strncmp(&part.part.p_type[1], "4.3BSD", 6) != 0)
        return;
    
    uint8_t sectors[8*part.im->sectorSize];
    if(part.readSectors(8, 8, sectors))
        return;
    memcpy(&superBlock, sectors, sizeof(superBlock));
    if(fsv(superBlock.fs_magic) != FS_MAGIC)
        return;
    if(fsv(superBlock.fs_bsize) != 0x2000)
        return;
    if(fsv(superBlock.fs_fsize) != 0x400)
        return;
    fsBShift = fsv(superBlock.fs_bshift);
    fsBMask  = fsv(~superBlock.fs_bmask);
    fsBSize  = fsv(superBlock.fs_bsize);
    fsFrag   = fsv(superBlock.fs_frag);
    
    for(int i = 0; i <= BCACHE_SIZE; i++) {
        blockCache[i]   = new uint8_t[part.im->sectorSize * fsFrag];
        cacheBlockNo[i] = BLOCK_INVALID;
    }
}

UFS::~UFS(void) {
    for(int i = 0; i <= BCACHE_SIZE; i++)
        delete[] blockCache[i];
}

int UFS::readInode(icommon& inode, uint32_t ino) {
    struct inb indsb;
    
    if(!(INOPB(&superBlock)))
        return ERR_FAIL;
    
    if(int err = part.readSectors(itod(&superBlock, ino), fsFrag, (uint8_t*)&indsb)) {
        cout << "Can't read inode " << ino << endl;
        return err;
    }
    
    int idx = itoo(&superBlock, ino);
    inode = indsb.ibs[idx];
    return ERR_NO;
}

int UFS::fillCacheWithBlock(uint32_t blockNum) {
    int cacheIndex = blockNum & BCACHE_MASK;
    if(cacheBlockNo[cacheIndex] != blockNum) {
        if(int err = part.readSectors(blockNum, fsFrag, blockCache[cacheIndex]))
            return err;
        cacheBlockNo[cacheIndex] = blockNum;
    }
    return cacheIndex;
}

int32_t UFS::bmap(const icommon& inode, uint32_t fBlk) {
    uint32_t iPtrCnt = fsBSize >> 2;
    if(fBlk >= NDADDR) {
        int lvl1CacheIndex = -1;
        int lvl2CacheIndex = -1;
        fBlk -= NDADDR;
        if(fBlk >= iPtrCnt) {
            fBlk -= iPtrCnt;
            uint32_t lvl2Idx = fBlk & (fsBMask >> 2);
            uint32_t lvl1Idx = fBlk >> (fsBShift - 2);
            if((lvl1CacheIndex = fillCacheWithBlock(fsv(inode.ic_ib[1]))) < 0 ) {
                cout << "error in lvl1 bmap(" << fBlk << ")" << endl;
                return -1;
            }
            if((lvl2CacheIndex = fillCacheWithBlock(fsv(((idb*)blockCache[lvl1CacheIndex])->idbs[lvl1Idx]))) < 0)  {
                cout << "error in lvl2 bmap(" << fBlk << ")" << endl;
                return -1;
            }
            return fsv(((idb*)blockCache[lvl2CacheIndex])->idbs[lvl2Idx]);
        } else {
            if((lvl1CacheIndex = fillCacheWithBlock(fsv(inode.ic_ib[0]))) < 0 ) {
                cout << "error in lvl1 bmap(" << fBlk << ")" << endl;
                return(-1);
            }
            return fsv(((idb*)blockCache[lvl1CacheIndex])->idbs[fBlk]);
        }
    }
    else return fsv(inode.ic_db[fBlk]);
}

string UFS::readlink(const icommon& inode) {
    if(inode.ic_Mun.ic_Msymlink[0])
        return inode.ic_Mun.ic_Msymlink;
    else {
        size_t size = fsv(inode.ic_size);
        char   buffer[size+1];
        readFile(inode, 0, static_cast<uint32_t>(size), reinterpret_cast<uint8_t*>(buffer));
        buffer[size] = '\0';
        return buffer;
    }
}

bool UFS::findInode(icommon& inode, const string& path, bool followSymlink) {
    vector<string> segments = PathCommon::split("/", path);
    uint32_t ino(ROOTINO);
    for(int seg = 0; seg < segments.size(); seg++) {
        vector<direct> files = list(ino);
        if(files.empty()) return false;
        for(size_t file = 0; file < files.size(); file++) {
            if(files[file].d_name == segments[seg]) {
                ino = fsv(files[file].d_inonum);
                readInode(inode, ino);
                if((fsv(inode.ic_mode) & IFMT) == IFLNK) {
                    string link = readlink(inode);
                    if(followSymlink) {
                        vector<string> linkSegs = PathCommon::split("/", link);
                        if(link[0] != '/') {
                            segments.erase(segments.begin() + seg);
                            segments.insert(segments.begin() + seg, linkSegs.begin(), linkSegs.end());
                        } else {
                            linkSegs.insert(linkSegs.end(), segments.begin() + seg, segments.end());
                            segments = linkSegs;
                        }
                        seg = -1;
                        ino = ROOTINO;
                    }
                    else
                        return seg == segments.size() - 1;
                }
                break;
            }
        }
    }
    return true;
}

uint32_t UFS::fileSize(const icommon& inode) {
    return fsv(inode.ic_size);
}

int UFS::readFile(const icommon& inode, uint32_t start, uint32_t len, uint8_t* data) {
    uint32_t fBlk;
    int32_t  dBlk;
    uint32_t sOff;
    uint32_t tLen;
    
    if(start + len > fsv(inode.ic_size))
        len = fsv(inode.ic_size) - start;
    
    fBlk = start >> fsBShift;
    sOff = start & fsBMask;
    
    if((dBlk = bmap(inode, fBlk)) < 0)
        return ERR_BMAP;
    
    if(sOff + len < fsBSize) {
        if(int err = readBlock(dBlk))
            return err;
        memcpy(data, blockCache[BCACHE_SIZE] + sOff, len);
        return ERR_NO;
    } else {
        tLen = fsBSize - sOff;
        if(int err = readBlock(dBlk))
            return err;
        memcpy(data, blockCache[BCACHE_SIZE] + sOff, tLen);
        data += tLen;
        len -= tLen;
        fBlk ++;
    }
    
    tLen = fsBSize;
    while(len >= tLen) {
        if((dBlk = bmap(inode, fBlk)) < 0)
            return dBlk;
        if(int err = part.readSectors(dBlk, fsFrag, data))
            return err;
        data += tLen;
        len -= tLen;
        fBlk++;
    }
    
    if(len > 0) {
        if((dBlk = bmap(inode, fBlk)) < 0)
            return dBlk;
        if(int err = readBlock(dBlk))
            return err;
        memcpy(data, blockCache[BCACHE_SIZE], len);
    }
    
    return ERR_NO;
}

int UFS::readBlock(uint32_t dBlk) {
    if(!(cacheBlockNo[BCACHE_SIZE] == dBlk)) {
        if(int err = part.readSectors(dBlk, fsFrag, blockCache[BCACHE_SIZE]))
            return err;
        cacheBlockNo[BCACHE_SIZE] = dBlk;
    }
    return ERR_NO;
}

std::vector<direct> UFS::list(uint32_t ino) {
    vector<direct> result;
    icommon inode;
    
    if(readInode(inode, ino))
        return result;
    if((fsv(inode.ic_mode) & IFMT) != IFDIR)
        return result;

    uint32_t size = fsv(inode.ic_size);
    uint8_t  directory[size];
    if(readFile(inode, 0, size, directory))
        return result;
    
    struct direct dirEnt;
    for(uint32_t start = 0; start < size; start += fsv(dirEnt.d_reclen)) {
        dirEnt = *((direct*)&directory[start]);
        if(fsv(dirEnt.d_inonum) == 0 || fsv(dirEnt.d_reclen) == 0)
            break;
        result.push_back(dirEnt);
    }
    return result;
}

string UFS::mountPoint(void) const {
    return (char*)superBlock.fs_u11.fs_u1.fs_fsmnt;
}

ostream& operator<< (ostream& os, const UFS& ufs) {
    os << endl << "    Mount point:  '" << ufs.mountPoint() << "' ";
    os << endl << "    Fragment size: " << fsv(ufs.superBlock.fs_fsize) << " Bytes";
    os << endl << "    Block size:    " << fsv(ufs.superBlock.fs_bsize) << " Bytes";
    return os;
}
