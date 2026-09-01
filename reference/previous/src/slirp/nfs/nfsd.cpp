#include <stdio.h>
#include <unistd.h>
#include <vector>

#include "nfsd.h"
#include "RPCServer.h"
#include "TCPServerSocket.h"
#include "UDPServerSocket.h"
#include "PortmapProg.h"
#include "NFSProg.h"
#include "MountProg.h"
#include "BootparamProg.h"
#include "configuration.h"
#include "SocketListener.h"
#include "VDNS.h"
#include "FileTableNFSD.h"
#include "NetInfoBindProg.h"

static bool         g_bLogOn = true;
static CPortmapProg g_PortmapProg;
static CRPCServer   g_RPCServer;

static std::vector<UDPServerSocket*> SERVER_UDP;
static std::vector<TCPServerSocket*> SERVER_TCP;

FileTableNFSD* nfsd_fts[] = {NULL}; // to be extended for multiple exports

static bool initialized = false;

void add_rpc_program(CRPCProg *pRPCProg, uint16_t port = 0) {
    UDPServerSocket* udp = new UDPServerSocket(&g_RPCServer);
    TCPServerSocket* tcp = new TCPServerSocket(&g_RPCServer);
    
    g_RPCServer.set(pRPCProg->getProgNum(), pRPCProg);

    if (tcp->open(pRPCProg->getProgNum(), port) && udp->open(pRPCProg->getProgNum(), port)) {
        printf("[NFSD] %s started\n", pRPCProg->getName().c_str());
        pRPCProg->init(tcp->getPort(), udp->getPort());
        g_PortmapProg.Add(pRPCProg);
        SERVER_TCP.push_back(tcp);
        SERVER_UDP.push_back(udp);
    } else {
        printf("[NFSD] %s start failed.\n", pRPCProg->getName().c_str());
    }
}

static void printAbout(void) {
    printf("[NFSD] Network File System server\n");
    printf("[NFSD] Copyright (C) 2005 Ming-Yang Kao\n");
    printf("[NFSD] Edited in 2011 by ZeWaren\n");
    printf("[NFSD] Edited in 2013 by Alexander Schneider (Jankowfsky AG)\n");
    printf("[NFSD] Edited in 2014 2015 by Yann Schepens\n");
    printf("[NFSD] Edited in 2016 by Peter Philipp (Cando Image GmbH), Marc Harding\n");
    printf("[NFSD] Mostly rewritten in 2019-2021 by Simon Schubiger for Previous NeXT emulator\n");
}

extern "C" int nfsd_read(const char* path, size_t fileOffset, void* dst, size_t count) {
    if(nfsd_fts[0]) {
        VFSFile file(*nfsd_fts[0], path, "rb");
        if(file.isOpen())
            return file.read(fileOffset, dst, count);
    }
    return -1;
}

extern "C" void nfsd_start(void) {
    if(access(ConfigureParams.Ethernet.szNFSroot, F_OK | R_OK | W_OK) < 0) {
        printf("[NFSD] can not access directory '%s'. nfsd startup canceled.\n", ConfigureParams.Ethernet.szNFSroot);
        delete nfsd_fts[0];
        nfsd_fts[0] = NULL;
        return;
    }
    
    if(nfsd_fts[0]) {
        if(nfsd_fts[0]->getBasePath() != HostPath(ConfigureParams.Ethernet.szNFSroot)) {
            VFSPath basePath = nfsd_fts[0]->getBasePathAlias();
            delete nfsd_fts[0];
            nfsd_fts[0] = new FileTableNFSD(ConfigureParams.Ethernet.szNFSroot, basePath);
        }
    } else {
        nfsd_fts[0] = new FileTableNFSD(ConfigureParams.Ethernet.szNFSroot, "/");
    }
    
    static CNFSProg         sNFSProg;
    static CMountProg       sMountProg;
    static CBootparamProg   sBootparamProg;
    static CNetInfoBindProg sNetInfoBindProg;
    
    sNetInfoBindProg.configure(ConfigureParams.Ethernet.bNetworkTime);
    
    if(initialized) return;

    char nfsd_hostname[NAME_HOST_MAX];
    gethostname(nfsd_hostname, sizeof(nfsd_hostname));
    
    printf("[NFSD] starting local NFS daemon on '%s', exporting '%s'\n", nfsd_hostname, ConfigureParams.Ethernet.szNFSroot);
    printAbout();
    
    g_RPCServer.setLogOn(g_bLogOn);

    add_rpc_program(&g_PortmapProg,  PORT_PORTMAP);
    add_rpc_program(&sNFSProg,       PORT_NFS);
    add_rpc_program(&sMountProg);
    add_rpc_program(&sBootparamProg);
    add_rpc_program(&sNetInfoBindProg);

    std::vector<NetInfoNode*> users = sNetInfoBindProg.m_Network.mRoot.find("name", "users");
    for(size_t i = 0; i < users.size(); i++)
        if(users[i]->getPropValue("name") == "me")
            sNFSProg.setUserID(::atoi(users[i]->getPropValue("uid").c_str()), ::atoi(users[i]->getPropValue("gid").c_str()));
    
    static VDNS vdns(&sNetInfoBindProg);
    
    initialized = true;
}

extern "C" int nfsd_match_addr(uint32_t addr) {
    return (addr == (ntohl(special_addr.s_addr) | CTL_NFSD)) ||
           (addr == (ntohl(special_addr.s_addr) | ~(uint32_t)CTL_NET_MASK)) ||
           (addr == (ntohl(special_addr.s_addr) | ~(uint32_t)CTL_CLASS_MASK(CTL_NET))); // NS kernel seems to broadcast on 10.255.255.255
}

extern "C" void nfsd_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO) {
    uint16_t dport = ntohs(*dportNBO);
    uint16_t port  = UDPServerSocket::toLocalPort(dport);
    if(port) {
        *dportNBO = htons(port);
        *ipNBO    = loopback_addr;
    }
}

extern "C" void nfsd_tcp_map_to_local_port(uint16_t port, uint16_t* sin_portNBO) {
    uint16_t localPort = TCPServerSocket::toLocalPort(port);
    if(localPort)
        *sin_portNBO = htons(localPort);
}

extern "C" void udp_map_from_local_port(uint16_t port, struct in_addr* saddrNBO, uint16_t* sin_portNBO) {
    uint16_t localPort = UDPServerSocket::fromLocalPort(port);
    if(localPort) {
        *sin_portNBO = htons(localPort);
        switch(localPort) {
            case PORT_DNS:
                saddrNBO->s_addr = special_addr.s_addr | htonl(CTL_DNS);
                break;
            default:
                saddrNBO->s_addr = special_addr.s_addr | htonl(CTL_NFSD);
                break;
        }
    }
}
