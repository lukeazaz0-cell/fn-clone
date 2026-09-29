#include "udp.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace si {

void netInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        done = true;
    }
#endif
}

std::string NetAddress::toString() const {
    in_addr a;
    a.s_addr = ip;
    char buf[64];
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(port);
}

bool NetAddress::resolve(const std::string& host, uint16_t port, NetAddress& out) {
    netInit();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.ip = ((sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
    out.port = port;
    freeaddrinfo(res);
    return true;
}

UdpSocket::UdpSocket() { netInit(); }
UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port) {
    close();
    long long s = (long long)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return false;
    int buf = 4 * 1024 * 1024;
    setsockopt((int)s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
    setsockopt((int)s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof(buf));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind((int)s, (sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket((SOCKET)s);
#else
        ::close((int)s);
#endif
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &nb);
#else
    fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sockaddr_in bound{};
    socklen_t bl = sizeof(bound);
    getsockname((int)s, (sockaddr*)&bound, &bl);
    port_ = ntohs(bound.sin_port);
    fd_ = s;
    return true;
}

void UdpSocket::close() {
    if (fd_ >= 0) {
#ifdef _WIN32
        closesocket((SOCKET)fd_);
#else
        ::close((int)fd_);
#endif
    }
    fd_ = -1;
}

bool UdpSocket::send(const NetAddress& to, const void* data, size_t len) {
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to.ip;
    addr.sin_port = htons(to.port);
    return sendto((int)fd_, (const char*)data, (int)len, 0, (sockaddr*)&addr, sizeof(addr)) == (int)len;
}

int UdpSocket::receive(NetAddress& from, void* buf, size_t cap) {
    if (fd_ < 0) return -1;
    sockaddr_in addr{};
    socklen_t al = sizeof(addr);
    int n = (int)recvfrom((int)fd_, (char*)buf, (int)cap, 0, (sockaddr*)&addr, &al);
    if (n < 0) return 0; // EWOULDBLOCK or transient error
    from.ip = addr.sin_addr.s_addr;
    from.port = ntohs(addr.sin_port);
    return n;
}

} // namespace si
