//
//  NetInfo.cpp
//  Slirp
//
//  Created by Simon Schubiger on 9/1/21.
//

#include "nfsd.h"
#include "NetInfoProg.h"
#include <sstream>

using namespace std;

#define DBG 1

CNetInfoProg::CNetInfoProg(const string& tag)
    : CRPCProg(PROG_NETINFO, 2, string("netinfod:" + tag))
    , mTag(tag)
    , mRoot(mIdMap, nullptr)
{
    #define RPC_PROG_CLASS  CNetInfoProg
    
    SET_PROC(1, STATISTICS);
    SET_PROC(2, ROOT);
    SET_PROC(3, SELF);
    SET_PROC(4, PARENT);
    SET_PROC(5, CREATE);
    SET_PROC(6, DESTROY);
    SET_PROC(7, READ);
    SET_PROC(8, WRITE);
    SET_PROC(9, CHILDREN);
    SET_PROC(10, LOOKUP);
    SET_PROC(11, LIST);
    SET_PROC(12, CREATEPROP);
    SET_PROC(13, DESTROYPROP);
    SET_PROC(14, READPROP);
    SET_PROC(15, WRITEPROP);
    SET_PROC(16, RENAMEPROP);
    SET_PROC(17, LISTPROPS);
    SET_PROC(18, CREATENAME);
    SET_PROC(19, DESTROYNAME);
    SET_PROC(20, READNAME);
    SET_PROC(21, WRITENAME);
    SET_PROC(22, RPARENT);
    SET_PROC(23, LISTALL);
    SET_PROC(24, BIND);
    SET_PROC(25, READALL);
    SET_PROC(26, CRASHED);
    SET_PROC(27, RESYNC);
    SET_PROC(28, LOOKUPREAD);
}

CNetInfoProg::~CNetInfoProg() {}
 
static string to_string(ni_status status) {
    switch(status) {
        case NI_OK:              return "NI_OK";
        case NI_BADID:           return "NI_BADID";
        case NI_STALE:           return "NI_STALE";
        case NI_NOSPACE:         return "NI_NOSPACE";
        case NI_PERM:            return "NI_PERM";
        case NI_NODIR:           return "NI_NODIR";
        case NI_NOPROP:          return "NI_NOPROP";
        case NI_NONAME:          return "NI_NONAME";
        case NI_NOTEMPTY:        return "NI_NOTEMPTY";
        case NI_UNRELATED:       return "NI_UNRELATED";
        case NI_SERIAL:          return "NI_SERIAL";
        case NI_NETROOT:         return "NI_NETROOT";
        case NI_NORESPONSE:      return "NI_NORESPONSE";
        case NI_RDONLY:          return "NI_RDONLY";
        case NI_SYSTEMERR:       return "NI_SYSTEMERR";
        case NI_ALIVE:           return "NI_ALIVE";
        case NI_NOTMASTER:       return "NI_NOTMASTER";
        case NI_CANTFINDADDRESS: return "NI_CANTFINDADDRESS";
        case NI_DUPTAG:          return "NI_DUPTAG";
        case NI_NOTAG:           return "NI_NOTAG";
        case NI_AUTHERROR:       return "NI_AUTHERROR";
        case NI_NOUSER:          return "NI_AUTHERROR";
        case NI_MASTERBUSY:      return "NI_MASTERBUSY";
        case NI_INVALIDDOMAIN:   return "NI_INVALIDDOMAIN";
        case NI_BADOP:           return "NI_BADOP";
        case NI_FAILED:          return "NI_FAILED";
        default:                 return "<unknown>";
    }
}

static string to_string(const vector<string>& strs) {
    string result;
    for(size_t i = 0; i < strs.size(); i++) {
        result += " '";
        result += strs[i];
        result += "'";
    }
    return result;
}

static void write_ni_namelist(XDROutput* m_out, vector<string> names) {
    m_out->write(names.size());
    for(size_t i = 0; i < names.size(); i++)
        m_out->write(names[i]);
}

static void write_ni_proplist(XDROutput* m_out, map<string, string> props) {
    m_out->write(props.size());
    for (map<string, string>::iterator it = props.begin(); it != props.end(); it++) {
        m_out->write(it->first);
        write_ni_namelist(m_out, NetInfoNode::getPropValues(props, it->first));
    }
}

