#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "network.h"
#include "holepoke.pb.h"
#include "peertopeer.pb.h"

int main(int argc, char** argv)
{
	// Verify that the version of the library that we linked against is
	// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	if ( argc != 4 )
	{
		fprintf(stderr, "Invalid number of arguments. Usage: test_client <IP address> <port> <ID>\n");
		exit(1);
	}
	
	const char* holepokeIPAddressString = argv[1];
	const char* holepokePortString = argv[2];
	const char* serverID = argv[3];
	
	struct sockaddr_storage ssHolepokeServer;
	socklen_t saddrHolepokeServerLen = sizeof(ssHolepokeServer);
	struct sockaddr* saddrHolepokeServer = (struct sockaddr*)&ssHolepokeServer;
	
	if ( !network::MakeSocketAddress(holepokeIPAddressString, holepokePortString, saddrHolepokeServer, &saddrHolepokeServerLen) )
	{
		fprintf(stderr, "Couldn't make socket address.\n");
		exit(1);
	}
	
	int sock = socket(saddrHolepokeServer->sa_family, SOCK_DGRAM, 0);
	
	if ( sock == -1 )
	{
		fprintf(stderr, "Error opening connected UDP socket\n");
		exit(1);
	}
	
	holepoke::Request request;
	request.set_type(holepoke::Request::CONNECT);
	holepoke::HoleID* holeID = request.mutable_hole_id();
	holeID->set_id(serverID);
	
	if ( !network::ProtoBufSendTo( sock, request, saddrHolepokeServer, saddrHolepokeServerLen ) )
	{
		fprintf(stderr, "Error sending lookup request\n");
		exit(1);
	}
	
	holepoke::Response response;
	
	if ( !network::ProtoBufRecvFrom( sock, response, NULL, 0 ) )
	{
		fprintf(stderr, "Error receiving response.\n");
		exit(1);
	}
	
	if ( !response.type() == holepoke::Response::CONNECT )
	{
		fprintf(stderr, "Expected connect response but got a %d.\n", response.type());
		exit(1);
	}
	
	if ( !response.has_hole_address() )
	{
		fprintf(stderr, "Response doesn't have a hole address.\n");
		exit(1);
	}
	
	const holepoke::HoleAddress & addr = response.hole_address();
	printf("Hole address is %s on port %d\n", addr.address().c_str(), addr.port());
	
	struct sockaddr_storage saddrPeerStorage;
	socklen_t saddrPeerLen = sizeof(saddrPeerStorage);
	struct sockaddr* saddrPeer = (struct sockaddr*)&saddrPeerStorage;
	
	if ( !network::MakeSocketAddress(addr.address().c_str(), addr.port(), saddrPeer, &saddrPeerLen) )
	{
		fprintf(stderr, "Error making socket address for peer.");
		exit(1);
	}
	
	peertopeer::Message message;
	message.set_type(peertopeer::Message::HELLO);
	
	if ( !network::ProtoBufSendTo(sock, message, saddrPeer, saddrPeerLen) )
	{
		fprintf(stderr, "Error sending hello message to server\n");
		exit(1);
	}
	
	message.Clear();
	if ( !network::ProtoBufRecvFrom(sock, message, NULL, 0) )
	{
		fprintf(stderr, "Error receiving hello message response from server.\n");
		exit(1);
	}
	
	printf("Successful spoke with server.\n");
	
	return 0;
}
