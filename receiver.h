#ifndef HOLEPOKE_RECEIVER_H
#define HOLEPOKE_RECEIVER_H

#include "endpoint.h"
#include "fsm.h"

namespace holepoke {

class ReceiverDelegate
{
public:
	virtual void invalidID() = 0;
};

class Receiver : public Endpoint
{
private:
	FiniteStateMachine _machine;
	std::string _senderID;
	ReceiverDelegate* _delegate;
	struct timeval _kRetryInterval;
	bool _invalidID;
	struct sockaddr_storage _senderAddress;
	socklen_t _senderAddressLen;
	
public:	
	Receiver(const struct sockaddr* serverAddr, socklen_t serverAddrLen);
	
	void setDelegate(ReceiverDelegate* delegate);
	
	// Block until a) the client is connected to a peer with the given ID or b) an error occurred.
	void connectToSender(const std::string & senderID);
	
	// State routines are public but are really implementation only. 
	FSMEvent stLookupAddressForID();
	FSMEvent stLocalConnectToPeer();
	FSMEvent stRemoteConnectToPeer();
	FSMEvent stConnected();
	FSMEvent stInvalidID();
};

}

#endif
