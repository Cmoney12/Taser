#ifndef TASER_RELAYALLOCATION_HPP
#define TASER_RELAYALLOCATION_HPP

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <random>
#include <cstdint>
#include <optional>

template<typename Socket>
class RelayAllocation {
public:
    explicit RelayAllocation(Socket&& primary,
        std::optional<Socket> secondary = std::nullopt)
            : primary_(std::move(primary)), secondary_(std::move(secondary)) {}

    bool hasSecondary() const {
        return secondary_.has_value();
    }

    std::uint16_t primaryPort() const {
        return primary_.port();
    }

    std::optional<std::uint16_t> secondaryPort() const {
        if (secondary_) return secondary_->local_endpoint().port();
        return std::nullopt;
    }

    Socket& primary() {
        return primary_;
    }

    Socket releaseSecondary() {
        Socket socket = std::move(*secondary_);
        secondary_.reset();
        return socket;
    }

private:
    Socket primary_;
    std::optional<Socket> secondary_;
};

using TcpRelayAllocation = RelayAllocation<boost::asio::ip::tcp::acceptor>;
using UdpRelayAllocation = RelayAllocation<boost::asio::ip::udp::socket>;


#endif //TASER_RELAYALLOCATION_HPP
