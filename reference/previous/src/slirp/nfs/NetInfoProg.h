//
//  NetInfo.hpp
//  Slirp
//
//  Created by Simon Schubiger on 9/1/21.
//

#ifndef NetInfoProg_hpp
#define NetInfoProg_hpp

#include <stdio.h>
#include <map>
#include <vector>
#include <string>

#include "RPCProg.h"

enum ni_status {
    NI_OK = 0,
    NI_BADID = 1,
    NI_STALE = 2,
    NI_NOSPACE = 3,
    NI_PERM = 4,
    NI_NODIR = 5,
    NI_NOPROP = 6,
    NI_NONAME = 7,
    NI_NOTEMPTY = 8,
    NI_UNRELATED = 9,
    NI_SERIAL = 10,
    NI_NETROOT = 11,
    NI_NORESPONSE = 12,
    NI_RDONLY = 13,
    NI_SYSTEMERR = 14,
    NI_ALIVE = 15,
    NI_NOTMASTER = 16,
    NI_CANTFINDADDRESS = 17,
    NI_DUPTAG = 18,
    NI_NOTAG = 19,
    NI_AUTHERROR = 20,
    NI_NOUSER = 21,
    NI_MASTERBUSY = 22,
    NI_INVALIDDOMAIN = 23,
    NI_BADOP = 24,
    NI_FAILED = 9999,
};

class NetInfoNode;

typedef std::map<uint32_t, NetInfoNode*> NIIDMap;

struct ni_id {
    uint32_t object;
    uint32_t instance;
    ni_id() : object(0), instance(0) {}
    ni_id(uint32_t object) : object(object), instance(1) {}
};

class NetInfoNode {
public:
    ni_id                              mId;
    NIIDMap&                           mIdmap;
    NetInfoNode*                       mParent;
    std::map<std::string, std::string> mProps;
    std::vector<NetInfoNode*>          mChildren;
    NetInfoNode(NIIDMap& idmap, NetInfoNode* parent, const std::map<std::string, std::string>& props)
    : mId(idmap.size())
    , mIdmap(idmap)
    , mParent(parent)
    , mProps(props)
    {idmap[mId.object] = this;}
    NetInfoNode(NIIDMap& idmap, NetInfoNode* parent)
    : mId(idmap.size())
    , mIdmap(idmap)
    , mParent(parent)
    {idmap[mId.object] = this;}
    NetInfoNode*              add(const std::map<std::string, std::string>& props);
    NetInfoNode*              addEx(const std::map<std::string, std::string>& props);
    void                      add(const std::string& key, const std::string& value);
    void                      remove(NetInfoNode* node);
    NetInfoNode*              find(struct ni_id& ni_id, ni_status& status, bool forWrite = false) const;
    std::vector<NetInfoNode*> find(const std::string& key, const std::string& value) const;
    unsigned int              checksum(void);
    std::string               getPath(void);
    std::string               getPropValue(const std::string& key) const;
    std::vector<std::string>  getPropNames();
    
    static std::vector<std::string>  getPropValues(const std::map<std::string, std::string>& props, const std::string& key);
    static std::vector<std::string>  getPropValues(const std::map<std::string, std::string>& props, uint32_t index);
};

class CNetInfoProg : public CRPCProg
{
    int Null(void);
 
    int procedureSTATISTICS(void);
    int procedureROOT(void);
    int procedureSELF(void);
    int procedurePARENT(void);
    int procedureCREATE(void);
    int procedureDESTROY(void);
    int procedureREAD(void);
    int procedureWRITE(void);
    int procedureCHILDREN(void);
    int procedureLOOKUP(void);
    int procedureLIST(void);
    int procedureCREATEPROP(void);
    int procedureDESTROYPROP(void);
    int procedureREADPROP(void);
    int procedureWRITEPROP(void);
    int procedureRENAMEPROP(void);
    int procedureLISTPROPS(void);
    int procedureCREATENAME(void);
    int procedureDESTROYNAME(void);
    int procedureREADNAME(void);
    int procedureWRITENAME(void);
    int procedureRPARENT(void);
    int procedureLISTALL(void);
    int procedureBIND(void);
    int procedureREADALL(void);
    int procedureCRASHED(void);
    int procedureRESYNC(void);
    int procedureLOOKUPREAD(void);
    
public:
    CNetInfoProg(const std::string& tag);
    virtual ~CNetInfoProg();
    
    std::map<uint32_t, NetInfoNode*> mIdMap;
    std::string                      mTag;
    NetInfoNode                      mRoot;
};

#endif /* NetInfoProg_hpp */
