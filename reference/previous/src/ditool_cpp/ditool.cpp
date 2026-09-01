#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <cstring>
#include <algorithm>

#include <stdio.h>
#include <unistd.h>
#include <ftw.h>

#include "config.h"
#include "ditool.h"
#include "fsdir.h"
#include "UFS.h"
#include "VirtualFS.h"
#include "ctl.h"

#ifndef _WIN32

#if !HAVE_STRUCT_STAT_ST_ATIMESPEC
#define st_atimespec st_atim
#endif

#if !HAVE_STRUCT_STAT_ST_MTIMESPEC
#define st_mtimespec st_mtim
#endif

#endif

using namespace std;

static const char* get_option(const char** begin, const char** end, const string& option) {
    const char** itr = find(begin, end, option);
    if (itr != end && ++itr != end)
        return *itr;
    return NULL;
}

static bool has_option(const char** begin, const char** end, const string& option) {
    return find(begin, end, option) != end;
}

static void print_help(void) {
    cout << "usage : ditool -im <disk_image_file> [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -h          Print this help." << endl;
    cout << "  -im <file>  Raw disk image file to read from." << endl;
    cout << "  -lsp        List partitions in disk image." << endl;
    cout << "  -p          Partition number to work on." << endl;
    cout << "  -ls         List files in disk image." << endl;
    cout << "  -lst <type> List files in disk image of type. type=FILE|DIR|SLINK|HLINK|FIFO|CHAR|BLOCK|SOCK" << endl;
    cout << "  -out <path> Copy files from disk image to <path>." << endl;
    cout << "  -clean      Clean output directory before copying." << endl;
    cout << "  -netboot    Prepare files in output directory for netboot." << endl;
}

static bool ignore_name(const char* name) {
    return strcmp(".", name) == 0 || strcmp("..", name) == 0;
}

static string join(const string& path, const string& name) {
    string result(path);
    if(path != "/") result += "/";
    result += name;

    if(result == "/.")
        result = "/";
    return result;
}

static void copy_attrs(struct stat& fstat, icommon& inode, uint32_t rdev) {
    fstat.st_mode              = fsv(inode.ic_mode);
    fstat.st_uid               = fsv(inode.ic_uid);
    fstat.st_gid               = fsv(inode.ic_gid);
    fstat.st_size              = fsv(inode.ic_size);
#ifdef _WIN32
    fstat.st_atime  = fsv(inode.ic_atime.tv_sec);
    fstat.st_mtime  = fsv(inode.ic_mtime.tv_sec);
#else
    fstat.st_atimespec.tv_sec  = fsv(inode.ic_atime.tv_sec);
    fstat.st_atimespec.tv_nsec = fsv(inode.ic_atime.tv_usec) * 1000;
    fstat.st_mtimespec.tv_sec  = fsv(inode.ic_mtime.tv_sec);
    fstat.st_mtimespec.tv_nsec = fsv(inode.ic_mtime.tv_usec) * 1000;
#endif
    fstat.st_rdev              = rdev;
}

static void set_attrs_recr(UFS& ufs, set<string>& skip, uint32_t ino, const string& path, VirtualFS& ft) {
    vector<direct> entries = ufs.list(ino);
    
    for(size_t i = 0; i < entries.size(); i++) {
        direct& dirEnt = entries[i];
        icommon inode;
        ufs.readInode(inode, fsv(dirEnt.d_inonum));
        
        string dirEntPath(join(path, dirEnt.d_name));

        if(ignore_name(dirEnt.d_name)) continue;

        if(skip.find(dirEntPath) != skip.end()) continue;

        uint32_t rdev = 0;
        switch(fsv(inode.ic_mode) & IFMT) {
            case IFDIR:       /* directory */
                set_attrs_recr(ufs, skip, fsv(dirEnt.d_inonum), dirEntPath, ft);
                break;
            case IFCHR:       /* character special */
            case IFBLK:       /* block special */
                rdev = fsv(inode.ic_db[0]);
                break;

        }
    
        struct stat fstat;
        copy_attrs(fstat, inode, rdev);
        FileAttrs fattr(fstat);
        ft.setFileAttrs(dirEntPath, fattr);
        
        timeval times[2];
        times[0].tv_sec  = fattr.atime_sec;
        times[0].tv_usec = fattr.atime_usec;
        times[1].tv_sec  = fattr.mtime_sec;
        times[1].tv_usec = fattr.mtime_usec;
        
        if(ft.vfsChmod(dirEntPath, fstat.st_mode & ~IFMT))
            cout << "Unable to set mode for " << dirEntPath << endl;
        if(ft.vfsUtimes(dirEntPath, times))
            cout << "Unable to set times for " << dirEntPath << endl;
    }
}

