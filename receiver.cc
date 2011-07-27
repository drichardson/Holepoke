#include "receiver.h"
#include "network.h"
#include "holepoke.pb.h"
#include <stdexcept>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static holepoke::FSMEvent stLookupAddressForID(void* userInfo)
{
	return ((holepoke::Receiver*)userInfo)->stLookupAddressForID();
}

static holepoke::FSMEvent stLocalConnectToPeer(void* userInfo)
{
	return ((holepoke::Receiver*)userInfo)->stLocalConnectToPeer();
}

static holepoke::FSMEvent stRemoteConnectToPeer(void* userInfo)
{
	return ((holepoke::Receiver*)userInfo)->stRemoteConnectToPeer();
}

static holepoke::FSMEvent stConnected(void* userInfo)
{
	return ((holepoke::Receiver*)userInfo)->stConnected();
}

static holepoke::FSMEvent stInvalidID(void* userInfo)
{
	return ((holepoke::Receiver*)userInfo)->stInvalidID();
}

namespace holepoke {

static const FSMEvent kTimeoutEvent = 1;
static const FSMEvent kGotPeerAddressEvent = 2;
static const FSMEvent kInvalidIDEvent = 3;
static const FSMEvent kPeerResponseEvent = 4;
static const FSMEvent kStopMachineEvent = 5;

Receiver::Receiver(const struct sockaddr* serverAddr, socklen_t serverAddrLen)
	: Endpoint(serverAddr, serverAddrLen)
{	
	// Use a 3 second retry interval
	_kRetryInterval.tv_sec = 3;
	_kRetryInterval.tv_usec = 0;
	
	//
	// Setup the state machine
	//
	_machine.setUserInfo(this);
	
	FSMState lookupAddressForID = _machine.addState(::stLookupAddressForID); // First state run as soon as machine is run.
	FSMState localConnectToPeer = _machine.addState(::stLocalConnectToPeer);
//	FSMState localConnectToPeerRetry = _machine.addState(::stLocalConnectToPeer);
	FSMState remoteConnectToPeer = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry1 = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry2 = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry3 = _machine.addState(::stRemoteConnectToPeer);
	FSMState connected = _machine.addState(::stConnected);
	FSMState invalidID = _machine.addState(::stInvalidID);
	
	_machine.addTransition(lookupAddressForID, kGotPeerAddressEvent, localConnectToPeer);
	_machine.addTransition(lookupAddressForID, kTimeoutEvent, lookupAddressForID);
	_machine.addTransition(lookupAddressForID, kInvalidIDEvent, invalidID);
	
	_machine.addTransition(localConnectToPeer, kTimeoutEvent, remoteConnectToPeer);//localConnectToPeerRetry);
	_machine.addTransition(localConnectToPeer, kPeerResponseEvent, connected);
	
//	_machine.addTransition(localConnectToPeerRetry, kTimeoutEvent, remoteConnectToPeer);
//	_machine.addTransition(localConnectToPeerRetry, kPeerResponseEvent, connected);
	
	_machine.addTransition(remoteConnectToPeer, kTimeoutEvent, remoteConnectToPeerRetry1);
	_machine.addTransition(remoteConnectToPeer, kPeerResponseEvent, connected);
	
	_machine.addTransition(remoteConnectToPeerRetry1, kTimeoutEvent, remoteConnectToPeerRetry2);
	_machine.addTransition(remoteConnectToPeerRetry1, kPeerResponseEvent, connected);
	
	_machine.addTransition(remoteConnectToPeerRetry2, kTimeoutEvent, remoteConnectToPeerRetry3);
	_machine.addTransition(remoteConnectToPeerRetry2, kPeerResponseEvent, connected);
	
	_machine.addTransition(remoteConnectToPeerRetry3, kTimeoutEvent, kFSMStopState);
	_machine.addTransition(remoteConnectToPeerRetry3, kPeerResponseEvent, connected);	
	
	_machine.addTransition(connected, kStopMachineEvent, kFSMStopState);
	
	_machine.addTransition(invalidID, kStopMachineEvent, kFSMStopState);
}

void Receiver::setDelegate(ReceiverDelegate* delegate)
{
	_delegate = delegate;
}

// Block until the Receiver is connected to a peer or an error occurred.
void Receiver::connectToSender(const std::string & senderID)
{
	_senderID = senderID;
	_machine.run();
}

//
// State Routines
//

FSMEvent Receiver::stLookupAddressForID()
{
	// Send HELLO to get port assignment
	holepoke::Request hello_request;
	hello_request.set_type(holepoke::Request::HELLO);
	
	if ( !network::ProtoBufSendTo(_sock, hello_request, serverSockaddr(), serverSockaddrLen()) )
	{
		throw std::logic_error("Invalid request in stAskServerToAssignID");
	}
	
	// Need to get updated port number after HELLO
	struct sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);
	struct sockaddr* localSockAddr = (struct sockaddr*)&localAddr;
	
	if (getsockname(_sock, localSockAddr, &addrLen) == -1)
	{
		fprintf(stderr, "Error getting local socket info.\n");
		exit(1);
	}
	((struct sockaddr_in*)&_localAddress)->sin_port = htons(network::PortFromSockaddr(localSockAddr));
	
	holepoke::Request request;
	request.set_type(holepoke::Request::CONNECT);
	holepoke::HoleID* holeID = request.mutable_hole_id();
	holeID->set_id(_senderID);
	
