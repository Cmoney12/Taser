#include "Server.hpp"

Server::Server(boost::asio::io_context& io_context, std::shared_ptr<const Config> config,
               const std::shared_ptr<AllocationManager>& allocation_manager)
    : socket_(io_context,
        udp::endpoint(boost::asio::ip::make_address(config->network.bind_address),
            config->network.bind_port)),
    channel_(io_context, 128),
    stun_turn_processor_(allocation_manager, config,
        [this](const Message& message, const Endpoint& endpoint) -> boost::asio::awaitable<void> {
            co_await this->asyncSend(message, endpoint);
}), config_(std::move(config)) {}

void Server::start() {
    /**boost::asio::co_spawn(socket_.get_executor(),
        [self = shared_from_this()]() -> boost::asio::awaitable<void> {
            using namespace boost::asio::experimental::awaitable_operators;
            co_await (self->reader() || self->writer());
        }, boost::asio::detached);**/

    LOG_INFO("UDP server started listening on: {}:{}", socket_.local_endpoint().address().to_string(),
        socket_.local_endpoint().port());

    co_spawn(socket_.get_executor(),
        [self = shared_from_this()] -> boost::asio::awaitable<void> {
            using namespace boost::asio::experimental::awaitable_operators;
            co_await (self->reader() || self->writer());
        },
        [](const std::exception_ptr &e) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                }
                catch (const std::exception& ex) {
                    LOG_CRITICAL("Caught Server Exception {}", ex.what());
                }
            }
        });
}

void Server::stop() {
    socket_.cancel();
    channel_.close();
}

boost::asio::awaitable<void> Server::asyncSend(const Message msg, const Endpoint endpoint) {
    // Attempt to send a message when no receiver is active
    if (!channel_.is_open()) co_return;

    const udp::endpoint udp_endpoint { endpoint.address(), endpoint.port() };

    co_await channel_.async_send(
        boost::system::error_code{},
        msg,
        udp_endpoint,
        boost::asio::use_awaitable);
}

boost::asio::awaitable<void> Server::reader() {
    for (Message message{};;) {
        udp::endpoint endpoint;

        auto [ec, size] = co_await socket_.async_receive_from(
            boost::asio::buffer(message.buffer), endpoint,
            boost::asio::as_tuple(boost::asio::use_awaitable));

        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("Server reader error: {}", ec.message());
            }
            co_return;
        }

        message.size = size;

        FiveTuple key = FiveTuple::fromUdp(endpoint, socket_.local_endpoint());

        co_await stun_turn_processor_.processPacket(message, key);
    }
}

boost::asio::awaitable<void> Server::writer() {
    while (socket_.is_open()) {
        auto [rec_ec, msg, endpoint] = co_await channel_.async_receive(
            boost::asio::as_tuple(boost::asio::use_awaitable));

        if (rec_ec) {
            if (rec_ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("Server writer channel error: {}", rec_ec.message());
            }
            co_return;
        }

        auto [send_ec, bytes] = co_await socket_.async_send_to(
            boost::asio::buffer(msg.buffer, msg.size), endpoint,
            boost::asio::as_tuple(boost::asio::use_awaitable));

        if (send_ec) {
            if (send_ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("Server writer send error: {}", send_ec.message());
            }
            co_return;
        }
    }
}