static void set_attrs_inode(UFS& ufs, uint32_t ino, const string& path, VirtualFS& ft) {
    icommon inode;
    ufs.readInode(inode, ino);
    struct stat fstat;
    copy_attrs(fstat, inode, 0);
    FileAttrs fattr(fstat);
    ft.setFileAttrs(path, fattr);
    
    timeval times[2];
    times[0].tv_sec  = fattr.atime_sec;
    times[0].tv_usec = fattr.atime_usec;
    times[1].tv_sec  = fattr.mtime_sec;
    times[1].tv_usec = fattr.mtime_usec;
    
    if(ft.vfsChmod(path, fstat.st_mode & ~IFMT))
        cout << "Unable to set mode for " << path << endl;
    if(ft.vfsUtimes(path, times))
        cout << "Unable to set times for " << path << endl;
}

static void verify_attr_recr(UFS& ufs, set<string>& skip, uint32_t ino, const string& path, VirtualFS& ft) {
    vector<direct> entries = ufs.list(ino);
    
    for(size_t i = 0; i < entries.size(); i++) {
        direct& dirEnt = entries[i];
        icommon inode;
        ufs.readInode(inode, fsv(dirEnt.d_inonum));
        
        string dirEntPath(join(path, dirEnt.d_name));
        
        if(skip.find(dirEntPath) != skip.end()) continue;
        
        uint32_t rdev = 0;
        switch(fsv(inode.ic_mode) & IFMT) {
            case IFDIR:       /* directory */
                if(!(ignore_name(dirEnt.d_name)))
                    verify_attr_recr(ufs, skip, fsv(dirEnt.d_inonum), dirEntPath, ft);
                break;
            case IFCHR:       /* character special */
            case IFBLK:       /* block special */
                rdev = fsv(inode.ic_db[0]);
                break;
        }
        
        struct stat fstat;
        ft.stat(dirEntPath, fstat);
        if(fstat.st_mode != fsv(inode.ic_mode))
            cout << "mode mismatch (act/exp) " << std::oct << fstat.st_mode << " != " << std::oct << fsv(inode.ic_mode) << " " << dirEntPath << endl;
        if(fstat.st_uid != fsv(inode.ic_uid))
            cout << "uid mismatch (act/exp) " << fstat.st_uid << " != " << fsv(inode.ic_uid) << " " << dirEntPath << endl;
        if(fstat.st_gid != fsv(inode.ic_gid))
            cout << "gid mismatch (act/exp) " << fstat.st_gid << " != " << fsv(inode.ic_gid) << " " << dirEntPath << endl;
        if((fsv(inode.ic_mode) & IFMT) != IFDIR) {
            if(fstat.st_size != fsv(inode.ic_size))
                cout << "size mismatch (act/exp) " << fstat.st_size << " != " << fsv(inode.ic_size) << " " << dirEntPath << endl;
            /*
            if(fstat.st_atimespec.tv_sec != fsv(inode.ic_atime.tv_sec))
                cout << "atime_sec mismatch " << dirEntPath << " diff:" << (fstat.st_atimespec.tv_sec - fsv(inode.ic_atime.tv_sec)) << endl;
            if(fstat.st_atimespec.tv_nsec != fsv(inode.ic_atime.tv_usec) * 1000)
                cout << "atime_nsec mismatch " << dirEntPath << " diff:" << (fstat.st_atimespec.tv_nsec - (fsv(inode.ic_atime.tv_usec) * 1000)) << endl;
             */
#ifdef _WIN32
            if(fstat.st_mtime != fsv(inode.ic_mtime.tv_sec))
                cout << "mtime_sec mismatch diff: " << (fstat.st_mtime - fsv(inode.ic_mtime.tv_sec)) << " " << dirEntPath << endl;
#else
            if(fstat.st_mtimespec.tv_sec != fsv(inode.ic_mtime.tv_sec))
                cout << "mtime_sec mismatch diff: " << (fstat.st_mtimespec.tv_sec - fsv(inode.ic_mtime.tv_sec)) << " " << dirEntPath << endl;
            if(fstat.st_mtimespec.tv_nsec != fsv(inode.ic_mtime.tv_usec) * 1000)
                cout << "mtime_nsec mismatch diff: " << (fstat.st_mtimespec.tv_nsec - (fsv(inode.ic_mtime.tv_usec)) * 1000) << " " << dirEntPath << endl;
#endif
        }
        if((uint32_t)fstat.st_rdev != rdev)
            cout << "rdev mismatch (act/exp) " << fstat.st_rdev << " != " << rdev << " " << dirEntPath << endl;
    }
}

