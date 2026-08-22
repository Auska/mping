#pragma once

#ifndef ICMPLIB_PING_DATA_SIZE
#define ICMPLIB_PING_DATA_SIZE 64
#endif

#ifndef ICMPLIB_RECV_BUFFER_SIZE
#define ICMPLIB_RECV_BUFFER_SIZE 1024
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cstring>
#include <random>
#include <regex>
#include <string>

#define ICMPLIB_ICMP_ECHO_RESPONSE 0
#define ICMPLIB_ICMP_DESTINATION_UNREACHABLE 3
#define ICMPLIB_ICMP_ECHO_REQUEST 8
#define ICMPLIB_ICMP_TIME_EXCEEDED 11
#define ICMPLIB_ICMPV6_DESTINATION_UNREACHABLE 1
#define ICMPLIB_ICMPV6_TIME_EXCEEDED 3
#define ICMPLIB_ICMPV6_ECHO_REQUEST 128
#define ICMPLIB_ICMPV6_ECHO_RESPONSE 129

#define ICMPLIB_INET4_HEADER_SIZE 20
#define ICMPLIB_INET4_TTL_OFFSET 8
#define ICMPLIB_INET4_ORIGINAL_DATA_SIZE ICMPLIB_INET4_HEADER_SIZE + 8
#define ICMPLIB_INET6_ORIGINAL_DATA_SIZE 8

#define ICMPLIB_TIMEOUT_1S 1000

#define ICMPLIB_SOCKET int
#define ICMPLIB_SOCKLEN socklen_t
#define ICMPLIB_SOCKET_ERROR -1
#define ICMPLIB_CLOSESOCKET close

