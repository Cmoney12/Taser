#include "Endpoint.hpp"

Endpoint::Endpoint(boost::asio::ip::address address,
                   const std::uint16_t port, const stunxx::Protocol protocol)
    : address_(std::move(address)), port_(port), protocol_(protocol) {}

const boost::asio::ip::address& Endpoint::address() const {
    return address_;
}

std::uint16_t Endpoint::port() const {
    return port_;
}

stunxx::Protocol Endpoint::protocol() const {
    return protocol_;
}

bool Endpoint::operator==(const Endpoint& other) const noexcept {
    return address_ == other.address_ &&
        port_ == other.port_ &&
            protocol_ == other.protocol_;
}
