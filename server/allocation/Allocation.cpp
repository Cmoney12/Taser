#include "Allocation.hpp"

Allocation::Allocation(const boost::asio::any_io_executor& exec,
                       const std::shared_ptr<const Config>& config,
                       FiveTuple  five_tuple,
                       ExpiryCallback expiry_cb)
    : allocation_lifetime_(config->turn.allocation_lifetime),
      timer_(exec),
      five_tuple_(std::move(five_tuple)),
      expiry_cb_(std::move(expiry_cb)),
      permission_manager_(std::make_shared<PermissionManager>(exec, config->turn.permission_lifetime)) {
}

void Allocation::refresh(const long seconds) {
    timer_.expires_after(std::chrono::seconds(seconds));
    timer_.async_wait(expiry_cb_);
}

long Allocation::allocationLifetime() const noexcept {
    return allocation_lifetime_;
}

bool Allocation::expired() const {
    return timer_.expiry() <= std::chrono::steady_clock::now();
}

void Allocation::addPermission(const boost::asio::ip::address& peer_addr) const {
    permission_manager_->addPermission(peer_addr);
}

bool Allocation::hasPermission(const boost::asio::ip::address& peer_addr) const {
    return permission_manager_->hasPermission(peer_addr);
}

bool Allocation::addChannel(std::uint16_t channel_number, const Endpoint& peer) {
    return false;
}

bool Allocation::checkChannel(const std::uint16_t channel_num) {
    return false;
}

std::optional<std::uint16_t> Allocation::getChannelByAddress(const Endpoint& peer) {
    return std::nullopt;
}

std::optional<Endpoint> Allocation::getPeerByChannel(std::uint16_t channel_number) {
    return std::nullopt;
}

Message Allocation::successMessage() const {
    return success_message_;
}

void Allocation::successMessage(const Message &message) {
    success_message_ = message;
}

std::span<const std::uint8_t, stunxx::STUN_TRANSACTION_ID_SIZE> Allocation::getTransactionId() const {
    if (success_message_.size < 20) {
        throw std::runtime_error("Message too small to contain a transaction ID");
    }
    return std::span<const std::uint8_t, stunxx::STUN_TRANSACTION_ID_SIZE>(&success_message_.buffer[8], 12);
}


