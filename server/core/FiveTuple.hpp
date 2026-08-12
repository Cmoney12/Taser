#ifndef TASER_FIVETUPLE_HPP
#define TASER_FIVETUPLE_HPP

#include <boost/asio.hpp>
#include <string>
#include <cstdint>
#include <functional>
#include "Endpoint.hpp"

#include <stunxx/Stun.hpp>

class FiveTuple {
public:
    FiveTuple(boost::asio::ip::address client_addr,
              std::uint16_t client_port,
              boost::asio::ip::address server_addr,
              std::uint16_t server_port,
              stunxx::Protocol protocol);

    static FiveTuple fromUdp(const boost::asio::ip::udp::endpoint& client,
        const boost::asio::ip::udp::endpoint& server);

    static FiveTuple fromTcp(const boost::asio::ip::tcp::endpoint& client,
        const boost::asio::ip::tcp::endpoint& server);

    const boost::asio::ip::address& clientAddress() const;

    std::uint16_t clientPort() const;

    const boost::asio::ip::address& serverAddress() const;

    std::uint16_t serverPort() const;

    stunxx::Protocol protocol() const noexcept;

    Endpoint endpoint() const noexcept;

    bool operator==(const FiveTuple& other) const = default;

private:
    boost::asio::ip::address client_address_;
    std::uint16_t client_port_;
    boost::asio::ip::address server_address_;
    std::uint16_t server_port_;
    stunxx::Protocol protocol_;
};

namespace std {
    template<>
    struct hash<FiveTuple> {
        std::size_t operator()(const FiveTuple& ft) const noexcept {
            std::size_t h = 0;

            auto hash_combine = [&](const std::size_t v) {
                h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            };

            hash_combine(std::hash<boost::asio::ip::address>{}(ft.clientAddress()));
            hash_combine(std::hash<std::uint16_t>{}(ft.clientPort()));
            hash_combine(std::hash<boost::asio::ip::address>{}(ft.serverAddress()));
            hash_combine(std::hash<std::uint16_t>{}(ft.serverPort()));
            hash_combine(std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(ft.protocol())));

            return h;
        }
    };
}

#endif //TASER_FIVETUPLE_HPP