namespace icmplib {
class IPAddress {
   public:
    enum class Type { IPv4, IPv6, Unknown };
    IPAddress() {
        address = reinterpret_cast<sockaddr*>(new sockaddr_in);
        std::memset(address, 0, sizeof(sockaddr_in));
        reinterpret_cast<sockaddr_in*>(address)->sin_family = AF_INET;
    }
    IPAddress(const std::string& address, Type type = Type::Unknown) : IPAddress() {
        auto init = [&](Type type) {
            switch (type) {
                case Type::IPv6:
                    delete this->address;
                    this->address = reinterpret_cast<sockaddr*>(new sockaddr_in6);
                    std::memset(this->address, 0, sizeof(sockaddr_in6));
                    reinterpret_cast<sockaddr_in6*>(this->address)->sin6_family = AF_INET6;
                    if (inet_pton(AF_INET6, address.c_str(),
                                  &reinterpret_cast<sockaddr_in6*>(this->address)->sin6_addr)
                        <= 0) {
                        throw std::runtime_error("Incorrect IPv6 address provided");
                    }
                    break;
                case Type::IPv4:
                default:
                    if (inet_pton(AF_INET, address.c_str(),
                                  &reinterpret_cast<sockaddr_in*>(this->address)->sin_addr)
                        <= 0) {
                        throw std::runtime_error("Incorrect IPv4 address provided");
                    }
            }
        };
        if ((type != Type::Unknown) && IsCorrect(address, type)) {
            init(type);
            return;
        } else if (type == Type::Unknown) {
            if (IsCorrect(address, Type::IPv4)) {
                init(Type::IPv4);
                return;
            } else if (IsCorrect(address, Type::IPv6)) {
                init(Type::IPv6);
                return;
            }
        }
        Resolve(address, type);
    }
    IPAddress(const std::string& address, uint16_t port, Type type = Type::Unknown)
        : IPAddress(address, type) {
        SetPort(port);
    }
    IPAddress(uint32_t address) : IPAddress() {
        reinterpret_cast<sockaddr_in*>(this->address)->sin_addr.s_addr = htonl(address);
    }
    IPAddress(uint32_t address, uint16_t port) : IPAddress(address) { SetPort(port); }
    IPAddress(const IPAddress& source) {
        switch (source.GetType()) {
            case Type::IPv6:
                address = reinterpret_cast<sockaddr*>(new sockaddr_in6);
                std::memcpy(address, source.address, sizeof(sockaddr_in6));
                break;
            case Type::IPv4:
            default:
                address = reinterpret_cast<sockaddr*>(new sockaddr_in);
                std::memcpy(address, source.address, sizeof(sockaddr_in));
        }
    }
    IPAddress(IPAddress&& source) {
        address        = source.address;
        source.address = reinterpret_cast<sockaddr*>(new sockaddr_in);
        std::memset(source.address, 0, sizeof(sockaddr_in));
        reinterpret_cast<sockaddr_in*>(source.address)->sin_family = AF_INET;
    }
    virtual ~IPAddress() { delete address; }
    IPAddress& operator=(const IPAddress& source) {
        delete address;
        switch (source.GetType()) {
            case Type::IPv6:
                address = reinterpret_cast<sockaddr*>(new sockaddr_in6);
                std::memcpy(address, source.address, sizeof(sockaddr_in6));
                break;
            case Type::IPv4:
            default:
                address = reinterpret_cast<sockaddr*>(new sockaddr_in);
                std::memcpy(address, source.address, sizeof(sockaddr_in));
        }
        return *this;
    }
    IPAddress& operator=(IPAddress&& source) {
        delete address;
        address        = source.address;
        source.address = reinterpret_cast<sockaddr*>(new sockaddr_in);
        std::memset(source.address, 0, sizeof(sockaddr_in));
        reinterpret_cast<sockaddr_in*>(source.address)->sin_family = AF_INET;
        return *this;
    }
    IPAddress& Resolve(const std::string& address, Type type = Type::IPv4) {
        addrinfo hints;
        std::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = NULL;
        if (getaddrinfo(address.c_str(), NULL, &hints, &result) == 0) {
            for (addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
                switch (ptr->ai_family) {
                    case AF_INET:
                        if ((type != Type::IPv4) && (type != Type::Unknown)) {
                            break;
                        }
                        delete this->address;
                        this->address = reinterpret_cast<sockaddr*>(new sockaddr_in);
                        std::memcpy(this->address, ptr->ai_addr, sizeof(sockaddr_in));
                        freeaddrinfo(result);
                        type = Type::IPv4;
                        return *this;
                    case AF_INET6:
                        if ((type != Type::IPv6) && (type != Type::Unknown)) {
                            break;
                        }
                        delete this->address;
                        this->address = reinterpret_cast<sockaddr*>(new sockaddr_in6);
                        std::memcpy(this->address, ptr->ai_addr, sizeof(sockaddr_in6));
                        freeaddrinfo(result);
                        type = Type::IPv6;
                        return *this;
                    default:
                        break;
                }
            }
            freeaddrinfo(result);
        }
        throw std::runtime_error("Cannot resolve host address: " + address);
    }
    operator std::string() const {
        char buffer[INET6_ADDRSTRLEN];
        switch (GetType()) {
            case Type::IPv6:
                if (inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(address)->sin6_addr,
                              buffer, INET6_ADDRSTRLEN)
                    != NULL) {
                    return std::string(buffer);
                }
                throw std::runtime_error("Cannot convert IPv6 address structure");
            case Type::IPv4:
            default:
                if (inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(address)->sin_addr, buffer,
                              INET6_ADDRSTRLEN)
                    != NULL) {
                    return std::string(buffer);
                }
                throw std::runtime_error("Cannot convert IPv4 address structure");
        }
    }
    void SetPort(uint16_t port) {
        switch (GetType()) {
            case Type::IPv6:
                reinterpret_cast<sockaddr_in6*>(address)->sin6_port = htons(port);
                break;
            case Type::IPv4:
            default:
                reinterpret_cast<sockaddr_in*>(address)->sin_port = htons(port);
        }
    }
    uint16_t GetPort() const {
        switch (GetType()) {
            case Type::IPv6:
                return ntohs(reinterpret_cast<sockaddr_in6*>(address)->sin6_port);
                break;
            case Type::IPv4:
            default:
                return ntohs(reinterpret_cast<sockaddr_in*>(address)->sin_port);
        }
    }
    Type GetType() const {
        switch (address->sa_family) {
            case AF_INET6:
                return Type::IPv6;
            case AF_INET:
            default:
                return Type::IPv4;
        }
    }
    sockaddr* GetSockAddr() const { return address; }
    ICMPLIB_SOCKLEN GetSockAddrLength() const {
        switch (GetType()) {
            case Type::IPv6:
                return sizeof(sockaddr_in6);
            case Type::IPv4:
            default:
                return sizeof(sockaddr_in);
        }
    }
    static bool IsCorrect(const std::string& address, Type type = Type::IPv4) {
        switch (type) {
            case Type::IPv4:
                return std::regex_match(address,
                                        std::regex(
                                            "^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:"
                                            "25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"));
            case Type::IPv6:
                return std::regex_match(
                    address,
                    std::regex(
                        "^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-"
                        "9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-"
                        "F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-"
                        "F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-"
                        "F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{"
                        "1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,"
                        "4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|"
                        "(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]"
                        "|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))"
                        "$"));
            default:
                return IsCorrect(address, Type::IPv4) || IsCorrect(address, Type::IPv6);
        }
    }
    static int GetFamily(Type type) {
        switch (type) {
            case Type::IPv6:
                return AF_INET6;
            case Type::IPv4:
            default:
                return AF_INET;
        }
    }

   private:
    sockaddr* address;
};

