#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include "MarketData.h"

class UdpPublisher {
private:
    int sock;
    struct sockaddr_in multicastAddr;

public:
    UdpPublisher(const char* multicastIp = "239.255.0.1", int port = 3000) {
        // 1. Create a UDP socket (SOCK_DGRAM instead of SOCK_STREAM)
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "Fatal: Failed to create UDP socket\n";
            return;
        }

        // 2. Set up the destination address
        multicastAddr.sin_family = AF_INET;
        multicastAddr.sin_addr.s_addr = inet_addr(multicastIp);
        multicastAddr.sin_port = htons(port);
    }

    ~UdpPublisher() {
        if (sock >= 0) close(sock);
    }

    // 3. The function that blasts the struct to the network
    inline void publishBbo(const BboMessage& msg) {
        // We literally just cast the struct pointer to a char pointer and send the raw bytes
        sendto(sock, reinterpret_cast<const char*>(&msg), sizeof(BboMessage), 0, 
              (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));
    }
};