#include "FiveTuple.hpp"

FiveTuple::FiveTuple(boost::asio::ip::address client_addr,
                     std::uint16_t client_port,
                     boost::asio::ip::address server_addr,
                     std::uint16_t server_port,
                     stunxx::Protocol protocol) : client_address_(std::move(client_addr)),
                                                  client_port_(client_port),
                                                  server_address_(std::move(server_addr)),
                                                  server_port_(server_port),
                                                  protocol_(protocol){}

FiveTuple FiveTuple::fromUdp(const boost::asio::ip::udp::endpoint& client,
    const boost::asio::ip::udp::endpoint& server) {

    return {client.address(),
            client.port(),
            server.address(),
            server.port(),
            stunxx::Protocol::UDP};
}

FiveTuple FiveTuple::fromTcp(const boost::asio::ip::tcp::endpoint& client,
    const boost::asio::ip::tcp::endpoint& server) {

    return {client.address(),
        client.port(),
        server.address(),
        server.port(),
        stunxx::Protocol::TCP};

}

const boost::asio::ip::address& FiveTuple::clientAddress() const {
    return client_address_;
}

std::uint16_t FiveTuple::clientPort() const {
    return client_port_;
}

const boost::asio::ip::address& FiveTuple::serverAddress() const {
    return server_address_;
}

std::uint16_t FiveTuple::serverPort() const {
    return server_port_;
}

stunxx::Protocol FiveTuple::protocol() const noexcept {
    return protocol_;
}

Endpoint FiveTuple::endpoint() const noexcept {
    return { client_address_,
        client_port_,
        protocol_};
}

