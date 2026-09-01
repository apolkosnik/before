#include "NFSProg.h"
#include "nfsd.h"

CNFSProg::CNFSProg() : CRPCProg(PROG_NFS, 0, "nfsd") {
}

CNFSProg::~CNFSProg() {}

void CNFSProg::setUserID(unsigned int nUID, unsigned int nGID) {
    m_NFS2Prog.setUserID(nUID, nGID);
}

int CNFSProg::process(void) {
    if (m_param->version == 2) {
        m_NFS2Prog.setup(m_in, m_out, m_param);
        return m_NFS2Prog.process();
    } else {
        log("Client requested NFS version %u which isn't supported.\n", m_param->version);
        return PRC_NOTIMP;
    }
}

void CNFSProg::setLogOn(bool bLogOn) {
    CRPCProg::setLogOn(bLogOn);

    m_NFS2Prog.setLogOn(bLogOn);
}
