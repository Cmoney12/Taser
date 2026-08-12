#ifndef TASER_SERVER_HPP
#define TASER_SERVER_HPP

#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include "Message.hpp"
#include "StunTurnProcessor.hpp"
#include "FiveTuple.hpp"
#include "config/Config.hpp"
#include "Endpoint.hpp"

using boost::asio::ip::udp;

class Server : public std::enable_shared_from_this<Server> {
public:
    Server(boost::asio::io_context& io_context, std::shared_ptr<const Config> config,
        const std::shared_ptr<AllocationManager>& allocation_manager);

    void start();

    void stop();

    // asyncSend(const Message& message, const FiveTuple& five_tuple);
    boost::asio::awaitable<void> asyncSend(Message msg, Endpoint endpoint);

private:

    boost::asio::awaitable<void> reader();

    boost::asio::awaitable<void> writer();

    udp::socket socket_;
    boost::asio::experimental::concurrent_channel<void(boost::system::error_code, Message, udp::endpoint)> channel_;
    StunTurnProcessor stun_turn_processor_;
    std::shared_ptr<const Config> config_;
};


#endif //TASER_SERVER_HPP