static void verify_inodes_recr(UFS& ufs, map<uint32_t, uint64_t>& inode2inode, set<string>& skip, uint32_t ino, const string& path, VirtualFS& ft) {
    vector<direct> entries = ufs.list(ino);
    
    for(size_t i = 0; i < entries.size(); i++) {
        direct& dirEnt = entries[i];
        icommon inode;
        ufs.readInode(inode, fsv(dirEnt.d_inonum));
        
        string dirEntPath(join(path, dirEnt.d_name));
        
        if(skip.find(dirEntPath) != skip.end()) continue;
        
        struct stat fstat;
        ft.stat(dirEntPath, fstat);
        
        if(inode2inode.find(dirEnt.d_inonum) == inode2inode.end()) {
            inode2inode[dirEnt.d_inonum] = fstat.st_ino;
        } else {
            if(inode2inode[dirEnt.d_inonum] != fstat.st_ino)
                cout << "inode mismatch (exp/act) " << inode2inode[dirEnt.d_inonum] << " != " << fstat.st_ino << " "  << dirEntPath << endl;
        }
            
        switch(fsv(inode.ic_mode) & IFMT) {
            case IFDIR:       /* directory */
                if(!(ignore_name(dirEnt.d_name)))
                    verify_inodes_recr(ufs, inode2inode, skip, fsv(dirEnt.d_inonum), dirEntPath, ft);
                break;
        }
    }
}

static bool do_print(const char* type, const char* listType, bool& doPrint, bool& force) {
    if(force) {
        force = false;
        return true;
    }
    if(listType) {
        doPrint = strstr(listType, type) != nullptr;
        return doPrint;
    }
    return true;
}

