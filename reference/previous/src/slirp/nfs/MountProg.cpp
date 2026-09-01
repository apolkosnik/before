#include <fstream>
#include <sstream>

#include "MountProg.h"
#include "FileTableNFSD.h"
#include "nfsd.h"
#include "compat.h"

using namespace std;

enum
{
	MNT_OK = 0,
    MNTERR_PERM = 1,
    MNTERR_NOENT = 2,
	MNTERR_IO = 5,
    MNTERR_ACCESS = 13,
	MNTERR_NOTDIR = 20,
    MNTERR_INVAL = 22
};

CMountProg::CMountProg() : CRPCProg(PROG_MOUNT, 1, "mountd") {
    #define RPC_PROG_CLASS CMountProg
    SET_PROC(1, MNT);
    SET_PROC(3, UMNT);
    SET_PROC(5, EXPORT);
}

CMountProg::~CMountProg() {
}

int CMountProg::procedureMNT(void) {
    XDRString path;

    m_in->read(path);
    log("MNT from %s for '%s'", m_param->remoteAddr, path.c_str());
    
    uint64_t handle = nfsd_fts[0]->getFileHandle(path.c_str());
    if(handle) {
        m_out->write(MNT_OK); //OK
        
        uint64_t data[8] = {handle, 0, 0, 0, 0, 0, 0, 0};
        
        if (m_param->version == 1 || m_param->version == 2) {
            m_out->write(data, FHSIZE);
        } else {
            m_out->write(FHSIZE_NFS3);
            m_out->write(data, FHSIZE_NFS3);
            m_out->write(0);  //flavor
        }
        
        m_mounts[m_param->remoteAddr].push_back(path);
    } else {
        m_out->write(MNTERR_ACCESS);  //permission denied
    }
    
    return PRC_OK;
}

int CMountProg::procedureUMNT(void) {
    XDRString path;
    m_in->read(path);
    log("UNMT from %s for '%s'", m_param->remoteAddr, path.c_str());

    bool found = false;
    string          umtPath(path);
    vector<string>& paths = m_mounts[m_param->remoteAddr];
    for(size_t i = 0; i < paths.size(); i++) {
        if(paths[i] == umtPath) {
            found = true;
            paths.erase(paths.begin() + i);
            break;
        }
    }
    m_out->write(found ? MNT_OK : MNTERR_NOTDIR);
    
    return PRC_OK;
}

int CMountProg::procedureEXPORT(void) {
    log("EXPORT");
    
    VFSPath path = nfsd_fts[0]->getBasePathAlias();
    // dirpath
    m_out->write(1);
    m_out->write(MAXPATHLEN, path.c_str());
    // groups
    m_out->write(1);
    m_out->write(1);
    m_out->write((void*)"*...", 4);
    m_out->write(0);
    
    m_out->write(0);
    m_out->write(0);
    
    return PRC_OK;
}
