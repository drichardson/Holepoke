// holepoke - main.c
// Listen for connections from clients and give them back a URL. This URL can be e-mailed to someone
// else who uses it to connect on their side.
//
// 1. Sender to holepoke
// Sender sends newxfer packet to holepoke. holepoke responds with a URL.
//
// When the receiver uses the URL, the URL has enough information to tell the receiver where
// the sender is, so it doesn't need to talk to holepoke.

#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "holepoke.pb.h"
#include <map>
#include <sstream>
#include <iostream>
#include "network.h"
#include "uuid.h"

struct server_address {
	struct sockaddr_storage holeaddrstorage;
	socklen_t holesocklen;
	struct sockaddr_storage localaddrstorage;
	socklen_t localsocklen;
};

int main(int argc, const char** argv)
{
	// Verify that the version of the library that we linked against is
	// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	if ( argc != 2 )
	{
		fprintf(stderr, "Expected port but got none.\nUsage: holepoke <port>\n");
		exit(1);
	}
	
	char* endptr = NULL;
	const char* portStr = argv[1];
	long portLong = strtol(portStr, &endptr, 10);
	
	if ( endptr == NULL || *endptr != '\0' )
	{
		fprintf(stderr, "Invalid port string %s\n", portStr);
		exit(1);
	}
	
	in_port_t port = portLong;
	
	if ( port != portLong )
	{
		fprintf(stderr, "Port is out of range. Tried to use %ld but got %d.\n", portLong, port);
		exit(1);
	}
	
	// Setup the single UDP socket for receiving requests and sending responses.
	
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	
	if ( sock == -1 )
	{
		fprintf(stderr, "Error creating socket descriptor. %d: %s", errno, strerror(errno));
		exit(1);
	}
	
	struct sockaddr_in myAddr;
	myAddr.sin_port = htons(port);
	myAddr.sin_addr.s_addr = INADDR_ANY;
	myAddr.sin_family = AF_INET;
#ifndef __linux__
	myAddr.sin_len = sizeof(myAddr);
#endif
	
	int err;
	err = bind(sock, (struct sockaddr*)&myAddr, sizeof(myAddr));
	
	if ( err != 0 )
	{
		fprintf(stderr, "Couldn't bind socket to name. %d: %s\n", errno, strerror(errno));
		exit(1);
	}
	
	typedef std::map<std::string, struct server_address> IDToAddressMap;
	IDToAddressMap idToAddressMap;

	while(1)
	{
		struct sockaddr_storage senderAddrStorage;
		struct sockaddr* senderAddr = (struct sockaddr*)&senderAddrStorage;
		socklen_t senderAddrLen = sizeof(senderAddrStorage);
		
		holepoke::Request request;
		if ( !network::ProtoBufRecvFrom(sock, request, senderAddr, &senderAddrLen) )
		{
			fprintf(stderr, "Error receiving protocol buffer\n");
			continue;
		}
		
		bool sendResponse = false;
		holepoke::Response response;
		
		switch( request.type() )
		{
			case holepoke::Request::REGISTER:
			{
				std::string uuid;
				if ( holepoke::GetUUID(uuid) && request.has_local_address() )
				{
					holepoke::LocalAddress local_address = request.local_address();
					struct sockaddr_storage localAddrStorage;
					socklen_t localAddrLen = sizeof(localAddrStorage);
					struct sockaddr* localAddr = (struct sockaddr*)&localAddrStorage;
				
					if ( !network::MakeSocketAddress(local_address.address().c_str(), local_address.port(), localAddr, &localAddrLen) )
					{
						fprintf(stderr, "Error making socket address for peer.\n");
						exit(1);
					}
					
					struct server_address serverAddress = { senderAddrStorage, senderAddrLen, localAddrStorage, localAddrLen };
					
					idToAddressMap[uuid] = serverAddress;					
					response.set_type(holepoke::Response::REGISTER);
					holepoke::HoleID *holeID = response.mutable_hole_id();
					holeID->set_id(uuid);
					sendResponse = true;
				}
				else
				{
					fprintf(stderr, "Error registering offer.\n");
				}
				
				break;
			}
			case holepoke::Request::HEARTBEAT:
			{
				if ( !request.has_hole_id() )
				{
					fprintf(stderr, "Heartbeat request missing hole ID\n");
					break;
				}
				
				IDToAddressMap::const_iterator itr = idToAddressMap.find(request.hole_id().id());
				
				if ( itr == idToAddressMap.end() )
				{
					fprintf(stderr, "Couldn't find address for the given ID for heartbeat: %s\n", request.hole_id().id().c_str());
					
					response.set_type(holepoke::Response::HEARTBEAT);
					response.set_successful(false);
					sendResponse = true;
				}
				else
				{
					printf("Updating hole ID %s with port %d\n", request.hole_id().id().c_str(), network::PortFromSockaddr((struct sockaddr*)&senderAddrStorage));

					holepoke::LocalAddress local_address = request.local_address();
					struct sockaddr_storage localAddrStorage;
					socklen_t localAddrLen = sizeof(localAddrStorage);
					struct sockaddr* localAddr = (struct sockaddr*)&localAddrStorage;
					
					if ( !network::MakeSocketAddress(local_address.address().c_str(), local_address.port(), localAddr, &localAddrLen) )
					{
						fprintf(stderr, "Error making socket address for peer.\n");
						exit(1);
					}
					
					struct server_address serverAddress = { senderAddrStorage, senderAddrLen, localAddrStorage, localAddrLen };					
					idToAddressMap[request.hole_id().id()] = serverAddress;
					
					response.set_type(holepoke::Response::HEARTBEAT);
					response.set_successful(true);
					sendResponse = true;
				}
				
				break;
			}
			case holepoke::Request::CONNECT:
			{
				if ( !request.has_hole_id() || !request.has_local_address() )
				{
					fprintf(stderr, "Lookup request missing needed info\n");
					break;
				}
				
				sendResponse = true;
				IDToAddressMap::const_iterator itr = idToAddressMap.find(request.hole_id().id());
				response.set_type(holepoke::Response::CONNECT);
				
				if ( itr == idToAddressMap.end() )
				{
					// Send CONNECT response with empty address
					fprintf(stderr, "Couldn't find address for the given ID: %s\n", request.hole_id().id().c_str());
				}
				else
				{
					// Make a CONNECT response for the client that has the stored server address.
					const struct server_address & serverAddress = itr->second;
					
					const struct sockaddr* saddrServer = (const struct sockaddr*)&serverAddress.holeaddrstorage;
					holepoke::HoleAddress *senderHoleAddress = response.mutable_hole_address();
					senderHoleAddress->set_port(network::PortFromSockaddr(saddrServer));
				
					std::string addrStr;
					if ( !network::AddressStringFromSockaddr(saddrServer, addrStr) )
					{
						fprintf(stderr, "Error converting server address to string.\n");
						exit(1);
					}
					senderHoleAddress->set_address(addrStr);
					
					const struct sockaddr* saddrLocal = (const struct sockaddr*)&serverAddress.localaddrstorage;
					holepoke::LocalAddress *senderLocalAddress = response.mutable_local_address();
					senderLocalAddress->set_port(network::PortFromSockaddr(saddrLocal));
					
					std::string localAddrStr;
					if ( !network::AddressStringFromSockaddr(saddrLocal, localAddrStr) )
					{
						fprintf(stderr, "Error converting local address to string.\n");
						exit(1);
					}
					senderLocalAddress->set_address(localAddrStr);
					
					
					// Tell server the client wants to connect.
					holepoke::Response connectResponse;
					connectResponse.set_type(holepoke::Response::CONNECT);
					holepoke::HoleAddress *receiverHoleAddress = response.mutable_hole_address();
					receiverHoleAddress = connectResponse.mutable_hole_address();
					receiverHoleAddress->set_port(network::PortFromSockaddr(senderAddr));
					if ( !network::AddressStringFromSockaddr( senderAddr, addrStr ) )
					{
						fprintf(stderr, "Error converting clients address to string\n");
						exit(1);
					}
					receiverHoleAddress->set_address(addrStr);
					
					holepoke::LocalAddress local_address = request.local_address();
					holepoke::LocalAddress *receiverLocalAddress = response.mutable_local_address();
					receiverLocalAddress = connectResponse.mutable_local_address();
					receiverLocalAddress->set_address(local_address.address());
					receiverLocalAddress->set_port(local_address.port());
					
					if ( !connectResponse.IsInitialized() )
					{
						std::string errstr = response.InitializationErrorString();
						fprintf(stderr, "Server connect response buffer isn't completely initialized. Error: %s\n", errstr.c_str());
						exit(1);
					}
					printf("Sending connect response to server.\n");
					if ( !network::ProtoBufSendTo(sock, connectResponse, saddrServer, serverAddress.holesocklen) )
					{
						fprintf(stderr, "Error sending response.\n");
					}
					// ************************
				}
				break;
			}
			case holepoke::Request::HELLO:
			{
				//fprintf(stderr, "Someone sent a HELLO to the server. Why would they do that?\n");
				break;
			}
		}
		
		if ( !sendResponse )
		{
			continue;
		}
		
		if ( !response.IsInitialized() )
		{
			std::string errstr = response.InitializationErrorString();
			fprintf(stderr, "Response buffer isn't completely initialized. Error: %s\n", errstr.c_str());
			continue;
		}
		
		printf("Sending response type %d\n",response.type());
		
		if ( !network::ProtoBufSendTo(sock, response, senderAddr, senderAddrLen) )
		{
			fprintf(stderr, "Error sending response.\n");
		}
	}
	
	return 0;
}