static void process_inodes_recr(UFS& ufs, map<uint32_t, string>& inode2path, set<string>& skip, uint32_t ino, const string& path, VirtualFS* ft, ostream& os, const char* listType) {
    vector<direct> entries = ufs.list(ino);
    for(size_t i = 0; i < entries.size(); i++) {
        direct& dirEnt = entries[i];
        icommon inode;
        ufs.readInode(inode, fsv(dirEnt.d_inonum));
        
        string dirEntPath(join(path, dirEnt.d_name));

        bool doPrint(true);
        bool forcePrint(false);
                                
        if(!(ignore_name(dirEnt.d_name)) || dirEntPath == "/") {
            if(ft && ft->vfsAccess(dirEntPath, F_OK) == 0) {
                struct stat fstat;
                ft->vfsStat(dirEntPath, fstat);
                if((fsv(inode.ic_mode) & IFMT) == IFLNK) {
                    string link = ufs.readlink(inode);
                    if(strcasecmp(link.c_str(), dirEnt.d_name) == 0) {
                        cout << "New file '" << dirEntPath << "' is link pointing to variant, skipping" << endl;
                        skip.insert(dirEntPath);
                        continue;
                    }
                }
#ifndef _WIN32
                if(S_ISLNK(fstat.st_mode)) {
                    VFSPath link;
                    ft->vfsReadlink(dirEntPath, link);
                    if(strcasecmp(link.c_str(), dirEnt.d_name) == 0) {
                        cout << "Existing file '" << dirEntPath << "' is link pointing to variant, removing link" << endl;
                        ft->vfsRemove(dirEntPath);
                        string tmp = path;
                        tmp += "/";
                        tmp += link.string();
                        skip.insert(tmp);
                    }
                } else
#endif
                if(dirEntPath != "/") {
                    cout << "WARNING: file '" << dirEntPath << "' (" << ft->toHostPath(dirEntPath) << ") already exists, skipping." << endl;
                    skip.insert(dirEntPath);
                    continue;
                }
            }
            if(inode2path.find(fsv(dirEnt.d_inonum)) != inode2path.end()) {
                if(do_print("HLINK", listType, doPrint, forcePrint)) os << "[HLINK] " << inode2path[fsv(dirEnt.d_inonum)] << " <- ";
                forcePrint = true;
                if(ft) ft->vfsLink(inode2path[fsv(dirEnt.d_inonum)], dirEntPath, false);
            } else
                inode2path[fsv(dirEnt.d_inonum)] = dirEntPath;
        }
        
        switch(fsv(inode.ic_mode) & IFMT) {
            case IFIFO:       /* named pipe (fifo) */
                if(do_print("FIFO", listType, doPrint, forcePrint)) os << "[FIFO]  ";
                if(ft) ft->touch(dirEntPath);
                break;
            case IFCHR:        /* character special */
                if(do_print("CHAR", listType, doPrint, forcePrint)) os << "[CHAR]  ";
                if(ft) ft->touch(dirEntPath);
                break;
            case IFDIR:       /* directory */
                if(do_print("DIR", listType, doPrint, forcePrint)) os << "[DIR]   ";
                if(!(ignore_name(dirEnt.d_name))) {
                    if(doPrint) os << dirEntPath << endl;
                    doPrint = false;
                    if(ft) ft->vfsMkdir(dirEntPath, DEFAULT_PERM);
                    process_inodes_recr(ufs, inode2path, skip, fsv(dirEnt.d_inonum), dirEntPath, ft, os, listType);
                }
                break;
            case IFBLK:       /* block special */
                if(do_print("BLOCK", listType, doPrint, forcePrint)) os << "[BLOCK] ";
                if(ft) ft->touch(dirEntPath);
                break;
            case IFREG:        /* regular */
                if(do_print("FILE", listType, doPrint, forcePrint)) os << "[FILE]  ";
                if(ft && ft->vfsAccess(dirEntPath, F_OK) != 0) {
                    VFSFile file(*ft, dirEntPath, "wb");
                    if(file.isOpen()) {
                        size_t size = fsv(inode.ic_size);
                        unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
                        ufs.readFile(inode, 0, static_cast<uint32_t>(size), buffer.get());
                        if(file.write(0, buffer.get(), size) != size) {
                            string errmsg("Error while writing '");
                            errmsg += dirEntPath;
                            errmsg += "'";
                            perror(errmsg.c_str());
                            exit(1);
                        }
                    }
                }
                break;
            case IFLNK: {       /* symbolic link */
                string link = ufs.readlink(inode);
                if(do_print("SLINK", listType, doPrint, forcePrint)) os << "[SLINK] " << link << " <- ";
                if(ft) ft->vfsLink(link, dirEntPath, true);
                break;
            }
            case IFSOCK:        /* socket */
                if(do_print("SOCK", listType, doPrint, forcePrint)) os << "[SOCK]  ";
                if(ft) ft->touch(dirEntPath);
                break;
            default:
                cout << "WARNING: unknown format (" << (fsv(inode.ic_mode) & IFMT) << ") '" << dirEntPath << "'" << endl;
                break;
        }
        

        if(doPrint)
            os << dirEntPath << endl;
    }
}

