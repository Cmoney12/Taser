#ifndef TASER_ENDPOINT_HPP
#define TASER_ENDPOINT_HPP

#include <boost/asio/ip/address.hpp>
#include <cstdint>
#include <utility>
#include <stunxx/Stun.hpp>
#include <stunxx/attributes/XorAddressAttrT.hpp>


class Endpoint {
public:
    Endpoint(boost::asio::ip::address address,
        std::uint16_t port, stunxx::Protocol protocol);

    const boost::asio::ip::address& address() const;

    std::uint16_t port() const;

    stunxx::Protocol protocol() const;

    bool operator==(const Endpoint& other) const noexcept;

private:
    boost::asio::ip::address address_;
    std::uint16_t port_;
    stunxx::Protocol protocol_;
};

inline stunxx::StunAddress makeStunAddress(const boost::asio::ip::address& address, std::uint16_t port) {
    if (address.is_v4()) {
        return stunxx::StunAddress::ipv4(port, address.to_v4().to_bytes());
    }
    return stunxx::StunAddress::ipv6(port, address.to_v6().to_bytes());
}

namespace std {
    template<>
    struct hash<Endpoint> {
        std::size_t operator()(const Endpoint& ep) const noexcept {
            std::size_t h = 0;

            auto hash_combine = [&](std::size_t v) {
                // same hash combination formula as your FiveTuple
                h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            };

            // combine the address, port, and protocol
            hash_combine(std::hash<boost::asio::ip::address>{}(ep.address()));
            hash_combine(std::hash<std::uint16_t>{}(ep.port()));
            hash_combine(std::hash<int>{}(static_cast<int>(ep.protocol())));

            return h;
        }
    };
}


#endif //TASER_ENDPOINT_HPP