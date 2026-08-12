#ifndef TASER_CHANNELMANAGER_HPP
#define TASER_CHANNELMANAGER_HPP

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/address.hpp>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <optional>
#include <ranges>

#include "core/Endpoint.hpp"

struct ChannelEntry {
    Endpoint endpoint;
    boost::asio::steady_timer timer;
};

class ChannelManager : public std::enable_shared_from_this<ChannelManager> {
public:
    ChannelManager(boost::asio::any_io_executor io_context,
        const long lifetime_seconds);

    ~ChannelManager();

    bool addChannel(std::uint16_t channel_number, const Endpoint& peer);

    bool checkChannel(std::uint16_t channel_number) const;

    std::optional<Endpoint> getEndpoint(std::uint16_t channel_number) const;

    std::optional<std::uint16_t> channelByAddress(const Endpoint& peer);

    void removeChannel(std::uint16_t channel_number);

private:
    long lifetime_seconds_;
    boost::asio::any_io_executor io_context_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::uint16_t, ChannelEntry> channels_;
};

#endif //TASER_CHANNELMANAGER_HPP