#include "network.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

#if defined(WIN32)
#include <io.h>
#endif

#include <string.h>
#include <errno.h>
#include <string>
#include <sstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <iostream>

#if DEBUGON
#include <ctime>
#endif

namespace network {
	
static const size_t MAX_PACKET_LEN = 1024;

int OpenConnectedUDPSocket(const char* ipAddressString, const char* portString)
{	
	struct sockaddr_storage ss;
	struct sockaddr* saddr = (struct sockaddr*)&ss;
	socklen_t saddrLen = sizeof(ss);
	
	if ( !MakeSocketAddress(ipAddressString, portString, saddr, &saddrLen) )
	{
		fprintf(stderr, "Couldn't make socket address for %s:%s.\n", ipAddressString, portString);
		return -1;
	}
	
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	
	if ( sock == -1 )
	{
		fprintf(stderr, "Couldn't create socket. %d: %s\n", errno, strerror(errno));
		return -1;
	}
	
	int err = connect(sock, saddr, saddrLen);
	
	if ( err == -1 )
	{
		fprintf(stderr, "Couldn't connect to %s:%s\n", ipAddressString, portString);
		close(sock);
		return -1;
	}
	
	return sock;
}

bool MakeSocketAddress(const char* ipAddressString, const char* portString, struct sockaddr* saddrOut, socklen_t* saddrOutLen)
{	
	char* endptr = NULL;
	long portLong = strtol(portString, &endptr, 10);
	
	if ( endptr == NULL || *endptr != '\0' )
	{
		fprintf(stderr, "Error parsing port %s.\n", portString);
		return false;
	}
	
	in_port_t port = portLong;
	
	if ( port != portLong )
	{
		fprintf(stderr, "Port out of range. Given %ld but using %d.\n", portLong, port);
		return false;
	}
	
	return MakeSocketAddress(ipAddressString, port, saddrOut, saddrOutLen);
}

bool MakeSocketAddress(const char* ipAddressString, in_port_t port, struct sockaddr* saddrOut, socklen_t* saddrOutLen)
{
	int err = 0;
	
	if ( strchr(ipAddressString, '.') != NULL && *saddrOutLen >= sizeof(struct sockaddr_in) )
	{
		// Probably is an IPv4 string, since there's a dot in it.
		
		*saddrOutLen = sizeof(struct sockaddr_in);
		struct sockaddr_in* saddr = (struct sockaddr_in*)saddrOut;
		saddr->sin_family = AF_INET;
#if defined(__APPLE__)
		saddr->sin_len = sizeof(struct sockaddr_in);
#endif
		saddr->sin_port = htons(port);
		
		err = inet_pton(AF_INET, ipAddressString, &saddr->sin_addr);
	}
	else if ( *saddrOutLen >= sizeof(struct sockaddr_in6) )
	{
		// It's not a valid IPv4 address, so try IPv6.
		
		*saddrOutLen = sizeof(struct sockaddr_in6);
		struct sockaddr_in6* saddr = (struct sockaddr_in6*)saddrOut;
		saddr->sin6_family = AF_INET6;
#if defined(__APPLE__)
		saddr->sin6_len = sizeof(struct sockaddr_in6);
#endif
		saddr->sin6_port = htons(port);
		
		err = inet_pton(AF_INET6, ipAddressString, &saddr->sin6_addr);
	}
	else
	{
		fprintf(stderr, "Address buffer not big enough.\n");
		return false;
	}
	
	if ( err != 1 )
	{
		if ( err == -1 )
		{
			fprintf(stderr, "Error parsing address %s. %d: %s\n", ipAddressString, errno, strerror(errno));
		}
		else
		{
			fprintf(stderr, "Error parsing address %s.\n", ipAddressString);
		}
		
		return false;
	}
	
	return true;
}

in_port_t PortFromSockaddr(const struct sockaddr* saddr)
{
	in_port_t result = 0;
	
	switch( saddr->sa_family )
	{
		case AF_INET:
		{
			result = ((struct sockaddr_in*)saddr)->sin_port;
			result = ntohs(result);
			break;
		}
			
		case AF_INET6:
		{
			result = ((struct sockaddr_in6*)saddr)->sin6_port;
			result = ntohs(result);
			break;
		}
			
		default:
		{
			fprintf(stderr, "Invalid address family %d\n", saddr->sa_family);
			break;
		}
	}
	
	return result;
}

bool AddressStringFromSockaddr(const struct sockaddr* saddr, std::string & addressOut)
{	
	char addrStrBuf[100];
	const char* addrStr = NULL;
	
	switch( saddr->sa_family )
	{
		case AF_INET:
		{
			addrStr = inet_ntop(AF_INET, &((struct sockaddr_in*)saddr)->sin_addr, addrStrBuf, sizeof(addrStrBuf));
			break;
		}
		
		case AF_INET6:
		{
			addrStr = inet_ntop(AF_INET6, &((struct sockaddr_in6*)saddr)->sin6_addr, addrStrBuf, sizeof(addrStrBuf));
			break;
		}
		
		default:
		{
			fprintf(stderr, "Invalid address family trying to convert to string. %d\n", saddr->sa_family);
			break;
		}
	}
	
	if ( addrStr )
	{
		addressOut = addrStr;
		return true;
	}
	
	return false;
}

bool ProtoBufSendTo(int sock, const google::protobuf::Message& message, const struct sockaddr* saddr, socklen_t saddrLen)
{
	size_t len = message.ByteSize();
	
#if DEBUGON
	time_t curtime;
	time(&curtime);
	fprintf(stdout, "\n%s - Outgoing Message:\n", ctime(&curtime));
	google::protobuf::io::OstreamOutputStream oDebug(&std::cout);
	google::protobuf::TextFormat::Print(message, &oDebug);
#endif
	
	if ( len > MAX_PACKET_LEN )
	{
#if DEBUGON
		fprintf(stderr, "Can't send message because %lu is bigger than the max allowed packet size of %lu\n", len, MAX_PACKET_LEN);
#endif
		return false;
	}
		
	// Write the message to a buffer
	std::stringstream streamSend;
	
	if ( !message.SerializeToOstream(&streamSend) )
	{
#if DEBUGON
		fprintf(stderr, "Couldn't serialize message to ostream.\n");
#endif
		return false;
	}
	
	const std::string & messageMsg = streamSend.rdbuf()->str();
	
#if DEBUGON
	std::string localAddrStr;
	if ( !network::AddressStringFromSockaddr(saddr, localAddrStr) )
	{
		fprintf(stderr, "Error converting local address to string.\n");
		exit(1);
	}
	
	fprintf(stdout, "Sending %lu bytes to %s on port %d\n", len, localAddrStr.c_str(), network::PortFromSockaddr(saddr));	
#endif
		
	int bytesSent = sendto(sock, messageMsg.c_str(), messageMsg.length(), 0, saddr, saddrLen);
	
	if ( bytesSent != (int)len )
	{
#if DEBUGON
		if ( bytesSent == -1 )
		{
			fprintf(stderr, "Error sending message. %d: %s\n", errno, strerror(errno));
		}
		else
		{
			fprintf(stderr, "Tried to send %lu bytes but only sent %d\n", len, bytesSent);
		}
#endif

		return false;
	}
	
#if DEBUGON
	fprintf(stdout, "\n");
#endif	
	return true;
}

bool ProtoBufSend(int sock, const google::protobuf::Message& message)
{
	std::stringstream stream;
	
	if ( !message.SerializeToOstream(&stream) )
	{
#if DEBUGON
		// TODO: Add protocol buffer debug logging here to find out what required fields are missing.
		fprintf(stderr, "Couldn't serialize message.\n");
#endif
		return false;
	}
	
	const std::string & msg = stream.rdbuf()->str();
	
	if ( msg.length() > MAX_PACKET_LEN )
	{
#if DEBUGON
		fprintf(stderr, "Message is larger than my largest safe size. Max is %lu, got %lu.\n", MAX_PACKET_LEN, msg.length());
#endif
		return false;
	}
	
	size_t count = send(sock, msg.c_str(), msg.length(), 0);
	if ( count != msg.length() )
	{
#if DEBUGON
		fprintf(stderr, "Tried to send %lu bytes but only sent %lu\n", msg.length(), count);
#endif
		return false;
	}
	
	return true;
}

bool ProtoBufRecvFrom(int sock, google::protobuf::Message& message, struct sockaddr* saddr, socklen_t *saddrLen)
{
	char buf[MAX_PACKET_LEN];
	memset(buf, '\0', MAX_PACKET_LEN);
	
	struct sockaddr_in lAddr;
	socklen_t lAddrLen = sizeof(lAddr);
	struct sockaddr* lSockAddr = (struct sockaddr*)&lAddr;
	
	size_t bytesReceived;
	if (saddr == NULL)
	{
		bytesReceived = recvfrom(sock, buf, MAX_PACKET_LEN, 0, lSockAddr, &lAddrLen);
	}
	else
	{
		bytesReceived = recvfrom(sock, buf, MAX_PACKET_LEN, 0, saddr, saddrLen);
	}
		
	if ( bytesReceived <= 0 || bytesReceived > MAX_PACKET_LEN )
	{
		return false;
	}
	
#if DEBUGON
	time_t curtime;
	time(&curtime);
	fprintf(stdout, "\n%s - Incoming Message:\n", ctime(&curtime));
	
	if (saddr == NULL)
	{
		std::string lAddrStr;
		if ( !network::AddressStringFromSockaddr(lSockAddr, lAddrStr) )
		{
			fprintf(stderr, "Error converting local address to string.\n");
			exit(1);
		}
		
		fprintf(stdout, "Received %lu bytes from %s on port %d\n", bytesReceived, lAddrStr.c_str(), network::PortFromSockaddr(lSockAddr));
	}
	else
	{
		std::string localAddrStr;
		if ( !network::AddressStringFromSockaddr(saddr, localAddrStr) )
		{
			fprintf(stderr, "Error converting local address to string.\n");
			exit(1);
		}
		
		fprintf(stdout, "Received %lu bytes from %s on port %d\n", bytesReceived, localAddrStr.c_str(), network::PortFromSockaddr(saddr));
	}
#endif
	
	// Attempt to de-serialize protocol buffer
	std::string msgstr(buf, bytesReceived);	
	std::stringstream streamRecv(msgstr);
	
	if (!message.ParseFromIstream(&streamRecv))
	{
#if DEBUGON
		fprintf(stderr, "Couldn't parse protocol buffer.\n");
#endif
		return false;
	}
#if DEBUGON
	google::protobuf::io::OstreamOutputStream oDebug(&std::cout);
	google::protobuf::TextFormat::Print(message, &oDebug);
	fprintf(stdout, "\n");
#endif
	return true;
}

bool ProtoBufRecvFromWithTimeout(int sock, google::protobuf::Message& message, struct sockaddr* saddr, socklen_t *saddrLen, struct timeval* timeout)
{
	assert(timeout);
	
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(sock, &readset);

	int result = select(sock + 1, &readset, NULL, NULL, timeout);

	if ( result == 0 )
	{
		// timeout - don't need to log error.
		return false;
	}

	if ( result == -1 )
	{
#if DEBUGON
		fprintf(stderr, "ProtoBufRecvFrom: select returned an error. %d: %s", errno, strerror(errno));
#endif
		return false;
	}
	
	return ProtoBufRecvFrom(sock, message, saddr, saddrLen);
}

bool ProtoBufRecv(int sock, google::protobuf::Message& message)
{
	char buf[MAX_PACKET_LEN];
	int count = recv(sock, buf, sizeof(buf), 0);
	if ( count == -1 )
	{
#if DEBUGON
		fprintf(stderr, "Error receiving data. %d: %s\n", errno, strerror(errno));
#endif
		exit(1);
	}
	
	if ( count <= 0 || (unsigned int)count > MAX_PACKET_LEN )
	{
#if DEBUGON
		fprintf(stderr, "Received out of range count: %d. Expected > 0 and < %lu\n", count, MAX_PACKET_LEN);
#endif
		return false;
	}
	
	std::string strbuf(buf, count);
	std::istringstream stream(strbuf);
	
	if ( !message.ParseFromIstream(&stream) )
	{
#if DEBUGON
		fprintf(stderr, "Error parsing message.\n");
#endif
		return false;
	}
	
	return true;
}

} // namespace