class DiToolFS : public VirtualFS {
public:
    DiToolFS(const HostPath& basePath, const VFSPath& basePathAlias) : VirtualFS(basePath, basePathAlias) {}
    virtual void move  (uint64_t fileHandleFrom, const VFSPath& absoluteVFSpathTo) {}
    virtual void remove(uint64_t fileHandle) {}
};

static void dump_part(DiskImage& im, int part, const HostPath& outPath, ostream& os, const char* listType) {
    VirtualFS* ft = NULL;
    UFS ufs(im.parts[part]);

    if(!(outPath.empty()))
        ft = new DiToolFS(outPath, ufs.mountPoint());
    
    map<uint32_t, string> inode2path;
    set<string>           skip;
    if(ft) {
        cout << "---- copying " << im.path << " partition " << part << " to " << ft->getBasePath() << endl;
        process_inodes_recr(ufs, inode2path, skip, ROOTINO, "", ft, os, listType);
        cout << "---- setting file attributes for NFSD" << endl;
        set_attrs_inode(ufs, ROOTINO, "", *ft);
        set_attrs_recr(ufs, skip, ROOTINO, "", *ft);
        cout << "---- verifying inode structure" << endl;
        map<uint32_t, uint64_t> inode2inode;
        verify_inodes_recr(ufs, inode2inode, skip, ROOTINO, "", *ft);
        cout << "---- verifying file attributes and sizes" << endl;
        verify_attr_recr(ufs, skip, ROOTINO, "", *ft);
        delete ft;
    } else {
        process_inodes_recr(ufs, inode2path, skip, ROOTINO, "", ft, os, listType);
    }
}

static bool is_mount(const HostPath& path) {
    struct stat sdir;  /* inode info */
    struct stat spdir; /* parent inode info */
    int res = ::stat(path.c_str(), &sdir);
    if (res < 0) return false;
    HostPath pdir(path / "..");
    res = ::stat(pdir.string().c_str(), &spdir);
    if (res < 0) return false;
    return    sdir.st_dev != spdir.st_dev  /* different devices */
           || sdir.st_ino == spdir.st_ino; /* root dir case */
}

static void clean_dir(const HostPath& path) {
    cout << "---- cleaning " << path << endl;
    ::nftw(path.c_str(), VirtualFS::remove, 1024, FTW_DEPTH | FTW_PHYS);
    if(is_mount(path)) {
        cout << "directory " << path << " is a mount point, using as-is" << endl;
    } else if(::access(path.c_str(), F_OK | R_OK | W_OK) == 0) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), ".%08X", rand());
        string newName = path.string();
        newName += tmp;
        cout << "directory " << path << " still exists. trying to rename it to " << newName << endl;
        rename(path.c_str(), newName.c_str());
    }
}

class NullBuffer : public streambuf {public: int overflow(int c) {return c;};};

static HostPath to_host_path(const char* path) {
    return path ? HostPath(path) : HostPath();
}

static bool is_case_insensitive(const HostPath& path) {
    string testFile(".nfsd__CASE__TEST__");
    fstream fs;
    fs.open((path / testFile).string(), ios::out);
    fs.close();
    transform(testFile.begin(), testFile.end(), testFile.begin(), ::tolower);
    return (path / testFile).exists();
}