class ICMPEcho {
   public:
    struct Result {
        enum class ResponseType {
            Success,
            Unreachable,
            TimeExceeded,
            Timeout,
            Unsupported,
            Failure
        } response;
        double delay;
        IPAddress address;
        uint8_t code;
        uint8_t ttl;
    };
    ICMPEcho()                           = delete;
    ICMPEcho(const ICMPEcho&)            = delete;
    ICMPEcho(ICMPEcho&&)                 = delete;
    ICMPEcho& operator=(const ICMPEcho&) = delete;
    static Result Execute(const IPAddress& target, unsigned timeout = ICMPLIB_TIMEOUT_1S,
                          uint16_t sequence = 1, uint8_t ttl = 255) {
        try {
            ICMPSocket sock(target.GetType(), ttl);
            return ExecuteOnSocket(sock.GetSocket(), target, timeout, sequence);
        } catch (...) {
            return {Result::ResponseType::Failure, 0, IPAddress(), 0, 0};
        }
    }

    // 在已打开的 socket 上执行单次 ping，供复用同一 socket 连续发包
    static Result ExecuteOnSocket(ICMPLIB_SOCKET sock, const IPAddress& target, unsigned timeout,
                                  uint16_t sequence = 1) {
        Result result = {Result::ResponseType::Timeout, static_cast<double>(timeout), IPAddress(),
                         0, 0};
        ICMPRequest request(target.GetType(), sequence);
        try {
            request.Send(sock, target);
        } catch (...) {
            return {Result::ResponseType::Failure, 0, IPAddress(), 0, 0};
        }
        auto start = std::chrono::high_resolution_clock::now();
        IPAddress source(target);

        while (true) {
            ICMPResponse response;
            bool recv = response.Receive(sock, source, timeout);
            auto end  = std::chrono::high_resolution_clock::now();
            if (!recv) {
                unsigned delta = static_cast<unsigned>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                if (delta >= timeout) {
                    break;
                }
                timeout -= delta;
                continue;
            }

            result.response = (source.GetType() != IPAddress::Type::IPv6)
                                  ? GetResponseType(request, response)
                                  : GetResponseTypeV6(request, response);
            if (result.response != Result::ResponseType::Timeout) {
                result.delay =
                    static_cast<double>(
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count())
                    / 1000.0;
                result.address = source;
                result.code    = response.GetICMPHeader().code;
                result.ttl     = response.GetTTL();
                break;
            }
        }
        return result;
    }

   public:
    struct ICMPHeader {
        uint8_t type;
        uint8_t code;
        uint16_t checksum;
    };

    struct ICMPEchoMessage : ICMPHeader {
        uint16_t id;
        uint16_t seq;
        uint8_t data[ICMPLIB_PING_DATA_SIZE];
    };

    struct ICMPRevertedMessage : ICMPHeader {
        uint32_t unused;
        uint8_t data[ICMPLIB_INET4_ORIGINAL_DATA_SIZE];
    };

    class ICMPSocket {
       public:
        ICMPSocket(IPAddress::Type type, uint8_t ttl) {
            int protocol = IPPROTO_ICMP;
            if (type == IPAddress::Type::IPv6) {
                protocol = IPPROTO_ICMPV6;
            }

            sock = socket(IPAddress::GetFamily(type), SOCK_RAW, protocol);
            if (sock <= 0) {
                throw std::runtime_error("Cannot initialize socket!");
            }

            switch (type) {
                case IPAddress::Type::IPv6:
                    if (setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                                   reinterpret_cast<char*>(&ttl), sizeof(uint8_t))
                        == ICMPLIB_SOCKET_ERROR) {
                        ICMPLIB_CLOSESOCKET(sock);
                        throw std::runtime_error("Cannot set socket options!");
                    }
                    break;
                case IPAddress::Type::IPv4:
                default:
                    if (setsockopt(sock, IPPROTO_IP, IP_TTL, reinterpret_cast<char*>(&ttl),
                                   sizeof(uint8_t))
                        == ICMPLIB_SOCKET_ERROR) {
                        ICMPLIB_CLOSESOCKET(sock);
                        throw std::runtime_error("Cannot set socket options!");
                    }
            }

