//
//  FileTableNFSD.hpp
//  Previous
//
//  Created by Simon Schubiger on 04.03.19.
//

#ifndef FileTableNFSD_hpp
#define FileTableNFSD_hpp

#include "../../ditool/VirtualFS.h"
#include "XDRStream.h"
#include "host.h"

class FileTableNFSD : public VirtualFS {
    mutex_t*                        mutex;
    std::map<uint64_t, std::string> handle2path;
public:
    FileTableNFSD(const HostPath& basePath, const VFSPath& basePathAlias);
    virtual ~FileTableNFSD(void);
    
    virtual int         stat            (const VFSPath& absoluteVFSpath, struct stat& stat);
    virtual void        move            (uint64_t fileHandleFrom, const VFSPath& absoluteVFSpathTo);
    virtual void        remove          (uint64_t fileHandle);
    virtual uint64_t    getFileHandle   (const VFSPath& absoluteVFSpath);
    virtual void        setFileAttrs    (const VFSPath& absoluteVFSpath, const FileAttrs& fstat);
    virtual FileAttrs   getFileAttrs    (const VFSPath& absoluteVFSpath);
    
    bool                getCanonicalPath(uint64_t handle, std::string& result);
};

#endif /* FileTableNFSD_hpp */