	holepoke::LocalAddress *local_address = request.mutable_local_address();
	local_address->set_port(network::PortFromSockaddr((struct sockaddr*)&_localAddress));
	std::string localAddrStr;
	if ( !network::AddressStringFromSockaddr((struct sockaddr*)&_localAddress, localAddrStr) )
	{
		fprintf(stderr, "Error converting local address to string.\n");
		exit(1);
	}
	local_address->set_address(localAddrStr);
	
	if ( !network::ProtoBufSendTo( _sock, request, serverSockaddr(), serverSockaddrLen() ) )
	{
		throw std::logic_error("Invalid request in stAskServerToLookupAddressForID");
	}
	
	bool repeat = true;
	while (repeat)
	{
		repeat = false;
	
		holepoke::Response response;
		struct timeval timeout = _kRetryInterval;
		
		struct sockaddr_in senderAddr;
		socklen_t senderAddrLen = sizeof(senderAddr);
		struct sockaddr* senderSockAddr = (struct sockaddr*)&senderAddr;
		
		if ( network::ProtoBufRecvFromWithTimeout(_sock, response, senderSockAddr, &senderAddrLen, &timeout) )
		{
			struct sockaddr_in* serverAddr = (struct sockaddr_in*)serverSockaddr();
			
			if (senderAddr.sin_addr.s_addr == serverAddr->sin_addr.s_addr && senderAddr.sin_port == serverAddr->sin_port)
			{
				if ( response.type() == holepoke::Response::CONNECT )
				{
					if (updatePeerAddresses(response))
					{
						return kGotPeerAddressEvent;
					}
				}
				else
				{
					fprintf(stderr, "Expected CONNECT response but got a %d.\n", response.type());
				}
			}
			else
			{
				repeat = true;
				fprintf(stderr, "Received response from address that doesn't match server. Ignoring.\n");
			}
		}
	}
	
	return kTimeoutEvent;
}

FSMEvent Receiver::stLocalConnectToPeer()
{
	holepoke::Request helloRequest;
	helloRequest.set_type(holepoke::Request::HELLO);
	if ( !network::ProtoBufSendTo(_sock, helloRequest, (struct sockaddr*)&_peerLocalAddress, _peerLocalAddressLen) )
	{
		throw std::logic_error("Error sending HELLO request");
	}
	
	struct timeval timeout = _kRetryInterval;
	
	bool repeat = true;
	while (repeat)
	{
		repeat = false;
		
		struct sockaddr_in senderAddr;
		socklen_t senderAddrLen = sizeof(senderAddr);
		struct sockaddr* senderSockAddr = (struct sockaddr*)&senderAddr;
		
		if ( network::ProtoBufRecvFromWithTimeout(_sock, helloRequest, senderSockAddr, &senderAddrLen, &timeout) )
		{
			struct sockaddr_in* peerAddr = (struct sockaddr_in*)&_peerLocalAddress;
			
			if (senderAddr.sin_addr.s_addr == peerAddr->sin_addr.s_addr && senderAddr.sin_port == peerAddr->sin_port)
			{
				if ( helloRequest.type() == holepoke::Request::HELLO )
				{
					holepoke::Request helloRequest;
					helloRequest.set_type(holepoke::Request::HELLO);
					if ( !network::ProtoBufSendTo(_sock, helloRequest, (struct sockaddr*)&_peerLocalAddress, _peerLocalAddressLen) )
					{
						throw std::logic_error("Error sending HELLO request");
					}
					
					connected_locally = true;
					is_connected = true;
					//printf("Connected with peer on local address.\n");
					return kPeerResponseEvent;
				}
				else
				{
					fprintf(stderr, "Got something besides a peer response.\n");
				}
			}
			else
			{
				repeat = true;
				fprintf(stderr, "Received response from address that doesn't match peer. Ignoring.\n");
			}
		}
	}
	
	return kTimeoutEvent;
}

FSMEvent Receiver::stRemoteConnectToPeer()
{
	holepoke::Request helloRequest;
	helloRequest.set_type(holepoke::Request::HELLO);

	if ( !network::ProtoBufSendTo(_sock, helloRequest, (struct sockaddr*)&_peerAddress, _peerAddressLen) )
	{
		throw std::logic_error("Error sending HELLO request");
	}
	
	struct timeval timeout = _kRetryInterval;
	
	bool repeat = true;
	while (repeat)
	{
		repeat = false;
		
		struct sockaddr_in senderAddr;
		socklen_t senderAddrLen = sizeof(senderAddr);
		struct sockaddr* senderSockAddr = (struct sockaddr*)&senderAddr;
		
		if ( network::ProtoBufRecvFromWithTimeout(_sock, helloRequest, senderSockAddr, &senderAddrLen, &timeout) )
		{
			struct sockaddr_in* peerAddr = (struct sockaddr_in*)&_peerAddress;
			
			if (senderAddr.sin_addr.s_addr == peerAddr->sin_addr.s_addr && senderAddr.sin_port == peerAddr->sin_port)
			{
				if ( helloRequest.type() == holepoke::Request::HELLO )
				{
					connected_locally = false;
					is_connected = true;
					return kPeerResponseEvent;
				}
				else
				{
					fprintf(stderr, "Got something besides a peer response.\n");
				}
			}
			else
			{
				repeat = true;
				fprintf(stderr, "Received response from address that doesn't match peer. Ignoring.\n");
			}
		}
	}
	
	return kTimeoutEvent;
}

FSMEvent Receiver::stConnected()
{
	return kStopMachineEvent;
}

FSMEvent Receiver::stInvalidID()
{
	if ( _delegate )
	{
		_delegate->invalidID();
	}
	
	return kStopMachineEvent;
}

} // namespace holepoke