            int flags = fcntl(sock, F_GETFL, 0);
            if ((flags == -1) || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
                ICMPLIB_CLOSESOCKET(sock);
                throw std::runtime_error("Cannot set socket options!");
            }
        }
        virtual ~ICMPSocket() { ICMPLIB_CLOSESOCKET(sock); }
        const ICMPLIB_SOCKET& GetSocket() { return sock; }

       private:
        ICMPLIB_SOCKET sock;
    };

    class ICMPRequest : public ICMPEchoMessage {
       public:
        ICMPRequest() = delete;
        ICMPRequest(IPAddress::Type protocol, uint16_t sequence = 1) {
            std::memset(this, 0, sizeof(ICMPEchoMessage));
            // 每次请求取独立随机 ID：避免并发进程未播种 rand() 的相同序列导致回包串线
            id   = static_cast<uint16_t>(std::random_device{}() & 0xFFFF);
            type = (protocol != IPAddress::Type::IPv6) ? ICMPLIB_ICMP_ECHO_REQUEST
                                                       : ICMPLIB_ICMPV6_ECHO_REQUEST;
            seq  = sequence;
            if (protocol != IPAddress::Type::IPv6) {
                SetChecksum<ICMPEchoMessage>(*this);
            }
        }
        void Send(ICMPLIB_SOCKET sock, const IPAddress& address) {
            int bytes = sendto(sock, reinterpret_cast<char*>(this), sizeof(ICMPEchoMessage), 0,
                               address.GetSockAddr(), address.GetSockAddrLength());
            if (bytes == ICMPLIB_SOCKET_ERROR) {
                throw std::runtime_error("Failed to send request!");
            }
        }
    };

    class ICMPResponse {
       public:
        ICMPResponse() : protocol(IPAddress::Type::IPv4), header(nullptr), length(0) {
            std::memset(&buffer, 0, sizeof(uint8_t) * ICMPLIB_RECV_BUFFER_SIZE);
        }
        virtual ~ICMPResponse() {
            if (header) {
                delete header;
            }
        }
        bool Receive(ICMPLIB_SOCKET sock, IPAddress& address, unsigned timeout) {
            fd_set sock_set;
            FD_ZERO(&sock_set);
            FD_SET(sock, &sock_set);

            timeval timeout_val;
            timeout_val.tv_sec  = timeout / 1000;
            timeout_val.tv_usec = (timeout % 1000) * 1000;

            int activity = select(sock + 1, &sock_set, NULL, NULL, &timeout_val);
            if ((activity <= 0) | !FD_ISSET(sock, &sock_set)) {
                return false;
            }

            ICMPLIB_SOCKLEN length = address.GetSockAddrLength();
            int bytes = recvfrom(sock, reinterpret_cast<char*>(buffer), ICMPLIB_RECV_BUFFER_SIZE, 0,
                                 address.GetSockAddr(), &length);
            if (bytes <= 0) {
                return false;
            }
            this->length = static_cast<unsigned>(bytes);
            protocol     = address.GetType();
            return true;
        };
        template <class T>
        const T Generate() const {
            if (sizeof(T) > length) {
                throw std::runtime_error("Incorrect ICMP packet size!");
            }
            T packet;
            std::memset(&packet, 0, sizeof(T));
            switch (protocol) {
                case IPAddress::Type::IPv6:
                    std::memcpy(&packet, buffer,
                                static_cast<long unsigned>(length) > sizeof(T)
                                    ? sizeof(T)
                                    : static_cast<long unsigned>(length));
                    break;
                case IPAddress::Type::IPv4:
                default:
                    std::memcpy(&packet, &buffer[ICMPLIB_INET4_HEADER_SIZE],
                                static_cast<long unsigned>(length) - ICMPLIB_INET4_HEADER_SIZE
                                        > sizeof(T)
                                    ? sizeof(T)
                                    : static_cast<long unsigned>(length)
                                          - ICMPLIB_INET4_HEADER_SIZE);
            }
            return packet;
        }
        const ICMPHeader& GetICMPHeader() {
            if (!header) {
                header  = new ICMPHeader;
                *header = Generate<ICMPHeader>();
            }
            return *header;
        }
        IPAddress::Type GetProtocol() const { return protocol; }
        uint8_t GetTTL() const {
            switch (protocol) {
                case IPAddress::Type::IPv6:
                    return 0;
                    break;
                case IPAddress::Type::IPv4:
                default:
                    return buffer[ICMPLIB_INET4_TTL_OFFSET];
            }
        }
        unsigned GetSize() const {
            switch (protocol) {
                case IPAddress::Type::IPv6:
                    return length;
                    break;
                case IPAddress::Type::IPv4:
                default:
                    return length - ICMPLIB_INET4_HEADER_SIZE;
            }
        }

       private:
        IPAddress::Type protocol;
        uint8_t buffer[ICMPLIB_RECV_BUFFER_SIZE];
        ICMPHeader* header;
        unsigned length;
    };

    static Result::ResponseType GetResponseType(const ICMPRequest& request,
                                                ICMPResponse& response) {
        Result::ResponseType result = Result::ResponseType::Timeout;
        ICMPEchoMessage echo;
        ICMPRevertedMessage reverted;
        switch (response.GetICMPHeader().type) {
            case ICMPLIB_ICMP_ECHO_RESPONSE:
                result        = Result::ResponseType::Success;
                echo          = response.Generate<ICMPEchoMessage>();
                echo.checksum = 0;
                if ((response.GetICMPHeader().checksum != SetChecksum<ICMPEchoMessage>(echo))
                    || (request.id != echo.id)) {
                    result = Result::ResponseType::Unsupported;
                }
                break;
            case ICMPLIB_ICMP_DESTINATION_UNREACHABLE:
                result = Result::ResponseType::Unreachable;
                [[fallthrough]];
            case ICMPLIB_ICMP_TIME_EXCEEDED:
                if (result == Result::ResponseType::Timeout) {
                    result = Result::ResponseType::TimeExceeded;
                }
                reverted          = response.Generate<ICMPRevertedMessage>();
                reverted.checksum = 0;
                if (response.GetICMPHeader().checksum
                    != SetChecksum<ICMPRevertedMessage>(reverted)) {
                    result = Result::ResponseType::Unsupported;
                }
                break;
            case ICMPLIB_ICMP_ECHO_REQUEST:
                break;
            default:
                result = Result::ResponseType::Unsupported;
        }

        return result;
    };

    static Result::ResponseType GetResponseTypeV6(const ICMPRequest& request,
                                                  ICMPResponse& response) {
        Result::ResponseType result = Result::ResponseType::Timeout;
        ICMPEchoMessage echo;
        switch (response.GetICMPHeader().type) {
            case ICMPLIB_ICMPV6_ECHO_RESPONSE:
                result = Result::ResponseType::Success;
                echo   = response.Generate<ICMPEchoMessage>();
                if (request.id != echo.id) {
                    result = Result::ResponseType::Unsupported;
                }
                break;
            case ICMPLIB_ICMPV6_DESTINATION_UNREACHABLE:
                result = Result::ResponseType::Unreachable;
                [[fallthrough]];
            case ICMPLIB_ICMPV6_TIME_EXCEEDED:
                if (result == Result::ResponseType::Timeout) {
                    result = Result::ResponseType::TimeExceeded;
                }
                break;
            case ICMPLIB_ICMPV6_ECHO_REQUEST:
                break;
            default:
                result = Result::ResponseType::Unsupported;
        }

        return result;
    };

    template <class T>
    static uint16_t SetChecksum(T& packet) {
        uint16_t* element  = reinterpret_cast<uint16_t*>(&packet);
        unsigned long size = sizeof(T);
        uint32_t sum       = 0;
        for (; size > 1; size -= 2) {
            sum += *element++;
        }
        if (size > 0) {
            sum += *reinterpret_cast<uint8_t*>(element);
        }
        sum = (sum >> 16) + (sum & 0xffff);
        sum += (sum >> 16);
        packet.checksum = static_cast<uint16_t>(~sum);
        return packet.checksum;
    };
};

using PingResult       = ICMPEcho::Result;
using PingResponseType = ICMPEcho::Result::ResponseType;

// RAII raw socket：按目标地址族创建，供复用同一 socket 连续发包
class PingSocket {
   public:
    PingSocket(IPAddress::Type type, uint8_t ttl = 255) : sock(type, ttl) {}
    ICMPLIB_SOCKET GetSocket() { return sock.GetSocket(); }

   private:
    ICMPEcho::ICMPSocket sock;
};

inline PingResult Ping(const IPAddress& target, unsigned timeout = ICMPLIB_TIMEOUT_1S,
                       uint16_t sequence = 1, uint8_t ttl = 255) {
    return ICMPEcho::Execute(target, timeout, sequence, ttl);
}

// 复用外部 socket 的单次 ping（省略 socket 创建，供连续发包）
inline PingResult Ping(ICMPLIB_SOCKET sock, const IPAddress& target,
                       unsigned timeout = ICMPLIB_TIMEOUT_1S, uint16_t sequence = 1) {
    return ICMPEcho::ExecuteOnSocket(sock, target, timeout, sequence);
}
}  // namespace icmplib