static string to_string(const map<string, string>& m) {
    string result;
    for(map<string,string>::const_iterator it = m.begin(); it != m.end(); it++) {
        result += " [";
        result += it->first;
        result += "] ='";
        result += it->second;
        result += "'";
    }
    return result;
}


int CNetInfoProg::procedureSTATISTICS() {
    map<string, string> result;
    
    static char buf[32];
    snprintf(buf, sizeof(buf), "%u", mRoot.checksum());
    result["checksum"] = buf;
    
    write_ni_proplist(m_out, result);
    
    return PRC_OK;
}

static void read_ni_id(XDRInput* in, ni_id& ni_id) {
    in->read(&ni_id.object);
    in->read(&ni_id.instance);
}

static void write_ni_id(XDROutput* out, const ni_id& ni_id) {
    out->write(ni_id.object);
    out->write(ni_id.instance);
}

int CNetInfoProg::procedureROOT() {
    m_out->write(NI_OK);
    write_ni_id(m_out, mRoot.mId);
    
    return PRC_OK;
}

int CNetInfoProg::procedureSELF() {
    ni_id      ni_id;
    read_ni_id(m_in, ni_id);
    
    ni_status status(NI_OK);
    mRoot.find(ni_id, status);
    m_out->write(status);
    if(status == NI_OK)
        write_ni_id(m_out, ni_id);
    return PRC_OK;
}

