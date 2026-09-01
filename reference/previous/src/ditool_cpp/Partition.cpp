//
//  Partition.cpp
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#include <string.h>

#include "Partition.h"
#include "fs.h"
#include "UFS.h"

using namespace std;

static disk_partition DUMMY = {};

Partition::Partition(void) : partNo(-1), partIdx(-1), im(NULL), dl(NULL), part(DUMMY) {}

Partition::Partition(int partNo, size_t partIdx, DiskImage* im, const disk_label* dl, disk_partition& part)
: partNo(partNo)
, partIdx(partIdx)
, im(im)
, dl(dl)
, part(part) {}

Partition::~Partition(void) {}

bool Partition::isUFS(void) const {
    if(strncmp(&part.p_type[1], "4.3BSD", 6))
        return false;
    
    uint8_t sectors[8*im->sectorSize];
    if(readSectors(8, 8, sectors))
        return false;
    
    struct ufs_super_block* fs = (struct ufs_super_block*)sectors;
    return fsv(fs->fs_magic) == FS_MAGIC;
}

int Partition::readSectors(uint32_t sector, uint32_t count, uint8_t* dst) const {
    streamsize size;
    ios_base::iostate result;
    const struct  disktab& dt = im->dl.dl_dt;
    int64_t       usable      = fsv(dt.d_ag_size) - fsv(dt.d_ag_alts);

    sector += fsv(part.p_base);

    int64_t limit(0);
    int64_t offset(0);
    do {
        if (usable) {
            offset = sector % usable;
            if (offset >= fsv(dt.d_ag_off))
                offset += fsv(dt.d_ag_alts);
            if (offset < fsv(dt.d_ag_off))
                limit = fsv(dt.d_ag_off) - offset;
            else
                limit = fsv(dt.d_ag_size) - offset + fsv(dt.d_ag_off);
            limit = count > limit ? limit : count;
            offset += sector / usable * fsv(dt.d_ag_size);
        } else {
            limit  = count;
            offset = sector;
        }
        offset += fsv(dt.d_front);
        
        offset *= im->sectorSize;
        size = limit * im->sectorSize;
        
        result = im->read(offset, size, dst);
        
        sector += limit;
        count  -= limit;
        dst    += size;
    } while((count > 0) && (result == ios_base::goodbit));

    if     (result == ios_base::goodbit) return ERR_NO;
    else if(result &  ios_base::eofbit)  return ERR_EOF;
    else                                 return ERR_FAIL;
}

ostream& operator<< (ostream& os, const Partition& part) {
    int64_t size = fsv(part.part.p_size);
    size *= part.im->sectorSize;
    size >>= 20;
    os << "Partition #" << part.partIdx << ": " << &part.part.p_type[1] << " " << size << " MBytes";
    if(part.isUFS())
        os << UFS(part);
    return os;
}
