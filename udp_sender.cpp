#include "udp_sender.hpp"

#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

struct sockaddr_in;

UdpSender::UdpSender(const std::string& ip, uint16_t port)
{
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    addr_ = new sockaddr_in();
    std::memset(addr_, 0, sizeof(sockaddr_in));
    addr_->sin_family = AF_INET;
    addr_->sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &(addr_->sin_addr));
}

UdpSender::~UdpSender()
{
    if (sock_ >= 0) ::close(sock_);
    delete addr_;
}

bool UdpSender::send_text(const std::string& s)
{
    return send_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

bool UdpSender::send_bytes(const uint8_t* data, size_t len)
{
    if (sock_ < 0) return false;
    // UDP datagram max ~65KB -> 352x352 JPEG (quality 60-80) thường < 60KB
    ssize_t sent = ::sendto(sock_, data, len, 0,
                            reinterpret_cast<sockaddr*>(addr_),
                            sizeof(sockaddr_in));
    return (sent == (ssize_t)len);
}