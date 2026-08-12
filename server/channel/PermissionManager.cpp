#include "PermissionManager.hpp"

PermissionManager::PermissionManager(
    boost::asio::any_io_executor io_context,
    const long lifetime_seconds)
: lifetime_seconds_(lifetime_seconds),
io_context_(std::move(io_context)) {}

PermissionManager::~PermissionManager() {
    decltype(permissions_) to_cancel;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        to_cancel = std::move(permissions_);
    }
}

void PermissionManager::addPermission(const boost::asio::ip::address& peer_addr) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    permissions_.erase(peer_addr); // cancel old if exists
    auto [it, _] = permissions_.emplace(peer_addr, boost::asio::steady_timer{io_context_});
    auto& timer = it->second;
    timer.expires_after(std::chrono::seconds(lifetime_seconds_));
    timer.async_wait([weak_self = weak_from_this(), peer_addr](const boost::system::error_code& ec) {
        if (ec) return;
        if (const auto self = weak_self.lock()) {
            self->removePermission(peer_addr);
        }
    });
}

void PermissionManager::removePermission(const boost::asio::ip::address& peer_addr) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto it = permissions_.find(peer_addr);
    if (it != permissions_.end() && it->second.expiry() <= std::chrono::steady_clock::now()) {
        permissions_.erase(it);
    }
}

bool PermissionManager::hasPermission(const boost::asio::ip::address& peer_addr) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return permissions_.contains(peer_addr);
}
