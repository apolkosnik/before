//
//  BootparamProg.hpp
//  Previous
//
//  Created by Simon Schubiger on 18.02.19.
//

#ifndef BootparamProg_hpp
#define BootparamProg_hpp

#include <stdio.h>
#include "RPCProg.h"

class CBootparamProg : public CRPCProg
{
    int Null(void);
    int procedureWHOAMI(void);
    int procedureGETFILE(void);
public:
    CBootparamProg();
    virtual ~CBootparamProg();
};

#endif /* BootparamProg_h */
