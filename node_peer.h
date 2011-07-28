//
//  node_peer.h
//  Expresso-Node
//
//  Created by zac on 7/27/11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#ifndef Expresso_Node_node_peer_h
#define Expresso_Node_node_peer_h

#include "endpoint.h"
#include "fsm.h"

namespace holepoke {

class NodePeer: public Endpoint
{
private:
    FiniteStateMachine _machine;
	struct timeval _kRetryInterval;
    std::string node_id;
    std::string remote_node_id;
    
public:
    NodePeer(const struct sockaddr* serverAddr, socklen_t serverAddrLen, std::string id, std::string remote_id);
    
    void connectToRemoteNode();
    
    // State routines are public but are for state machine implementation only. Do not call directly.
	FSMEvent stRegisterWithServer();
	FSMEvent stWaitForConnect();
	FSMEvent stLocalConnectToPeer();
	FSMEvent stRemoteConnectToPeer();
	FSMEvent stConnected();
};

}

#endif
