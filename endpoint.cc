#include "endpoint.h"
#include <assert.h>
#include <string.h>
#include <sstream>
#include <errno.h>
#include <stdexcept>
#include <stdio.h>
#include <errno.h>
#include "network.h"

#if defined(WIN32)
#include <io.h>
#endif

namespace holepoke {

Endpoint::Endpoint(const struct sockaddr* serverAddr, socklen_t serverAddrLen)
{
	assert(serverAddrLen <= sizeof(_serverAddr));
	
	::memcpy(&_serverAddr, serverAddr, serverAddrLen);
	_serverAddrLen = serverAddrLen;
	
	::memset(&_peerAddress, 0, sizeof(_peerAddress));
	_peerAddressLen = 0;
	
	_sock = socket(serverSockaddr()->sa_family, SOCK_DGRAM, 0);
	
	// Figuring out local IP address
	connect(_sock, serverSockaddr(), serverSockaddrLen());
	struct sockaddr* localSockAddr = (struct sockaddr*)&_localAddress;
	_localAddressLen = sizeof(_localAddress);
	if (getsockname(_sock, localSockAddr, &_localAddressLen) == -1)
	{
		fprintf(stderr, "Error getting local address.\n");
		exit(1);
	}
	
	std::string localAddrStr;
	if ( !network::AddressStringFromSockaddr(localSockAddr, localAddrStr) )
	{
		fprintf(stderr, "Error converting local address to string.\n");
		exit(1);
	}
	//fprintf(stderr, "Local IP address is: %s\n", localAddrStr.c_str());
	
#ifdef WIN32
	closesocket(_sock);
#else
	close(_sock);
#endif
	
	// Reopen socket
	_sock = socket(serverSockaddr()->sa_family, SOCK_DGRAM, 0);
	
	if ( _sock == -1 )
	{
		std::stringstream str;
		str << "Couldn't create socket. " << errno << ": " << strerror(errno);
		throw std::runtime_error(str.str());
	}
	
	is_connected = false;
}

Endpoint::~Endpoint()
{
	if ( _sock != -1 )
	{
		close(_sock);
	}
}

bool Endpoint::hasPeerAddress() const
{
	return _peerAddressLen > 0;
}

bool Endpoint::connectedLocally() const
{
	return connected_locally;
}

bool Endpoint::isConnected() const
{
	return is_connected;
}
	
void Endpoint::getPeerAddress(struct sockaddr* saddr, socklen_t* len)
{
	if ( connected_locally )
	{
		if ( _peerLocalAddressLen <= *len )
		{
			::memcpy(saddr, &_peerLocalAddress, _peerLocalAddressLen);
			*len = _peerLocalAddressLen;
		}		
	}
	else
	{
		if ( _peerAddressLen <= *len )
		{
			::memcpy(saddr, &_peerAddress, _peerAddressLen);
			*len = _peerAddressLen;
		}
	}
}
	
bool Endpoint::updatePeerAddresses(holepoke::Response response)
{
	const holepoke::HoleAddress& holeAddress = response.hole_address();
	_peerAddressLen = sizeof(_peerAddress);
	
	if ( network::MakeSocketAddress(holeAddress.address().c_str(), holeAddress.port(), (struct sockaddr*)&_peerAddress, &_peerAddressLen) )
	{
		const holepoke::LocalAddress& localAddress = response.local_address();
		_peerLocalAddressLen = sizeof(_peerLocalAddress);
		
		if ( network::MakeSocketAddress(localAddress.address().c_str(), localAddress.port(), (struct sockaddr*)&_peerLocalAddress, &_peerLocalAddressLen) )
		{
			return true;
		}
		else
		{
			fprintf(stderr, "Couldn't make socket address for peer. Treating like a timeout.\n");
			_peerLocalAddressLen = 0;
		}
	}
	else
	{
		fprintf(stderr, "Couldn't make socket address for peer. Treating like a timeout.\n");
		_peerAddressLen = 0;
	}
	
	return false;
}	

int Endpoint::takeSocket()
{
	int result = _sock;
	_sock = -1;
	return result;
}

}
