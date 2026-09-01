//
//  FileTableNFSD.cpp
//  Previous
//
//  Created by Simon Schubiger on 04.03.19.
//

#include "FileTableNFSD.h"
#include "compat.h"
#include <unistd.h>

using namespace std;

FileTableNFSD::FileTableNFSD(const HostPath& basePath, const VFSPath& basePathAlias)
: VirtualFS(basePath, basePathAlias)
, mutex(host_mutex_create()) {}

FileTableNFSD::~FileTableNFSD(void) {
    host_mutex_destroy(mutex);
}

bool FileTableNFSD::getCanonicalPath(uint64_t fhandle, std::string& result) {
    NFSDLock lock(mutex);
    map<uint64_t, string>::iterator iter(handle2path.find(fhandle));
    if(iter != handle2path.end()) {
        result = iter->second;
        return true;
    }
    return false;
}

int FileTableNFSD::stat(const VFSPath& absoluteVFSpath, struct stat& fstat) {
    NFSDLock lock(mutex);
    return VirtualFS::stat(absoluteVFSpath, fstat);
}

void FileTableNFSD::move(uint64_t fileHandleFrom, const VFSPath& absoluteVFSpathTo) {
    NFSDLock lock(mutex);
    handle2path.erase(fileHandleFrom);
    handle2path[VirtualFS::getFileHandle(absoluteVFSpathTo)] = absoluteVFSpathTo.canonicalize().string();
}

void FileTableNFSD::remove(uint64_t fileHandle) {
    NFSDLock lock(mutex);
    handle2path.erase(fileHandle);
}

uint64_t FileTableNFSD::getFileHandle(const VFSPath& absoluteVFSpath) {
    NFSDLock lock(mutex);
    uint64_t result(VirtualFS::getFileHandle(absoluteVFSpath));
    handle2path[result] = absoluteVFSpath.canonicalize().string();
    return result;
}

void FileTableNFSD::setFileAttrs(const VFSPath& absoluteVFSpath, const FileAttrs& fstat) {
    NFSDLock lock(mutex);
    VirtualFS::setFileAttrs(absoluteVFSpath, fstat);
}

FileAttrs FileTableNFSD::getFileAttrs(const VFSPath& absoluteVFSpath) {
    NFSDLock lock(mutex);
    return VirtualFS::getFileAttrs(absoluteVFSpath);
}
