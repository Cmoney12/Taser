#include "UdpAllocation.hpp"

UdpAllocation::UdpAllocation(
    const std::shared_ptr<const Config>& config,
    const FiveTuple& five_tuple,
    SendToClientFn send_cb,
    ExpiryCallback expiry_cb,
    udp::socket&& socket) : Allocation(socket.get_executor(), config,
                                       five_tuple, std::move(expiry_cb)),
socket_(std::move(socket)),
channel_(socket.get_executor(), 15),
channel_manager_(std::make_shared<ChannelManager>(socket.get_executor(), config->turn.channel_lifetime)),
send_to_client_fn_(std::move(send_cb)) {}

void UdpAllocation::start(const long seconds) {
    refresh(seconds);

    auto self = std::static_pointer_cast<UdpAllocation>(shared_from_this());

    co_spawn(socket_.get_executor(),
        [self] -> boost::asio::awaitable<void> {
            using namespace boost::asio::experimental::awaitable_operators;
            co_await (self->reader() || self->writer());
        },
        [](const std::exception_ptr &e) {
            if (e) {
                try { std::rethrow_exception(e); }
                catch (const std::exception&) {}
            }
        });
}

void UdpAllocation::stop() {
    socket_.cancel();
    channel_.close();
}

boost::asio::awaitable<void> UdpAllocation::sendToPeer(const Message& message, const Endpoint& peer) {
    const udp::endpoint udp_endpoint { peer.address(), peer.port() };

    co_await channel_.async_send(
        boost::system::error_code{},
        message,
        udp_endpoint,
        boost::asio::use_awaitable);
}

boost::asio::awaitable<void> UdpAllocation::processPeerMessage(const Message& message, const Endpoint& peer) const {
    // --- Check if there is a channel bound to this sender ---
    if (const auto channel = channel_manager_->channelByAddress(peer)) {
        Message channel_message{};

        stunxx::ChannelData channel_data{
            channel.value(),
            std::span(message.buffer.data(), message.size)
        };

        if (channel_data.encode(channel_message.buffer)) {
            channel_message.size = channel_data.totalSize();

            co_await send_to_client_fn_(
                channel_message,
                five_tuple_.endpoint());
        }
    }
    else if (hasPermission(peer.address())) {
        Message resp{};  // buffer for STUN message

        auto transaction_id = stunxx::generateTransactionId();

        // Build the STUN Data Indication
        auto builder = stunxx::StunMessageBuilder(
            stunxx::StunMethod::Data,
            stunxx::StunClass::Indication,
            transaction_id,
            resp.buffer
        );

        auto peerAttr = makeStunAddress(peer.address(), peer.port());

        std::vector<std::uint8_t> payload(message.buffer.begin(), message.buffer.begin() + message.size);

        const stunxx::Encoder& encoder = builder
                                         .add<stunxx::XorPeerAddrAttr>(transaction_id, peerAttr)
                                         .add<stunxx::DataAttr>(std::move(payload))
                                         .finalize();

        resp.size = encoder.totalSize();

        co_await send_to_client_fn_(resp, five_tuple_.endpoint());
    }
    co_return;
}

std::string UdpAllocation::address() const {
    return socket_.local_endpoint().address().to_string();
}

bool UdpAllocation::supportsFamily(const stunxx::AddressFamily address_family) const {
    const auto& addr = socket_.local_endpoint().address();
    return (address_family == stunxx::AddressFamily::IPv4 && addr.is_v4()) ||
           (address_family == stunxx::AddressFamily::IPv6 && addr.is_v6());
}

std::uint16_t UdpAllocation::port() const {
    return socket_.local_endpoint().port();
}

Endpoint UdpAllocation::endpoint() const {
    Endpoint endpoint(socket_.local_endpoint().address(), socket_.local_endpoint().port(), stunxx::Protocol::UDP);
    return endpoint;
}

stunxx::Protocol UdpAllocation::protocol() const {
    return stunxx::Protocol::UDP;
}

bool UdpAllocation::addChannel(const std::uint16_t channel_number, const Endpoint& peer) {
    if (!channel_manager_->addChannel(channel_number, peer)) {
        return false;
    }
    addPermission(peer.address());
    return true;
}

bool UdpAllocation::checkChannel(const std::uint16_t channel_num) {
     return channel_manager_->checkChannel(channel_num);
}

std::optional<std::uint16_t> UdpAllocation::getChannelByAddress(const Endpoint &peer) {
    return channel_manager_->channelByAddress(peer);
}

std::optional<Endpoint> UdpAllocation::getPeerByChannel(const std::uint16_t channel_number) {
   return channel_manager_->getEndpoint(channel_number);
}

boost::asio::awaitable<void> UdpAllocation::reader() {
    for (Message message{};;) {
        udp::endpoint sender;
        message.size = co_await socket_.async_receive_from(
            boost::asio::buffer(message.buffer),
            sender,
            boost::asio::use_awaitable);

        Endpoint peer{sender.address(), sender.port(), stunxx::Protocol::UDP};

        // --- Process the packet ---
        co_await processPeerMessage(message, peer);
    }
}

boost::asio::awaitable<void> UdpAllocation::writer() {
    while (socket_.is_open()) {

        auto [msg, endpoint] = co_await channel_.async_receive(boost::asio::use_awaitable);

        co_await socket_.async_send_to(boost::asio::buffer(msg.buffer, msg.size),
            endpoint, boost::asio::use_awaitable);
    }
}