extern "C" int main(int argc, const char * argv[]) {
    if(has_option(argv, argv+argc, "-h") || has_option(argv, argv+argc, "--help")) {
        print_help();
        return 0;
    }
    
    HostPath    imageFile = to_host_path(get_option(argv, argv + argc, "-im"));
    bool        listParts = has_option(argv, argv+argc,   "-lsp");
    const char* partNum   = get_option(argv, argv + argc, "-p");
    bool        listFiles = has_option(argv, argv + argc, "-ls");
    const char* listType  = get_option(argv, argv + argc, "-lst");
    HostPath    outPath   = to_host_path(get_option(argv, argv + argc, "-out"));
    bool        clean     = has_option(argv, argv + argc, "-clean");
    bool        netboot   = has_option(argv, argv + argc, "-netboot");

    if (!(imageFile).empty()) {
        DiskImage  im(imageFile.string());
        if(!(im.valid())) {
            cout << "Can't read '" << imageFile << "' (" << im.error <<")." << endl;
            return 1;
        }
        
        NullBuffer nullBuffer;
        ostream    nullStream(&nullBuffer);

        if(listType)
            listFiles = true;
        
        if(listParts)
            cout << im << endl;
        
        if(listFiles || !(outPath.empty())) {
            if(!(outPath.empty())) {
                if(is_case_insensitive(outPath)) {
                    cout << "WARNING: " << outPath << " is on a case insensitive file system." << endl;
                    cout << "         NeXTstep requires a case sensitive file system to run properly." << endl;
                    cout << "         Use i.e. OSX DiskUtility to create a disk image with a" << endl;
                    cout << "         case sensitive file system for your NFS directory." << endl;
                }
                if(clean) clean_dir(outPath);
#ifdef _WIN32
                ::mkdir(outPath.c_str());
#else
                ::mkdir(outPath.c_str(), DEFAULT_PERM);
#endif
                if(::access(outPath.c_str(), F_OK | R_OK | W_OK) < 0) {
                    cout << "Can't access '" << outPath << "'" << endl;
                    return 1;
                }
            }
            
            int part = partNum ? atoi(partNum) : -1;
            if(part >= 0 && part < static_cast<int>(im.parts.size()) && im.parts[part].isUFS()) {
                dump_part(im, part, outPath, listFiles ? cout : nullStream, listType);
            } else {
                for(int part = 0; part < (int)im.parts.size(); part++)
                    dump_part(im, part, outPath, listFiles ? cout : nullStream, listType);
            }
        }
    } else if(!(netboot)) {
        cout << "Missing image file." << endl;
        print_help();
        return 1;
    }
    
    DiToolFS vfs(outPath, "/");
    
    NetbootTask* tasks[] = {
        new NBTLink("../../sdmach", "/private/tftpboot/mach"),
        new NBTResolveConf("/private/etc/resolv.conf"),
        new NBTHosts("/private/etc/hosts"),
        new NBTHostConfig("/private/etc/hostconfig"),
        new NBTFstab("/private/etc/fstab"),
        nullptr
    };
    
    if(netboot) {
        if(outPath.exists()) {
            cout << "---- Preparing for netboot." << endl;

            for(NetbootTask** task = tasks; *task; task++) {
                cout << **task << ((*task)->run(vfs) ? " [OK]" : " [FAIL]") << endl;
            }
        } else {
            cout << "Missing output path." << endl;
            print_help();
            return 1;
        }
    }
    
    cout << "---- done." << endl;
    return 0;
}

// netboot tasks

bool NBTLink::run(VirtualFS& vfs) {
    vfs.vfsRemove(to);
    return vfs.vfsLink(from, to, true) == 0;
}

static string ip_addr_str(uint32_t addr) {
    stringstream ss;
    ss << ((addr >> 24) & 0xFF) << "." << ((addr >> 16) & 0xFF) << "." << ((addr >> 8) & 0xFF) << "." << (addr & 0xFF);
    return ss.str();
}

bool NetbootTaskFile::run(VirtualFS& vfs) {
    try {
        if(!(preRun(vfs)))
            return false;
        
        ofstream out(vfs.toHostPath(file).string());
        if(!(out)) return false;
        
        return run(out);
    } catch(exception& e) {
        cout << e.what() << endl;
        return false;
    }
}

