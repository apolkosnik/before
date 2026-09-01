#ifndef _SOCKETLISTENER_H_
#define _SOCKETLISTENER_H_

#include <stdint.h>

class CSocket;

class ISocketListener
{
public:
	virtual void socketReceived(CSocket *pSocket, uint32_t header) = 0;
};

#endif
