#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <google/protobuf/message.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#if defined(WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#define in_port_t u_short
#endif


namespace network {

// OpenConnectedUDPSocket
// ipAddressString - IP address string
// portString - port string
// return value - socket descriptor on success or -1 on failure. Caller responsible for closing.
int OpenConnectedUDPSocket(const char* ipAddressString, const char* portString);

// MakeSocketAddress
// ipAddressString - IP address string
// portString - port string
// saddrOut - pointer to sockaddr
// saddrOutLen - in/out parameter. On in, it is the size of the buffer pointed to by saddrOut. On out, the actual size stored.
// return value - true if strings parsed successfully and saddrOut set; false otherwise.
bool MakeSocketAddress(const char* ipAddressString, const char* portString, struct sockaddr* saddrOut, socklen_t* saddrOutLen);

bool MakeSocketAddress(const char* ipAddressString, in_port_t port, struct sockaddr* saddrOut, socklen_t* saddrOutLen);

// PortFromSockaddr - get the port in the sockaddr. Works with IPv4 and IPv6 addresses. Returns 0 on failure.
in_port_t PortFromSockaddr(const struct sockaddr* saddr);

bool AddressStringFromSockaddr(const struct sockaddr* saddr, std::string & addressOut);

// ProtoBufSendTo - similar to sendto, but takes a protocol buffer Message
// sock - file descriptor
// message - message to send
// saddr - address to send message to.
// saddrLen - length of saddr.
// return value - True if the entire message was sent. False otherwise.
//
// This method is only intended to be used by single UDP packets. It doesn't handle breaking them up. It assumes
// message can fit into a single UDP packet.
bool ProtoBufSendTo(int sock, const google::protobuf::Message& message, const struct sockaddr* saddr, socklen_t saddrLen);

// ProtoBufSend - similar to send, but takes a protocol buffer Message.
// sock - file descriptor. Must be connected. Otherwise, use ProtoBufSendTo.
// message - message to send
// return value - True if the entire message was sent. False otherwise.
//
// This method is only intended to be used by single UDP packets. It doesn't handle breaking them up. It assumes
// message can fit into a single UDP packet.
bool ProtoBufSend(int sock, const google::protobuf::Message& message);

// ProtoBufRecvFrom - similar to recvfrom, but takes a protocol buffer Message
// sock - file descriptor
// message - protocol buffer to store result in. Cannot be NULL.
// saddr - The address of the sender. Can be NULL if you don't care.
// saddrLen - In/out value. Can be NULL if saddr is NULL. Otherwise, should be set to length of saddr buffer on input. On return, will be the length of the sender's address.
// return value - True if a valid protocol buffer message was read. False otherwise.
bool ProtoBufRecvFrom(int sock, google::protobuf::Message& message, struct sockaddr* saddr, socklen_t *saddrLen);

// ProtoBufRecvFromWithTimeout - same as ProtoBufRecvFrom, but allows you to specify a timeout
bool ProtoBufRecvFromWithTimeout(int sock, google::protobuf::Message& message, struct sockaddr* saddr, socklen_t *saddrLen, struct timeval* timeout);

// ProtoBufRecvFrom - similar to recv, but takes a protocol buffer Message
// sock - file descriptor. Must be connected. Otherwise, use ProtoBufRecvFrom.
// message - protocol buffer to store result in.
// return value - True if a valid protocol buffer message was read. False otherwise.
bool ProtoBufRecv(int sock, google::protobuf::Message& message);

}

#endif
