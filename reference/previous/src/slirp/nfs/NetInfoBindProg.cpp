//
//  NetInfoBindProg.cpp
//  Slirp
//
//  Created by Simon Schubiger on 9/1/21.
//

#include "nfsd.h"
#include "NetInfoBindProg.h"
#include "configuration.h"

#include <sstream>
#include <cstring>

using namespace std;

extern void add_rpc_program(CRPCProg *pRPCProg, uint16_t port = 0);

class NIProps
{
private:
    map<string, string> m_map;
public:
    NIProps() {}
    
    NIProps(const string& key, const string& val) {
        m_map[key] = val;
    }
    NIProps& operator()(const string& key, const string& val) {
        m_map[key] = val;
        return *this;
    }
    operator map<string, string>() {
        return m_map;
    }
    
    string& operator[] (const string& key) {
        return m_map[key];
    }
};

static string ip_addr_str(uint32_t addr, size_t count) {
    stringstream ss;
    switch(count) {
        case 3:
            ss << (0xFF&(addr >> 24)) << "." << (0xFF&(addr >> 16)) << "." <<  (0xFF&(addr >> 8));
            break;
        case 4:
            ss << (0xFF&(addr >> 24)) << "." << (0xFF&(addr >> 16)) << "." <<  (0xFF&(addr >> 8)) << "." << (0xFF&(addr));
            break;
    }
    return ss.str();
}

void CNetInfoBindProg::addHost(CNetInfoProg& host, const string& name, const string& systemType) {
    add_rpc_program(&host);
    doRegister(host.mTag, host.getPortUDP(), host.getPortTCP());
    host.mRoot.add("trusted_networks", ip_addr_str(CTL_NET, 3));
    string master(name);
    master += "/local";
    //host.mRoot.add("master", name);
    NetInfoNode* machines   = host.mRoot.add(NIProps("name","machines"));
    machines->add(NIProps("name","broadcasthost")("ip_address",ip_addr_str(0xFFFFFFFF, 4))("serves","../network"));
    machines->add(NIProps("name","localhost")("ip_address",ip_addr_str(0x7F000001, 4))("serves","./local")("netgroups","")("system_type",systemType));
}

void CNetInfoBindProg::configure(bool ntp) {
    /* Remove old */
    vector<NetInfoNode*> old = m_Network.mRoot.find("name","locations");
    m_Network.mRoot.remove(old[0]);
    
    /* Create new */
    static const char* domain = (NAME_DOMAIN[0] == '.' ? &NAME_DOMAIN[1] : &NAME_DOMAIN[0]);
    NetInfoNode* locations = m_Network.mRoot.add(NIProps("name","locations"));
    locations->add(NIProps("name","resolver")("nameserver",ip_addr_str(CTL_NET|CTL_DNS, 4))("domain",domain)("search",domain));
    if (ntp) {
        locations->add(NIProps("name","ntp")("server",NAME_NFSD)("host",NAME_NFSD));
    }
}

