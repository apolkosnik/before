//
//  DiskImage.cpp
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#include "rs.h"
#include "DiskImage.h"

#include <iostream>
#include <cstring>

/* Pull in ntohs()/ntohl()/htons()/htonl() declarations... shotgun approach */
#if defined(linux) || defined(__MINGW32__)
    /* netinet/in.h doesn't have proper extern "C" declarations for these... may also apply to other Unices */
    extern "C" uint32_t ntohl(uint32_t);
    extern "C" uint16_t ntohs(uint16_t);
    extern "C" uint32_t htonl(uint32_t);
    extern "C" uint16_t htons(uint16_t);
#else
    #if HAVE_ARPA_INET_H
        #include <arpa/inet.h>
    #endif
    #if HAVE_NETINET_IN_H
        #include <netinet/in.h>
    #endif
    #if HAVE_WINSOCK_H
        #include <winsock.h>
    #endif
#endif

using namespace std;

uint32_t fsv(uint32_t v) { return ntohl(v); }
uint16_t fsv(uint16_t v) { return ntohs(v); }

int16_t  fsv(int16_t v)  { return ntohs(v); }
int32_t  fsv(int32_t v)  { return ntohl(v); }

DiskImage::DiskImage(const string& path)
: imf(path, ios::binary | ios::in)
, diskOffset(0)
, blockSize(BLOCKSZ)
, rawOptical(false)
, sectorSize(0)
, path(path) {
    if(!(imf)) {
        error = strerror(errno);
        return;
    }
    
    read(0, sizeof(dl), &dl);
    if(
       strncmp(dl.dl_version, "NeXT", 4) &&
       strncmp(dl.dl_version, "dlV2", 4) &&
       strncmp(dl.dl_version, "dlV3", 4)
       ) {
        diskOffset = MO_BLOCK0;
        blockSize  = MO_BLOCKSZ;
        rawOptical = true;
        
        memset(bbt, 0, sizeof(bbt));
        memset(bm, 0, sizeof(bm));
        bbt_size = 0;
        spa      = 1;
        
        read(0, sizeof(dl), &dl);
        if(
           strncmp(dl.dl_version, "NeXT", 4) &&
           strncmp(dl.dl_version, "dlV2", 4) &&
           strncmp(dl.dl_version, "dlV3", 4)
           ) {
            cout << "Unknown version: " << dl.dl_version << endl;
            exit(1);
        }
        cout << "Magneto-optical disk detected" << endl;
        
        if (strncmp(dl.dl_version, "NeXT", 4)) {
            spa = 1;
        } else {
            spa = fsv(dl.dl_dt.d_nsectors) >> 1;
        }
        if (spa < 1) {
            cout << "Bad number of sectors per alternate" << endl;
            spa = 1;
        }

        apag = fsv(dl.dl_dt.d_ag_alts) / spa;
        if (apag < 1) {
            cout << "Bad number of alternates per alternate group" << endl;
            apag = 1;
        }

        if (strncmp(dl.dl_version, "dlV3", 4)) {
            bbt_off  = 558;
            bbt_size = 1670;
        } else {
            bbt_off  = 4*BLOCKSZ;
            bbt_size = 3*BLOCKSZ;
        }
        read(bbt_off, bbt_size * sizeof(uint32_t), bbt);
        
        bm_off  = 16*BLOCKSZ;
        bm_size = 16*BLOCKSZ;
        read(bm_off, bm_size * sizeof(uint32_t), bm);
        
        int bad = 0;
        for (int i = 0; i < bbt_size; i++) {
            if (bbt[i] == 0xFFFFFFFF) {
                bbt_size = i;
                break;
            }
            if (bbt[i] > 0) {
                bad++;
            }
        }
        if (bad > 0) {
            bad *= spa;
            cout << "Disk has " << bad << " bad blocks" << endl;
            cout << "Label: " << dl.dl_version << endl;
            cout << bbt_size << " entries in bad block table" << endl;
            cout << fsv(dl.dl_dt.d_ngroups) << " alternate groups of " << fsv(dl.dl_dt.d_ag_size) << " sectors each" << endl;
            cout << fsv(dl.dl_dt.d_ag_alts) << " sectors per alternate group at offset " << fsv(dl.dl_dt.d_ag_off) << endl;
            cout << apag << " alternates per alternate group with " << spa << " sectors per alternate" << endl;
        }
    }
    sectorSize = fsv(dl.dl_dt.d_secsize);
    if(sectorSize != 0x400) {
        cout << "Unsupported sector size: " << sectorSize;
        exit(1);
    }
    for(int p = 0; p < NPART; p++) {
        if(fsv(dl.dl_dt.d_partitions[p].p_bsize) == 0 || fsv(dl.dl_dt.d_partitions[p].p_bsize) == ~0)
            continue;
        parts.push_back(Partition(p, parts.size(), this, &dl, dl.dl_dt.d_partitions[p]));
    }
}

