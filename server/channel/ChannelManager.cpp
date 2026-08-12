#include "ChannelManager.hpp"


ChannelManager::ChannelManager(boost::asio::any_io_executor io_context,
                               const long lifetime_seconds)
: io_context_(std::move(io_context)), lifetime_seconds_(lifetime_seconds) {}

ChannelManager::~ChannelManager() {
    decltype(channels_) to_cancel;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        to_cancel = std::move(channels_);
    }
}

bool ChannelManager::addChannel(std::uint16_t channel_number, const Endpoint& peer) {
    std::unique_lock lock(mutex_);
    if (auto it = channels_.find(channel_number); it != channels_.end()) {
        auto& entry = it->second;

        // channel is occupied by a different peer
        if (entry.endpoint != peer) {
            return false;
        }

        // update the timer if it doesn't
        entry.timer.expires_after(std::chrono::seconds(lifetime_seconds_));
        entry.timer.async_wait(
            [weak_self = weak_from_this(), channel_number] (boost::system::error_code ec) {
                if (ec) return;

                if (const auto self = weak_self.lock()) {
                    self->removeChannel(channel_number);
                }
        });

        return true;
    }

    // peer already bound to a different address
    if (std::ranges::any_of(channels_ | std::views::values,
        [&](const ChannelEntry& e) {
            return e.endpoint == peer;})) {
        return false;
    }

    ChannelEntry entry{
        .endpoint = peer,
        .timer = boost::asio::steady_timer{io_context_},
    };

    entry.timer.expires_after(std::chrono::seconds(lifetime_seconds_));
    entry.timer.async_wait(
        [weak_self = weak_from_this(), channel_number](boost::system::error_code ec) {
            if (ec) return;
            if (const auto self = weak_self.lock()) {
                self->removeChannel(channel_number);
            }
        });

    channels_.emplace(channel_number, std::move(entry));

    return true;
}

bool ChannelManager::checkChannel(std::uint16_t channel_number) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return channels_.contains(channel_number);
}

std::optional<Endpoint> ChannelManager::getEndpoint(const std::uint16_t channel_number) const {
    std::shared_lock lock(mutex_);
    const auto it = channels_.find(channel_number);
    if (it == channels_.end()) return std::nullopt;
    return it->second.endpoint;
}

std::optional<std::uint16_t> ChannelManager::channelByAddress(const Endpoint& peer) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = std::ranges::find_if(
        channels_,
        [&](const auto& pair) {
            return pair.second.endpoint == peer;
        });

    if (it == channels_.end()) {
        return std::nullopt;
    }

    return it->first; // channel number
}

void ChannelManager::removeChannel(std::uint16_t channel_number) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto it = channels_.find(channel_number);
    if (it != channels_.end() && it->second.timer.expiry() <= std::chrono::steady_clock::now()) {
        channels_.erase(it);
    }
}
