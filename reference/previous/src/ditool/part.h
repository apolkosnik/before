/*
 *  part.h (former Partition.hpp)
 *  Previous
 *
 *  Created by Simon Schubiger on 03.03.19.
 *
 *  Rewritten in C by Andreas Grabher.
 */

#ifndef Partition_hpp
#define Partition_hpp

#include "im.h"

enum Error {
    ERR_NO   =  0,
    ERR_EOF  = -1,
    ERR_FAIL = -2,
    ERR_BMAP = -3
};

struct part_t {
    int                    number;
    char                   letter;
    struct im_t*           im;
    struct disk_partition* part;
    struct part_t*         next;
};

void partition_init(int part_num, struct im_t* im, struct disk_partition* partition);
void partition_uninit(struct part_t* part);
int  partition_readSectors(struct part_t* part, uint32_t sector, uint32_t count, uint8_t* dst);
void partition_print(struct part_t* part);

#endif /* Partition_hpp */
