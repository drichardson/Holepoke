#ifndef HOLEPOKE_ENDPOINT_H
#define HOLEPOKE_ENDPOINT_H

#include <string>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#endif

#include "holepoke.pb.h"

namespace holepoke {

class Endpoint
{
	struct sockaddr_storage _serverAddr;
	socklen_t _serverAddrLen;
	
protected:
	int _sock;
	
	bool connected_locally;
	bool is_connected;
	
	struct sockaddr_storage _localAddress;
	socklen_t _localAddressLen;
	
	struct sockaddr_storage _peerAddress;
	socklen_t _peerAddressLen;
	
	struct sockaddr_storage _peerLocalAddress;
	socklen_t _peerLocalAddressLen;
	
	const struct sockaddr* serverSockaddr() const { return (struct sockaddr*)&_serverAddr; }
	socklen_t serverSockaddrLen() const { return _serverAddrLen; }
	
	const struct sockaddr* peerSockaddr() const { return (struct sockaddr*)&_peerAddress; }
	socklen_t peerSockaddrLen() const { return _peerAddressLen; }
	
	const struct sockaddr* peerLocalSockaddr() const { return (struct sockaddr*)&_peerLocalAddress; }
	socklen_t peerLocalSockaddrLen() const { return _peerLocalAddressLen; }
	
	bool updatePeerAddresses(holepoke::Response response);
	
public:
	Endpoint(const struct sockaddr* saddr, socklen_t saddrLen);
	virtual ~Endpoint();
	
	bool hasPeerAddress() const;
	
	bool connectedLocally() const;
	
	bool isConnected() const;
	
	// Get the peer address.
	void getPeerAddress(struct sockaddr* saddr, socklen_t* len);
	
	// Take ownership of the socket. Only valid if isConnected is true.
	int takeSocket();
};

}

#endif
