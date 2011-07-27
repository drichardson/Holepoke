#ifndef HOLEPOKE_SENDER_H
#define HOLEPOKE_SENDER_H

#include "endpoint.h"
#include "fsm.h"

namespace holepoke {

class SenderDelegate
{
public:
	virtual void gotID(const std::string & identifier) = 0;
};

class Sender : public Endpoint
{
private:
	FiniteStateMachine _machine;
	std::string _identifier;
	SenderDelegate* _delegate;
	struct timeval _kRetryInterval;

public:
	Sender(const struct sockaddr* serverAddr, socklen_t serverAddrLen);

	void setDelegate(SenderDelegate* delegate);
	void connectToReceiver();

	// State routines are public but are for state machine implementation only. Do not call directly.
	FSMEvent stAskServerToAssignID();
	FSMEvent stWaitForPeerAddress();
	FSMEvent stUpdateAddress();
	FSMEvent stLocalConnectToPeer();
	FSMEvent stRemoteConnectToPeer();
	FSMEvent stConnected();
};

}

#endif
