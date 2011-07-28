//
//  node_peer.cc
//  Expresso-Node
//
//  Created by zac on 7/27/11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#include "node_peer.h"
#include "network.h"
#include "holepoke.pb.h"
#include <stdexcept>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <stdio.h>

using namespace std;

static holepoke::FSMEvent stRegisterWithServer(void* userInfo)
{
	return ((holepoke::NodePeer*)userInfo)->stRegisterWithServer();
}

static holepoke::FSMEvent stWaitForConnect(void* userInfo)
{
	return ((holepoke::NodePeer*)userInfo)->stWaitForConnect();
}

static holepoke::FSMEvent stLocalConnectToPeer(void* userInfo)
{
	return ((holepoke::NodePeer*)userInfo)->stLocalConnectToPeer();
}

static holepoke::FSMEvent stRemoteConnectToPeer(void* userInfo)
{
	return ((holepoke::NodePeer*)userInfo)->stRemoteConnectToPeer();
}

static holepoke::FSMEvent stConnected(void* userInfo)
{
	return ((holepoke::NodePeer*)userInfo)->stConnected();
}

namespace holepoke {
    
    static const FSMEvent kTimeoutEvent = 1;
    static const FSMEvent kGotWaitEvent = 2;
    static const FSMEvent kGotConnectEvent = 3;
    static const FSMEvent kPeerResponseEvent = 4;
    static const FSMEvent kStopMachineEvent = 5;
    static const FSMEvent kErrorEvent = 6;

    NodePeer::NodePeer(const struct sockaddr* serverAddr, socklen_t serverAddrLen, string id, string remote_id)
        : Endpoint(serverAddr, serverAddrLen)
    {
        node_id = id;
        remote_node_id = remote_id;
        
        // Use a 3 second retry interval
        _kRetryInterval.tv_sec = 3;
        _kRetryInterval.tv_usec = 0;
        
        //
        // Setup the state machine
        //
        _machine.setUserInfo(this);
        
        FSMState registerWithServer = _machine.addState(::stRegisterWithServer); // First state, will be called first when state machine run.
        FSMState waitForConnect = _machine.addState(::stWaitForConnect);
        
        FSMState localConnectToPeer = _machine.addState(::stLocalConnectToPeer);
        
        FSMState remoteConnectToPeer = _machine.addState(::stRemoteConnectToPeer);
        FSMState remoteConnectToPeerRetry1 = _machine.addState(::stRemoteConnectToPeer);
        FSMState remoteConnectToPeerRetry2 = _machine.addState(::stRemoteConnectToPeer);
        FSMState remoteConnectToPeerRetry3 = _machine.addState(::stRemoteConnectToPeer);
        
        FSMState connected = _machine.addState(::stConnected);
        
        _machine.addTransition(registerWithServer, kTimeoutEvent, registerWithServer);
        _machine.addTransition(registerWithServer, kGotWaitEvent, waitForConnect);
        _machine.addTransition(registerWithServer, kGotConnectEvent, localConnectToPeer);
        
        _machine.addTransition(waitForConnect, kTimeoutEvent, waitForConnect);
        _machine.addTransition(waitForConnect, kGotConnectEvent, localConnectToPeer);
        
        _machine.addTransition(localConnectToPeer, kTimeoutEvent, remoteConnectToPeer);
        _machine.addTransition(localConnectToPeer, kPeerResponseEvent, connected);
        
        _machine.addTransition(remoteConnectToPeer, kTimeoutEvent, remoteConnectToPeerRetry1);
        _machine.addTransition(remoteConnectToPeer, kPeerResponseEvent, connected);
        
        _machine.addTransition(remoteConnectToPeerRetry1, kTimeoutEvent, remoteConnectToPeerRetry2);
        _machine.addTransition(remoteConnectToPeerRetry1, kPeerResponseEvent, connected);
        
        _machine.addTransition(remoteConnectToPeerRetry2, kTimeoutEvent, remoteConnectToPeerRetry3);
        _machine.addTransition(remoteConnectToPeerRetry2, kPeerResponseEvent, connected);
        
        _machine.addTransition(remoteConnectToPeerRetry3, kTimeoutEvent, registerWithServer);
        _machine.addTransition(remoteConnectToPeerRetry3, kPeerResponseEvent, connected);
        
        _machine.addTransition(connected, kStopMachineEvent, kFSMStopState);
    }

    // Block until the Sender is connected to a peer or an error occurred.
    void NodePeer::connectToRemoteNode()
    {
        _machine.run();
    }
    
    //
    // State Routines
    //
    
    FSMEvent NodePeer::stRegisterWithServer()
    {
        // Send HELLO to get port assignment
        holepoke::Request hello_request;
        hello_request.set_type(holepoke::Request::HELLO);
        
        if ( !network::ProtoBufSendTo(_sock, hello_request, serverSockaddr(), serverSockaddrLen()) )
        {
            throw std::logic_error("Invalid request in stRegisterWithServer");
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
        
        holepoke::Node *local_node = request.mutable_node();
        local_node->set_id(node_id);
        
        holepoke::Node *remote_node = request.mutable_remote_node();
        remote_node->set_id(remote_node_id);
        
        if ( !network::ProtoBufSendTo(_sock, request, serverSockaddr(), serverSockaddrLen()) )
        {
            throw std::logic_error("Invalid request in stRegisterWithServer");
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
                    if (response.type() == holepoke::Response::WAIT)
                    {
                        return kGotWaitEvent;
                    }
                    else if (response.type() == holepoke::Response::CONNECT)
                    {
                        if (updatePeerAddresses(response))
                        {
                            return kGotConnectEvent;
                        }
                    }
                    else
                    {
                        repeat = true;
                    }
                }
                else
                {
                    repeat = true;
                }
            }
        }
        
        return kTimeoutEvent;
    }
    
    FSMEvent NodePeer::stWaitForConnect()
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
                            return kGotConnectEvent;
                        }
                    }
                    else
                    {
                        repeat = true;
                    }
                }
                else
                {
                    repeat = true;
                }
            }
        }
        
        holepoke::Request request;
        request.set_type(holepoke::Request::HEARTBEAT);
        
        if ( !network::ProtoBufSendTo(_sock, request, serverSockaddr(), serverSockaddrLen()) )
        {
            throw std::logic_error("Invalid request in stWaitForConnect");
        }
        
        return kTimeoutEvent;
    }
    
    FSMEvent NodePeer::stLocalConnectToPeer()
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
	
    FSMEvent NodePeer::stRemoteConnectToPeer()
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
    
    FSMEvent NodePeer::stConnected()
    {
        return kStopMachineEvent;
    }
}
