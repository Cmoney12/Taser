#ifndef TASER_UDPALLOCATION_HPP
#define TASER_UDPALLOCATION_HPP

#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <stunxx/ChannelData.hpp>
#include "Allocation.hpp"
#include "channel/ChannelManager.hpp"
#include "config/Config.hpp"

using boost::asio::ip::udp;

// need a common interface for allocations
class UdpAllocation final : public Allocation {
public:

    UdpAllocation(
        const std::shared_ptr<const Config>& config,
        const FiveTuple& five_tuple,
        SendToClientFn send_cb,
        ExpiryCallback expiry_cb,
        udp::socket&& socket);

    ~UdpAllocation() override = default;

    void start(long seconds) override;
    void stop() override;

    boost::asio::awaitable<void> sendToPeer(const Message& message, const Endpoint& peer) override;

    boost::asio::awaitable<void> processPeerMessage(const Message& message, const Endpoint& peer) const;

    std::string address() const override;
    bool supportsFamily(stunxx::AddressFamily address_family) const override;
    std::uint16_t port() const override;
    Endpoint endpoint() const override;
    stunxx::Protocol protocol() const override;
    // channel ops
    bool addChannel(std::uint16_t channel_number, const Endpoint& peer) override;
    bool checkChannel(std::uint16_t channel_num) override;
    std::optional<std::uint16_t> getChannelByAddress(const Endpoint& peer) override;
    std::optional<Endpoint> getPeerByChannel(std::uint16_t channel_number) override;

private:

    boost::asio::awaitable<void> reader();

    boost::asio::awaitable<void> writer();

    udp::socket socket_;
    boost::asio::experimental::concurrent_channel<void(boost::system::error_code,
        Message, udp::endpoint)> channel_;
    std::shared_ptr<ChannelManager> channel_manager_;
    SendToClientFn send_to_client_fn_;
};

#endif //TASER_UDPALLOCATION_HPP