ios_base::iostate DiskImage::read(streampos offset, streamsize size, void* data) {
    int64_t block     = offset / BLOCKSZ;
    int64_t blockOff  = offset % BLOCKSZ;
    ios_base::iostate result(ios_base::goodbit);
    char*   dataPtr   = (char*)data;
    char    buffer[blockSize];
    while(size > 0) {
        int64_t rdSize = std::min((int64_t)size, BLOCKSZ - blockOff);
        imf.seekg(block * blockSize + diskOffset, ios::beg);
        imf.read(buffer, blockSize);
        if(rawOptical) {
            size_t bmIndex = block / spa;
            int    bmShift = (bmIndex & 0xF) << 1;
            int    bmValue = (fsv(bm[bmIndex>>4]) >> bmShift) & 3;
            switch(bmValue) {
                case BM_UNTESTED:
                case BM_WRITTEN:
                    if(rs_decode((uint8_t*)buffer) >= 0)
                        break;
                    if(bmValue != BM_UNTESTED)
                        cout << "Warning: block " << block << " not decodable" << endl;
                case BM_BAD:
                {
                    bool mappedBlock(false);
                    for(size_t bbtIndex = 0; bbtIndex < bbt_size; bbtIndex++) {
                        if(fsv(bbt[bbtIndex]) == 0)
                            continue;
                        if(fsv(bbt[bbtIndex]) == block) {
                            uint32_t reserve = bbtIndex / apag;
                            if(reserve < fsv(dl.dl_dt.d_ngroups)) {
                                reserve *= fsv(dl.dl_dt.d_ag_size);
                                reserve += fsv(dl.dl_dt.d_ag_off) + (bbtIndex % apag) * spa;
                            } else {
                                reserve = fsv(dl.dl_dt.d_ngroups) * fsv(dl.dl_dt.d_ag_size);
                                reserve += (bbtIndex - fsv(dl.dl_dt.d_ngroups) * apag) * spa;
                            }
                            reserve += block % spa;
                            reserve += fsv(dl.dl_dt.d_front);
                            
                            cout << "Mapping bad block " << block << " to " << reserve << endl;
                            read(reserve * BLOCKSZ, BLOCKSZ, buffer);
                            mappedBlock = true;
                            break;
                        }
                    }
                    if(mappedBlock)
                        break;
                    if(bmValue != BM_UNTESTED)
                        cout << "Unable to re-map bad block " << block << endl;
                }
                case BM_ERASED:
                    memset(buffer, 0, blockSize);
                    break;
                    
                default:
                    break;
            }
        }
        memcpy(dataPtr, buffer + blockOff, rdSize);
        blockOff = 0;
        size    -= rdSize;
        dataPtr += rdSize;
        block++;
        ios_base::iostate result = imf.rdstate();
        if(result != ios_base::goodbit) {
            cout << "Can't read " << size << " bytes at offset " << offset << endl;
            break;
        }
    }
    
    return result;
}

DiskImage::~DiskImage() {}

bool DiskImage::valid() {
    return error.empty();
}

ostream& operator<< (ostream& os, const DiskImage& im) {
    int64_t size = im.sectorSize;
    size *= fsv(im.dl.dl_dt.d_ntracks);
    size *= fsv(im.dl.dl_dt.d_nsectors);
    size *= fsv(im.dl.dl_dt.d_ncylinders);
    size >>= 20;
    os << "Disk '" << im.dl.dl_dt.d_name << "' '" << im.dl.dl_label << "' '" << im.dl.dl_dt.d_type << "' " << size << " MBytes" << endl;
    os << "  Sector size: " << im.sectorSize << " Bytes" << endl << endl;
    for(size_t p = 0; p < im.parts.size(); p++)
        os << "  " << im.parts[p] << endl;
    return os;
}
