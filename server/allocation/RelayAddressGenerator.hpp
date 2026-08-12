#ifndef TASER_RELAYADDRESSGENERATOR_HPP
#define TASER_RELAYADDRESSGENERATOR_HPP

#include <boost/asio.hpp>
#include <random>
#include <optional>
#include <cstdint>
#include <utility>

#include "RelayAllocation.hpp"

using boost::asio::ip::udp;
using boost::asio::ip::tcp;

enum class PortPolicy {
    Any,
    Even,
    EvenAndReserveNext
};

class RelayAddressGenerator {
public:
    explicit RelayAddressGenerator(boost::asio::ip::address addr, std::uint16_t min_port,
        std::uint16_t max_port);

    explicit RelayAddressGenerator(const std::string& addr, std::uint16_t min_port,
        std::uint16_t max_port);

    bool supportsIPv4() const noexcept;
    bool supportsIPv6() const noexcept;

    std::optional<UdpRelayAllocation> generateUdpRelay(
        const boost::asio::any_io_executor& executor, PortPolicy policy) const;

    // Generates a TCP acceptor for TURN relay allocation (RFC 6062).
    // Unlike UDP relay, TCP does not require port parity or RTP/RTCP pair
    // reservation — PortPolicy is therefore not applicable here. TCP relay
    // is typically used by clients that cannot use UDP due to firewall restrictions.
    std::optional<TcpRelayAllocation> generateTcpAcceptor(
        const boost::asio::any_io_executor& executor) const;

private:
    std::uint16_t min_port_;
    std::uint16_t max_port_;
    boost::asio::ip::address addr_;
    int max_retries{210};
};


#endif //TASER_RELAYADDRESSGENERATOR_HPP