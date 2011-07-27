#include "sender.h"
#include "network.h"
#include "holepoke.pb.h"
#include <stdexcept>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static holepoke::FSMEvent stAskServerToAssignID(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stAskServerToAssignID();
}

static holepoke::FSMEvent stWaitForPeerAddress(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stWaitForPeerAddress();
}

static holepoke::FSMEvent stUpdateAddress(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stUpdateAddress();
}

static holepoke::FSMEvent stLocalConnectToPeer(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stLocalConnectToPeer();
}

static holepoke::FSMEvent stRemoteConnectToPeer(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stRemoteConnectToPeer();
}

static holepoke::FSMEvent stConnected(void* userInfo)
{
	return ((holepoke::Sender*)userInfo)->stConnected();
}

namespace holepoke {

static const FSMEvent kTimeoutEvent = 1;
static const FSMEvent kGotIDEvent = 2;
static const FSMEvent kGotPeerAddressEvent = 3;
static const FSMEvent kUpdateAddressACKEvent = 4;
static const FSMEvent kPeerResponseEvent = 5;
static const FSMEvent kStopMachineEvent = 6;
static const FSMEvent kErrorEvent = 7;

Sender::Sender(const struct sockaddr* serverAddr, socklen_t serverAddrLen)
	: Endpoint(serverAddr, serverAddrLen)
{	
	// Use a 3 second retry interval
	_kRetryInterval.tv_sec = 3;
	_kRetryInterval.tv_usec = 0;
	
	//
	// Setup the state machine
	//
	_machine.setUserInfo(this);
	
	FSMState askServerToAssignID = _machine.addState(::stAskServerToAssignID); // First state, will be called first when state machine run.
	FSMState waitForPeerAddress = _machine.addState(::stWaitForPeerAddress);
	FSMState updateAddress = _machine.addState(::stUpdateAddress);
	FSMState localConnectToPeer = _machine.addState(::stLocalConnectToPeer);
//	FSMState localConnectToPeerRetry = _machine.addState(::stLocalConnectToPeer);
	FSMState remoteConnectToPeer = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry1 = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry2 = _machine.addState(::stRemoteConnectToPeer);
	FSMState remoteConnectToPeerRetry3 = _machine.addState(::stRemoteConnectToPeer);
	FSMState connected = _machine.addState(::stConnected);
	
	_machine.addTransition(askServerToAssignID, kTimeoutEvent, askServerToAssignID);
	_machine.addTransition(askServerToAssignID, kGotIDEvent, waitForPeerAddress);
	
	_machine.addTransition(updateAddress, kTimeoutEvent, updateAddress);
	_machine.addTransition(updateAddress, kUpdateAddressACKEvent, waitForPeerAddress);
	_machine.addTransition(updateAddress, kErrorEvent, askServerToAssignID);
	
	_machine.addTransition(waitForPeerAddress, kTimeoutEvent, updateAddress);
	_machine.addTransition(waitForPeerAddress, kGotPeerAddressEvent, localConnectToPeer);
	
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
	
	_machine.addTransition(remoteConnectToPeerRetry3, kTimeoutEvent, askServerToAssignID);
	_machine.addTransition(remoteConnectToPeerRetry3, kPeerResponseEvent, connected);
	
	_machine.addTransition(connected, kStopMachineEvent, kFSMStopState);
}

void Sender::setDelegate(SenderDelegate* delegate)
{
	_delegate = delegate;
}

// Block until the Sender is connected to a peer or an error occurred.
void Sender::connectToReceiver()
{
	_machine.run();
}

//
// State Routines
//

FSMEvent Sender::stAskServerToAssignID()
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
	request.set_type(holepoke::Request::REGISTER);
	holepoke::LocalAddress *local_address = request.mutable_local_address();
	local_address->set_port(network::PortFromSockaddr((struct sockaddr*)&_localAddress));
	std::string localAddrStr;
	if ( !network::AddressStringFromSockaddr((struct sockaddr*)&_localAddress, localAddrStr) )
	{
		fprintf(stderr, "Error converting server address to string.\n");
		exit(1);
	}
	local_address->set_address(localAddrStr);
	
	if ( !network::ProtoBufSendTo(_sock, request, serverSockaddr(), serverSockaddrLen()) )
	{
		throw std::logic_error("Invalid request in stAskServerToAssignID");
	}
	
	bool repeat = true;
	while (repeat)
	{
		repeat = false;
		
		struct timeval timeout = _kRetryInterval;
		holepoke::Response response;
		
		struct sockaddr_in senderAddr;
		socklen_t senderAddrLen = sizeof(senderAddr);
		struct sockaddr* senderSockAddr = (struct sockaddr*)&senderAddr;
		
		if ( network::ProtoBufRecvFromWithTimeout(_sock, response, senderSockAddr, &senderAddrLen, &timeout) )
		{
			struct sockaddr_in* serverAddr = (struct sockaddr_in*)serverSockaddr();
			
			if (senderAddr.sin_addr.s_addr == serverAddr->sin_addr.s_addr && senderAddr.sin_port == serverAddr->sin_port)
			{
				if ( response.type() == holepoke::Response::REGISTER )
				{
					if ( response.has_hole_id() )
					{
						_identifier = response.hole_id().id();
					
						if ( _delegate )
						{
							_delegate->gotID(_identifier);
						}
					
						return kGotIDEvent;
					}
					else
					{
						fprintf(stderr, "Got response but it didn't have a hole ID. Treating like a timeout.\n");
					}
				}
				else
				{
					fprintf(stderr, "Expected REGISTER response but got %d. Treating like a timeout.\n", response.type());
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

FSMEvent Sender::stUpdateAddress()
{
	// timeout occurred. Send a heartbeat.
	holepoke::Request request;
	request.set_type(holepoke::Request::HEARTBEAT);
	holepoke::HoleID *holeID = request.mutable_hole_id();
	holeID->set_id(_identifier);
	
	holepoke::LocalAddress *local_address = request.mutable_local_address();
	local_address->set_port(network::PortFromSockaddr((struct sockaddr*)&_localAddress));
	std::string localAddrStr;
	if ( !network::AddressStringFromSockaddr((struct sockaddr*)&_localAddress, localAddrStr) )
	{
		fprintf(stderr, "Error converting server address to string.\n");
		exit(1);
	}
	local_address->set_address(localAddrStr);	
	
	if ( !network::ProtoBufSendTo(_sock, request, serverSockaddr(), serverSockaddrLen()) )
	{
		throw std::logic_error("Error sending HEARTBEAT request");
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
				if ( response.type() == Response::HEARTBEAT )
				{
					if (!response.has_successful() || response.successful())
					{
						return kUpdateAddressACKEvent;
					}
					else
					{
						fprintf(stderr, "Heartbeat request failed.\n");
						return kErrorEvent;
					}
				}
				else if ( response.type() == holepoke::Response::CONNECT )
				{
					if (updatePeerAddresses(response))
					{
						return kGotPeerAddressEvent;
					}
				}
				else
				{
					fprintf(stderr, "Got something besides a heartbeat response. Treating like a timeout.\n");
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

FSMEvent Sender::stWaitForPeerAddress()
{
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
					fprintf(stderr, "Expected a CONNECT but got something else: %d. Treating like a timeout.\n", response.type());
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

FSMEvent Sender::stLocalConnectToPeer()
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
	
FSMEvent Sender::stRemoteConnectToPeer()
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

FSMEvent Sender::stConnected()
{
	return kStopMachineEvent;
}

} // namespace holepoke