int CNetInfoProg::procedurePARENT() {
    ni_id      ni_id;
    read_ni_id(m_in, ni_id);
    
    ni_status status(NI_OK);
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "PARENT:";
    if(node) dbg << node->getPath();
    dbg << " =";
#endif

    if(node->mParent == nullptr)
        status = NI_NETROOT;
        
    m_out->write(status);
    if(status == NI_OK) {
        m_out->write(node->mParent->mId.object);
        write_ni_id(m_out, ni_id);
#if DBG
        dbg << node->mParent->getPath();
#endif
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    return PRC_OK;
}

int CNetInfoProg::procedureCREATE() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureDESTROY() {
    return PRC_NOTIMP;
}

int CNetInfoProg::procedureREAD() {
    ni_id     ni_id;
    
    read_ni_id(m_in, ni_id);

    ni_status status;
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "READ:";
    if(node) dbg << node->getPath();
    dbg << " =";
#endif
    
    m_out->write(status);
    if(status == NI_OK) {
        write_ni_id(m_out, ni_id);
        write_ni_proplist(m_out, node->mProps);
#if DBG
        dbg << to_string(node->mProps);
#endif
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    return PRC_OK;
}

int CNetInfoProg::procedureWRITE() {
    return PRC_NOTIMP;
}

int CNetInfoProg::procedureCHILDREN() {
    ni_id     ni_id;
    
    read_ni_id(m_in, ni_id);

    ni_status status(NI_OK);
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "CHILDREN:";
    if(node) dbg << node->getPath();
    dbg << " =";
#endif

    m_out->write(status);
    if(status == NI_OK) {
        m_out->write(node->mChildren.size());
        for(size_t i = 0; i < node->mChildren.size(); i++) {
            m_out->write(node->mChildren[i]->mId.object);
#if DBG
            dbg << " " << node->mChildren[i]->mId.object;
#endif
        }
        
        write_ni_id(m_out, ni_id);
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    return PRC_OK;
}

int CNetInfoProg::procedureLOOKUP() {
    ni_id     ni_id;
    XDRString key;
    XDRString value;

    read_ni_id(m_in, ni_id);
    m_in->read(key);
    m_in->read(value);
    
    ni_status status(NI_OK);
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "LOOKUP:";
    if(node) dbg << node->getPath();
    dbg << key << ":" << value << " =";
#endif

    if(node && node->find(key, value).empty())
        status = NI_NODIR;
    
    m_out->write(status);
    if(status == NI_OK) {
        vector<NetInfoNode*> nodes = node->find(key, value);
        m_out->write(nodes.size());
        for(size_t i = 0; i < nodes.size(); i++) {
            m_out->write(nodes[i]->mId.object);
#if DBG
            dbg << " " << nodes[i]->mId.object;
#endif
        }
        write_ni_id(m_out, ni_id);
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    
    return PRC_OK;
}

int CNetInfoProg::procedureLIST() {
    ni_id     ni_id;
    XDRString propName;
    
    read_ni_id(m_in, ni_id);
    m_in->read(propName);

    ni_status status(NI_OK);
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "LIST:";
    if(node) dbg << node->getPath();
    dbg << propName << " =";
#endif
    
    m_out->write(status);
    if(status == NI_OK) {
        m_out->write(node->mChildren.size());
        for(size_t i = 0; i < node->mChildren.size(); i++) {
            m_out->write(node->mChildren[i]->mId.object);
            vector<string> propValues = NetInfoNode::getPropValues(node->mChildren[i]->mProps, propName);
            if(propValues.empty())
                m_out->write(0);
            else {
                m_out->write(1);
                write_ni_namelist(m_out, propValues);
#if DBG
                dbg << " [" << i << "] =" << to_string(propValues) << "";
#endif
            }
        }
        
        write_ni_id(m_out, ni_id);
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    
    return PRC_OK;
}

int CNetInfoProg::procedureCREATEPROP() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureDESTROYPROP() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureREADPROP() {
    ni_id     ni_id;
    uint32_t  index;
    
    read_ni_id(m_in, ni_id);
    m_in->read(&index);
    
    ni_status status;
    NetInfoNode* node = mRoot.find(ni_id, status);

#if DBG
    stringstream dbg;
    dbg << "READPROP:";
    if(node)
        dbg << node->getPath() << node->getPropNames()[index];
    else
        dbg << index;
    dbg << " =";
#endif

    vector<string> propValues;
    if(node)
        propValues = NetInfoNode::getPropValues(node->mProps, index);
    
    if(node && propValues.empty())
        status = NI_NOPROP;

    m_out->write(status);

    if(status == NI_OK) {
        write_ni_namelist(m_out, propValues);
        write_ni_id(m_out, ni_id);
#if DBG
        dbg << to_string(propValues);
#endif
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif

    return PRC_OK;
}

int CNetInfoProg::procedureWRITEPROP() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureRENAMEPROP() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureLISTPROPS() {
    ni_id     ni_id;
    
    read_ni_id(m_in, ni_id);

    ni_status status;
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "LISTPROPS:";
    if(node) dbg << node->getPath();
    dbg << " =";
#endif

    m_out->write(status);
    if(status == NI_OK) {
        vector<string> propNames = node->getPropNames();
        write_ni_namelist(m_out, propNames);
        write_ni_id(m_out, ni_id);
#if DBG
        dbg << to_string(propNames);
#endif
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif

    return PRC_OK;
}

int CNetInfoProg::procedureCREATENAME() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureDESTROYNAME() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureREADNAME() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureWRITENAME() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureRPARENT() {
    m_out->write(NI_NETROOT);
    return PRC_OK;
}
int CNetInfoProg::procedureLISTALL() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureBIND() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureREADALL() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureCRASHED() {
    return PRC_NOTIMP;
}
int CNetInfoProg::procedureRESYNC() {
    return PRC_NOTIMP;
}

int CNetInfoProg::procedureLOOKUPREAD() {
    ni_id     ni_id;
    XDRString key;
    XDRString value;

    read_ni_id(m_in, ni_id);
    m_in->read(key);
    m_in->read(value);

    ni_status status(NI_OK);
    NetInfoNode* node = mRoot.find(ni_id, status);
    
#if DBG
    stringstream dbg;
    dbg << "LOOKUPREAD:";
    if(node) dbg << node->getPath();
    dbg << key << ":" << value << " =";
#endif

    if(node && node->find(key, value).empty())
        status = NI_NODIR;
        
    m_out->write(status);
    if(status == NI_OK) {
        map<string, string> result;
        vector<NetInfoNode*> nodes = node->find(key, value);
        for(size_t i = 0; i < nodes.size(); i++) {
            for(map<string,string>::const_iterator it = nodes[i]->mProps.begin(); it != nodes[i]->mProps.end(); it++) {
                if(result.find(it->first) == result.end())
                    result[it->first] = it->second;
                else {
                    result[it->first] += ",";
                    result[it->first] += it->second;
                }
            }
        }

        write_ni_id(m_out, ni_id);
        write_ni_proplist(m_out, result);
#if DBG
        dbg << to_string(result);
#endif
    }
    
#if DBG
    log("%s (%s)", dbg.str().c_str(), to_string(status).c_str());
#endif
    return PRC_OK;
}

NetInfoNode* NetInfoNode::add(const map<string, string>& props) {
    NetInfoNode* result = new NetInfoNode(mIdmap, this, props);
    mChildren.push_back(result);
    return result;
}

NetInfoNode* NetInfoNode::addEx(const map<string, string>& props) {
    map<string, string>::const_iterator it = props.find("name");
    vector<NetInfoNode*> nodes;
    if(it != props.end())
        nodes = find("name", it->second);
    NetInfoNode* result = nullptr;
    if(nodes.empty()) {
        result = new NetInfoNode(mIdmap, this, props);
        mChildren.push_back(result);
    } else
        result = nodes[0];
    return result;
}

void NetInfoNode::add(const string& key, const string& value) {
    mProps[key] = value;
}

NetInfoNode* NetInfoNode::find(struct ni_id& ni_id, ni_status& status, bool forWrite) const {
    map<uint32_t,NetInfoNode*>::const_iterator it = mIdmap.find(ni_id.object);
    if(it == mIdmap.end()) {
        status = NI_BADID;
        return nullptr;
    }
    NetInfoNode* result = it->second;
    status = (!(forWrite) || (result->mId.instance == ni_id.instance)) ? NI_OK : NI_STALE;
    ni_id.instance = result->mId.instance;
    return result;
}

vector<NetInfoNode*> NetInfoNode::find(const string& key, const string& value) const {
    vector<NetInfoNode*> result;
    for(size_t i = 0; i < mChildren.size(); i++) {
        if(mChildren[i]->mProps.find(key) != mChildren[i]->mProps.end() &&
           mChildren[i]->mProps[key] == value)
            result.push_back(mChildren[i]);
    }
    return result;
}

void NetInfoNode::remove(NetInfoNode* node) {
    for(vector<NetInfoNode*>::const_iterator it = mChildren.begin(); it != mChildren.end(); it++) {
        if(*it == node) {
            mChildren.erase(it);
            delete *it;
            return;
        }
    }
}

static unsigned int checksum(string str) {
    unsigned int result = 0;
    for (size_t i = 0; i < str.size(); ++i)
        result = result * 31 + static_cast<int>(str[i]);
    return result;
}

unsigned int NetInfoNode::checksum() {
    unsigned int result = 0;
    for(size_t i = 0; i < mChildren.size(); i++)
        result += mChildren[i]->checksum();
    for(map<string,string>::const_iterator it = mProps.begin(); it != mProps.end(); it++) {
        result += ::checksum(it->first);
        result += ::checksum(it->second);
    }
    return result;
}

static void split_value(const string& value, vector<string>& result) {
    stringstream ss(value);
    std::string prop;

    while (getline (ss, prop, ','))
        result.push_back (prop);
}

vector<string> NetInfoNode::getPropValues(const map<string, string>& props, const string& key) {
    vector<string> result;
    map<string,string>::const_iterator it = props.find(key);
    if(it != props.end())
        split_value(it->second, result);

    return result;
}

vector<string> NetInfoNode::getPropValues(const map<string, string>& props, uint32_t index) {
    vector<string> result;
    for(map<string,string>::const_iterator it = props.begin(); it != props.end(); it++, index--) {
        if(index == 0) {
            split_value(it->second, result);
            break;
        }
    }

    return result;
}

string NetInfoNode::getPath() {
    string result;
    if(mParent) {
        result += mParent->getPath();
        result += mProps["name"];
    }
    result += "/";
    return result;
}

vector<string> NetInfoNode::getPropNames() {
    vector<string> result;
    for (map<string, string>::iterator it = mProps.begin(); it != mProps.end(); it++)
        result.push_back(it->first);
    return result;
}

string NetInfoNode::getPropValue(const string& key) const {
    map<string,string>::const_iterator it = mProps.find(key);
    if(it == mProps.end()) return "";
    return it->second;
 }
