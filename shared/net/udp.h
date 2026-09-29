// Minimal non-blocking UDP socket wrapper (POSIX + Winsock).
#pragma once
#include <cstdint>
#include <string>

namespace si {

struct NetAddress {
    uint32_t ip = 0;   // network byte order
    uint16_t port = 0; // host byte order
    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }
    std::string toString() const;
    static bool resolve(const std::string& host, uint16_t port, NetAddress& out);
};

struct NetAddressHash {
    size_t operator()(const NetAddress& a) const { return ((size_t)a.ip << 16) ^ a.port; }
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port 0 = ephemeral. Returns false on failure.
    bool open(uint16_t port = 0);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    bool send(const NetAddress& to, const void* data, size_t len);
    // Returns number of bytes received, 0 if nothing pending, -1 on error.
    int receive(NetAddress& from, void* buf, size_t cap);
    uint16_t localPort() const { return port_; }

private:
    long long fd_ = -1;
    uint16_t port_ = 0;
};

void netInit();

} // namespace si
