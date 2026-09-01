//
//  Partition.hpp
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#ifndef Partition_hpp
#define Partition_hpp

#include <ostream>

#include "DiskImage.h"

enum Error {
    ERR_NO   = 0,
    ERR_EOF  = -1,
    ERR_FAIL = -2,
    ERR_BMAP = -2,
};

class DiskImage;

class Partition {
public:
    int                      partNo;
    size_t                   partIdx;
    DiskImage*               im;
    const struct disk_label* dl;
    struct disk_partition&   part;
    
    Partition(void);
    Partition(int partNo, size_t partIdx, DiskImage* im, const struct disk_label* dl, struct disk_partition& part);
    ~Partition(void);
    
    int  readSectors(uint32_t sector, uint32_t count, uint8_t* dst) const;
    bool isUFS(void) const;
};

std::ostream& operator<< (std::ostream& stream, const Partition& part);

#endif /* Partition_hpp */
