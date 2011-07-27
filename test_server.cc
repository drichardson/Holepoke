#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/select.h>
#include "network.h"
#include "holepoke.pb.h"
#include "peertopeer.pb.h"

int main(int argc, char** argv)
{
	// Verify that the version of the library that we linked against is
	// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	if ( argc != 3 )
	{
		fprintf(stderr, "Invalid number of arguments. Usage: test_server <IP address> <port>\n");
		exit(1);
	}
	
	const char* holepokeIPAddressString = argv[1];
	const char* holepokePortString = argv[2];
	
	struct sockaddr_storage saddrHolepokeStorage;
	socklen_t saddrHolepokeLen = sizeof(saddrHolepokeStorage);
	struct sockaddr* saddrHolepoke = (struct sockaddr*)&saddrHolepokeStorage;
	
	if ( !network::MakeSocketAddress(holepokeIPAddressString, holepokePortString, saddrHolepoke, &saddrHolepokeLen) )
	{
		fprintf(stderr, "Error making socket address for peer.\n");
		exit(1);
	}

	int sock = socket(saddrHolepoke->sa_family, SOCK_DGRAM, 0);
	
	if ( sock == -1 )
	{
		fprintf(stderr, "Error creating socket. %d: %s\n", errno, strerror(errno));
		exit(1);
	}
	
	holepoke::Request request;
	request.set_type(holepoke::Request::REGISTER);
	
	if ( !network::ProtoBufSendTo(sock, request, saddrHolepoke, saddrHolepokeLen) )
	{
		fprintf(stderr, "Error sending REGISTER request\n");
		exit(1);
	}
	
	holepoke::Response response;
	if ( !network::ProtoBufRecvFrom(sock, response, NULL, 0) )
	{
		fprintf(stderr, "Error reading response.\n");
		exit(1);
	}
		
	if ( !response.has_hole_id() )
	{
		fprintf(stderr, "Didn't get hole id in register.\n");
		exit(1);
	}
	
	std::string holeIDStr = response.hole_id().id();
	printf("Got ID: %s. Waiting for peer to connect...\n", holeIDStr.c_str());
	
	struct sockaddr_storage peerAddressStorage;
	struct sockaddr* peerAddress = (struct sockaddr*)&peerAddressStorage;
	socklen_t peerAddressLen = sizeof(peerAddressStorage);
	
	// Start the heartbeat, listen loop. Waiting for a CONNECT from the server to tell me a client is ready to connect.
	bool waitingForConnect = true;
	while (waitingForConnect)
	{
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(sock, &readset);
		struct timeval timeout;
		timeout.tv_sec = 3; // 3 second heartbeat
		timeout.tv_usec = 0;
	
		int result = select(sock + 1, &readset, NULL, NULL, &timeout);
		
		if ( result == 0 )
		{
			// timeout occurred. Send a heartbeat.
			request.Clear();
			request.set_type(holepoke::Request::HEARTBEAT);
			holepoke::HoleID *holeID = request.mutable_hole_id();
			holeID->set_id(holeIDStr);
			
			if ( !network::ProtoBufSendTo(sock, request, saddrHolepoke, saddrHolepokeLen) )
			{
				fprintf(stderr, "Error sending HEARTBEAT request\n");
				exit(1);
			}
		}
		else if ( result > 0 )
		{
			// Got a CONNECT from the server?
			holepoke::Response response;

			struct sockaddr_storage saddrPeerStorage;
			socklen_t saddrPeerLen = sizeof(saddrPeerStorage);
			struct sockaddr* saddrPeer = (struct sockaddr*)&saddrPeerStorage;

			if ( !network::ProtoBufRecvFrom(sock, response, saddrPeer, &saddrPeerLen) )
			{
				fprintf(stderr, "Error receiving server message. Expecting CONNECT.\n");
				exit(1);
			}
			
			if ( response.type() == holepoke::Response::CONNECT )
			{
				const holepoke::HoleAddress& holeAddress = response.hole_address();
				
				printf("Got CONNECT from holepoked. Peer is %s on port %d\n", holeAddress.address().c_str(), holeAddress.port());
				waitingForConnect = false;
				
				if ( !network::MakeSocketAddress(holeAddress.address().c_str(), holeAddress.port(), peerAddress, &peerAddressLen) )
				{
					fprintf(stderr, "Couldn't make socket address for peer.\n");
					exit(1);
				}
			}
		}
		else
		{
			fprintf(stderr, "Select returned of %d. errno is %d: %s\n", result, errno, strerror(errno));
		}
	}
	
	// In the CONNECTed state. Send a HELLO message to peer and then wait for one.
	
	bool waitingForHello = true;
	while (waitingForHello)
	{
		peertopeer::Message message;
		message.set_type(peertopeer::Message::HELLO);

		if ( !network::ProtoBufSendTo(sock, message, peerAddress, peerAddressLen) )
		{
			fprintf(stderr, "Error sending HELLO response to client.\n");
			exit(1);
		}
		
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(sock, &readset);
		struct timeval timeout;
		timeout.tv_sec = 3; // 3 second timeout
		timeout.tv_usec = 0;
	
		int result = select(sock + 1, &readset, NULL, NULL, &timeout);
		
		if ( result == 0 ) // timeout
		{
			// try again
			continue;
		}
		else if ( result < 0 ) // error
		{
			if ( errno == EINTR )
			{
				fprintf(stderr, "Got an interrupt while waiting for HELLO, but I'm ignoring it.\n");
				continue;
			}
				
			fprintf(stderr, "Error waiting for HELLO. %d: %s\n", errno, strerror(errno));
			exit(1);
		}
		
		// We have something to read. See if it's a HELLO.
		peerAddressLen = sizeof(peerAddressStorage);
		message.Clear();
		if ( !network::ProtoBufRecvFrom(sock, message, NULL, NULL) )
		{
			fprintf(stderr, "Error receiving peer message\n");
			exit(1);
		}
		
		if ( message.type() == peertopeer::Message::HELLO )
		{
			waitingForHello = false;
			puts("Got hello from other side");
		}
		else
		{
			fprintf(stderr, "Ignoring unexpected message from peer or server.\n");
		}
	}
	
	printf("Successfully spoke with client.\n");
	
	return 0;
}
