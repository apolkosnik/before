#ifndef _DITOOL_H_
#define _DITOOL_H_

#include <string>
#include <vector>
#include <ostream>
#include "VirtualFS.h"

class NetbootTask {
protected:
    NetbootTask(const std::string& info) : info(info) {}
public:
    virtual bool run(VirtualFS& vfs) = 0;
private:
    std::string info;
    
    friend std::ostream& operator<<(std::ostream& os, const NetbootTask& task);
};

class NBTLink : public NetbootTask {
public:
    NBTLink(const VFSPath& from, const VFSPath& to) : NetbootTask("Linking " + from.string() + " -> " + to.string()), from(from), to(to) {}
    virtual bool run(VirtualFS& vfs);
private:
    VFSPath from;
    VFSPath to;
};

class NetbootTaskFile : public NetbootTask {
protected:
    NetbootTaskFile(const std::string& info, const VFSPath& file) : NetbootTask(info + file.string()), file(file) {}
public:
    virtual bool run(VirtualFS& vfs);
protected:
    virtual bool preRun(VirtualFS& vfs)  {return true;}
    virtual bool run(std::ofstream& out) = 0;
    VFSPath file;
};

class NBTResolveConf : public NetbootTaskFile {
public:
    NBTResolveConf(const VFSPath& resolvConf) : NetbootTaskFile("Writing ", resolvConf) {}
protected:
    virtual bool run(std::ofstream& out);
};

class NBTHosts : public NetbootTaskFile {
public:
    NBTHosts(const VFSPath& hosts) : NetbootTaskFile("Patching ", hosts) {}
protected:
    virtual bool preRun(VirtualFS& vfs);
    virtual bool run(std::ofstream& out);
private:
    void addHost(std::ofstream& out, uint32_t addr, const std::string& hostNames);
    std::vector<std::string> lines;
};

class NBTHostConfig : public NetbootTaskFile {
public:
    NBTHostConfig(const VFSPath& hostconfig) : NetbootTaskFile("Patching ", hostconfig) {}
protected:
    virtual bool preRun(VirtualFS& vfs);
    virtual bool run(std::ofstream& out);
private:
    void addParameter(std::ofstream& out, const std::string& param, const std::string& val);
    std::vector<std::string> lines;
};

class NBTFstab : public NetbootTaskFile {
public:
    NBTFstab(const VFSPath& fstab) : NetbootTaskFile("Writing ", fstab) {}
protected:
    virtual bool run(std::ofstream& out);
};

#endif