bool NBTResolveConf::run(std::ofstream& out) {
    out << "domain " << (NAME_DOMAIN[0] == '.' ? &NAME_DOMAIN[1]  : &NAME_DOMAIN[0]) << '\n';
    out << "nameserver " << ip_addr_str(CTL_NET | CTL_DNS) << '\n';
    return true;
}

bool NBTHosts::preRun(VirtualFS& vfs) {
    try {
        ifstream in(vfs.toHostPath(file).string());
        if(!(in)) return false;
        
        cout << endl;
        int net[] = {((CTL_NET >> 24) & 0xFF), ((CTL_NET >> 16) & 0xFF), ((CTL_NET >> 8) & 0xFF), 0};
        for(string line; getline(in, line);) {
            char name[256];
            int  ip[4];
            if(sscanf(line.c_str(), "%d.%d.%d.%d%s", &ip[0], &ip[1], &ip[2], &ip[4], name) >= 5) {
                if(ip[0] == net[0] && ip[1] == net[1] && ip[2] == net[2]) {
                    cout << "- removing " << line << endl;
                    continue;
                }
            } else if(line.find("# added by ditool") != string::npos)
                continue;
            lines.push_back(line);
        }
        in.close();
        
        return true;
    } catch(exception& e) {
        cout << e.what() << endl;
        return false;
    }
}

bool NBTHosts::run(std::ofstream& out) {
    for(size_t line = 0; line < lines.size(); line++)
        out << lines[line] << '\n';
    out << "# added by ditool for previous" << '\n';
    addHost(out, CTL_NET | CTL_HOST, NAME_HOST);
    addHost(out, CTL_NET | CTL_NFSD, NAME_NFSD);
    return true;
}

void NBTHosts::addHost(ofstream& out, uint32_t addr, const std::string& hostName) {
    cout << "- adding " << ip_addr_str(addr) << " " << hostName << endl;
    out << ip_addr_str(addr) << "\t" << hostName << '\n';
}

bool NBTHostConfig::preRun(VirtualFS& vfs) {
    try {
        string templateconfig = "/usr/template/client/etc/hostconfig";
        ifstream in(vfs.toHostPath(templateconfig).string());
        if(!(in)) return false;
        
        string router = "ROUTER";
        string netmask = "IPNETMASK";
        
        cout << endl;
        cout << "- using template " << templateconfig << endl;
        
        for(string line; getline(in, line);) {
            if(line.compare(0, router.size(), router) == 0 ||
               line.compare(0, netmask.size(), netmask) == 0) {
                cout << "- removing " << line << endl;
                continue;
            }
            lines.push_back(line);
        }
        in.close();
        
        return true;
    } catch(exception& e) {
        cout << e.what() << endl;
        return false;
    }
}

bool NBTHostConfig::run(std::ofstream& out) {
    for(size_t line = 0; line < lines.size(); line++)
        out << lines[line] << '\n';
    addParameter(out, "ROUTER", "-ROUTED-");
    addParameter(out, "IPNETMASK", "-AUTOMATIC-");
    return true;
}

void NBTHostConfig::addParameter(ofstream& out, const std::string& param, const std::string& val) {
    cout << "- adding " << param << "=" << val << endl;
    out << param << "=" << val << '\n';
}

bool NBTFstab::run(std::ofstream& out) {
    out << "# fstab generated by ditool" << '\n';
    out << NAME_NFSD << ":/ / nfs rw,noauto 0 0" << '\n';
    out << NAME_NFSD << ":/private /private nfs rw,noauto 0 0" << '\n';
//  out << "/dev/sd0a /private/swapdisk 4.3 rw,noquota,noauto 0 0" << '\n';
    return true;
}

ostream& operator<<(ostream& os, const NetbootTask& task) {
    os << task.info;
    return os;
}