CNetInfoBindProg::CNetInfoBindProg()
    : CRPCProg(PROG_NETINFOBIND, 1, "nibindd")
    , m_Local("local")
    , m_Network("network")
{
    #define RPC_PROG_CLASS  CNetInfoBindProg
    
    SET_PROC(1, REGISTER);
    SET_PROC(2, UNREGISTER);
    SET_PROC(3, GETREGISTER);
    SET_PROC(4, LISTREG);
    SET_PROC(5, CREATEMASTER);
    SET_PROC(6, CREATECLONE);
    SET_PROC(7, DESTROYDOMAIN);
    SET_PROC(8, BIND);
    
    string systemType = ConfigureParams.System.nMachineType == NEXT_STATION ? "NeXTstation" : "NeXTcube";
    if(ConfigureParams.System.nMachineType == NEXT_STATION && ConfigureParams.System.bColor)
        systemType += " Color";

    addHost(m_Local, NAME_HOST, systemType);

    add_rpc_program(&m_Network, PORT_NETINFO);
    doRegister(m_Network.mTag, m_Network.getPortUDP(), m_Network.getPortTCP());
    
    m_Network.mRoot.add("master", NAME_NFSD "/network");
    m_Network.mRoot.add("trusted_networks", ip_addr_str(CTL_NET, 3));
    NetInfoNode* machines   = m_Network.mRoot.add(NIProps("name","machines"));
    
    char hostname[NAME_HOST_MAX];
    hostname[0] = '\0';
    gethostname(hostname, sizeof(hostname));

    const char* domain = (NAME_DOMAIN[0] == '.' ? &NAME_DOMAIN[1] : &NAME_DOMAIN[0]);

    machines->add(NIProps("name",hostname) ("ip_address",ip_addr_str(CTL_NET|CTL_ALIAS, 4)));
    machines->add(NIProps("name",NAME_HOST)("ip_address",ip_addr_str(CTL_NET|CTL_HOST,  4))("serves",NAME_HOST"/local")("netgroups","")("system_type",systemType));
    machines->add(NIProps("name",NAME_DNS) ("ip_address",ip_addr_str(CTL_NET|CTL_DNS,   4)));
    machines->add(NIProps("name",NAME_NFSD)("ip_address",ip_addr_str(CTL_NET|CTL_NFSD,  4))("serves","./network,../network"));

    NetInfoNode* mounts     = m_Network.mRoot.add(NIProps("name","mounts"));
    mounts->add(NIProps("name",NAME_NFSD":/")("dir","/Net")("opts","rw,net"));
    
    NetInfoNode* locations  = m_Network.mRoot.add(NIProps("name","locations"));
    locations->add(NIProps("name","resolver")("nameserver",ip_addr_str(CTL_NET|CTL_DNS, 4))("domain",domain)("search",domain));
    locations->add(NIProps("name","ntp")("server",NAME_NFSD)("host",NAME_NFSD));

    /*
    NetInfoNode* printers   = m_Network.mRoot.add(NIProps("name","printers"));
    NetInfoNode* fax_modems = m_Network.mRoot.add(NIProps("name","fax_modems"));
    NetInfoNode* aliases    = m_Network.mRoot.add(NIProps("name","aliases"));
    
    const size_t buffer_size = 1024*1024;
    char* buffer = new char[buffer_size];

    NetInfoNode* groups     = m_Network.mRoot.add(NIProps("name","groups"));
    
    int count = tryRead("/etc/group", buffer, buffer_size);
    if(count > 0) {
        buffer[count] = '\0';
        char* line = strtok(buffer, "\n");
        while(line) {
            NIProps props;
            if(parse_group(line, props))
                groups->add(props);
            line  = strtok(NULL, "\n");
        }
    }
    
    groups->addEx(NIProps("name","wheel")   ("gid","0") ("passwd","*")("users","root,me"));
    groups->addEx(NIProps("name","nogroup") ("gid","-2")("passwd","*"));
    groups->addEx(NIProps("name","daemon")  ("gid","1") ("passwd","*")("users","daemon"));
    groups->addEx(NIProps("name","sys")     ("gid","2") ("passwd","*"));
    groups->addEx(NIProps("name","bin")     ("gid","3") ("passwd","*"));
    groups->addEx(NIProps("name","uucp")    ("gid","4") ("passwd","*"));
    groups->addEx(NIProps("name","kmem")    ("gid","5") ("passwd","*"));
    groups->addEx(NIProps("name","news")    ("gid","6") ("passwd","*"));
    groups->addEx(NIProps("name","ingres")  ("gid","7") ("passwd","*"));
    groups->addEx(NIProps("name","tty")     ("gid","8") ("passwd","*"));
    groups->addEx(NIProps("name","operator")("gid","9") ("passwd","*"));
    groups->addEx(NIProps("name","staff")   ("gid","10")("passwd","*")("users","root,me"));
    groups->addEx(NIProps("name","other")   ("gid","20")("passwd","*"));
                      
    NetInfoNode* users     = m_Network.mRoot.add(NIProps("name","users"));
    
    count = tryRead("/etc/passwd", buffer, buffer_size);
    if(count > 0) {
        buffer[count] = '\0';
        char* line = strtok(buffer, "\n");
        while(line) {
            NIProps props;
            if(parse_user(line, props))
                users->add(props)->add(NIProps("name","info")("_writers", props["name"]));
            line  = strtok(NULL, "\n");
        }
    }
    
    NetInfoNode* user_root = users->addEx(NIProps("name","root")("_writers_passwd","root")("gid","1")("home","/")("passwd","")("realname","Operator")("shell","/bin/csh")("uid","0"));
    user_root->addEx(NIProps("name","info")("_writers","root"));
    
    delete[] buffer;
     */
}
                                       
CNetInfoBindProg::~CNetInfoBindProg() {}
 
void CNetInfoBindProg::doRegister(const std::string& tag, uint32_t udpPort, uint32_t tcpPort) {
    m_Register[tag].tcp_port = tcpPort;
    m_Register[tag].udp_port = udpPort;
    
    log("%s tcp:%d udp:%d", tag.c_str(), tcpPort, udpPort);
}

int CNetInfoBindProg::procedureREGISTER() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureUNREGISTER() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureGETREGISTER() {
    XDRString tag;
    m_in->read(tag);
    
    std::map<std::string, nibind_addrinfo>::iterator it = m_Register.find(tag.c_str());
    
    if(it == m_Register.end()) {
        log("no tag %s", tag.c_str());
        m_out->write(NI_NOTAG);
    } else {
        log("%s tcp:%d udp:%d", tag.c_str(), it->second.tcp_port, it->second.udp_port);
        m_out->write(NI_OK);
        m_out->write(it->second.udp_port);
        m_out->write(it->second.tcp_port);
    }

    return PRC_OK;
}
int CNetInfoBindProg::procedureLISTREG() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureCREATEMASTER() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureCREATECLONE() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureDESTROYDOMAIN() {
    return PRC_NOTIMP;
}
int CNetInfoBindProg::procedureBIND() {
    uint32_t  clientAddr;
    XDRString clientTag;
    XDRString serverTag;
    
    m_in->read(&clientAddr);
    m_in->read(clientTag);
    m_in->read(serverTag);

    m_out->write(NI_OK);

    return PRC_OK;
}
