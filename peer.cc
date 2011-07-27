#include <iostream>
#include "sender.h"
#include "receiver.h"
#include "network.h"
#include <unistd.h>
#include <stdio.h>

using namespace std;

class MySenderDelegate : public holepoke::SenderDelegate
{
public:
	void gotID(const std::string & identifier) {
		cout << "Identifier: " << identifier << endl;
	}
};

class MyReceiverDelegate : public holepoke::ReceiverDelegate
{
public:
	void invalidID() {
		cout << "Invalid ID" << endl;
	}
};

int main(int argc, char** argv)
{
	// Verify that the version of the library that we linked against is
	// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	
	try
	{
	
		if ( argc != 3 && argc != 4 )
		{
			cerr << "Invalid number of arguments. Usage: peer <holepoke_server_ip> <holepoke_port> [other_peer_identifier]" << endl;
			exit(1);
		}
	
		const char* holepokeIPAddressString = argv[1];
		const char* holepokePortString = argv[2];
		const char* otherPeerIdentifier = argc > 3 ? argv[3] : NULL;
	
		struct sockaddr_storage saddrHolepokeStorage;
		socklen_t saddrHolepokeLen = sizeof(saddrHolepokeStorage);
		struct sockaddr* saddrHolepoke = (struct sockaddr*)&saddrHolepokeStorage;
	
		if ( !network::MakeSocketAddress(holepokeIPAddressString, holepokePortString, saddrHolepoke, &saddrHolepokeLen) )
		{
			fprintf(stderr, "Error making socket address for peer.\n");
			exit(1);
		}
	
		int sock = -1;
	
		if ( otherPeerIdentifier )
		{
			// I'm a receiver.
			MyReceiverDelegate receiverDelegate;
			holepoke::Receiver receiver(saddrHolepoke, saddrHolepokeLen);
			receiver.setDelegate(&receiverDelegate);
			receiver.connectToSender(otherPeerIdentifier);
			sock = receiver.takeSocket();
		
			cout << "Receiver got peer address? " << receiver.hasPeerAddress() << endl;
		}
		else
		{
			// I'm a sender
			MySenderDelegate senderDelegate;
			holepoke::Sender sender(saddrHolepoke, saddrHolepokeLen);
			sender.setDelegate(&senderDelegate);
			sender.connectToReceiver();
			sock = sender.takeSocket();
		
			cout << "Sender got peer address? " << sender.hasPeerAddress() << endl;
		}
	
		cout << "Took socket " << sock << " from the endpoint class." << endl;
	
		close(sock);
	
		cout << "Completed peer to peer transaction" << endl;
	}
	catch(std::exception & e)
	{
		cerr << "Caught std::exception: " << e.what() << endl;
	}
	catch(...)
	{
		cerr << "Unknown exception caught" << endl;
	}
	
	return 0;
}
