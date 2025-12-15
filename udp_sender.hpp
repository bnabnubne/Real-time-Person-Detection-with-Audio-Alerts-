#pragma once
#include <string>
#include <cstdint>

class UdpSender {
public:
    UdpSender(const std::string& ip, uint16_t port);
    ~UdpSender();

    bool send_text(const std::string& s);
    bool send_bytes(const uint8_t* data, size_t len);

private:
    int sock_;
    struct sockaddr_in* addr_;
};