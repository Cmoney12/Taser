#include "RelayAddressGenerator.hpp"

RelayAddressGenerator::RelayAddressGenerator(boost::asio::ip::address addr, const std::uint16_t min_port,
                                             const std::uint16_t max_port) : min_port_(min_port), max_port_(max_port), addr_(std::move(addr)) {}

RelayAddressGenerator::RelayAddressGenerator(const std::string &addr, const std::uint16_t min_port,
    const std::uint16_t max_port) :  min_port_(min_port), max_port_(max_port),
addr_(boost::asio::ip::make_address(addr)) {}

bool RelayAddressGenerator::supportsIPv4() const noexcept {
    return addr_.is_v4();
}

bool RelayAddressGenerator::supportsIPv6() const noexcept {
    return addr_.is_v6();
}

std::optional<UdpRelayAllocation> RelayAddressGenerator::generateUdpRelay(
    const boost::asio::any_io_executor& executor, PortPolicy policy) const {

    const bool require_even =
        (policy == PortPolicy::Even || policy == PortPolicy::EvenAndReserveNext);
    const bool reserve_next =
        (policy == PortPolicy::EvenAndReserveNext);

    boost::system::error_code ec{};

    // According to RFC 6056 ("Recommendations for Transport-Protocol Port Randomization"),
    // ephemeral ports should be selected randomly to reduce predictability and improve security.
    // See: https://www.rfc-editor.org/rfc/rfc6056.html
    thread_local std::mt19937 rng(std::random_device{}());

    // --- Compute valid port bounds ---
    std::uint16_t port_min = min_port_;
    std::uint16_t port_max = max_port_;

    if (require_even) {
        port_min = (min_port_ + 1) & ~1;  // round up to next even ≥ min_port_
        port_max = max_port_ & ~1;        // round down to largest even ≤ max_port_
    }

    std::uniform_int_distribution<std::uint16_t> dist(
        require_even ? port_min / 2 : port_min,
        require_even ? port_max / 2 : port_max
    );

    for (int i = 0; i < max_retries; ++i) {
        std::uint16_t port = require_even ? dist(rng) * 2 : dist(rng);

        if (reserve_next && port + 1 > max_port_) {
            continue;
        }

        // Primary socket
        udp::socket primary(executor);
        primary.open(udp::endpoint(addr_, port).protocol(), ec);
        if (ec) continue;

        primary.bind(udp::endpoint(addr_, port), ec);
        if (ec) continue;

        // No reservation needed
        if (!reserve_next) {
            return UdpRelayAllocation{std::move(primary)};
        }

        // Secondary socket
        udp::socket secondary(executor);
        secondary.open(udp::endpoint(addr_, port + 1).protocol(), ec);
        if (ec) continue;

        secondary.bind(udp::endpoint(addr_, port + 1), ec);
        if (ec) continue;

        return UdpRelayAllocation{std::move(primary), std::move(secondary)};
    }

    return std::nullopt;
}

std::optional<TcpRelayAllocation> RelayAddressGenerator::generateTcpAcceptor(
    const boost::asio::any_io_executor& executor) const {

    // According to RFC 6056 ("Recommendations for Transport-Protocol Port Randomization"),
    // ephemeral TCP ports should be selected randomly to reduce predictability and improve security.
    // See: https://www.rfc-editor.org/rfc/rfc6056.html
    boost::system::error_code ec;
    thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<std::uint16_t> dist(min_port_, max_port_);

    for (int i = 0; i < max_retries; ++i) {
        std::uint16_t port = dist(rng);

        boost::asio::ip::tcp::acceptor acceptor(executor);
        acceptor.open(tcp::endpoint(addr_, port).protocol(), ec);
        if (ec) continue;

        acceptor.bind(tcp::endpoint(addr_, port), ec);
        if (ec) continue;

        return TcpRelayAllocation{std::move(acceptor)};
    }

    return std::nullopt;
}