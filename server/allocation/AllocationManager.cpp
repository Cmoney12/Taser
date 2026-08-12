#include "AllocationManager.hpp"

AllocationManager::AllocationManager(boost::asio::io_context& io_context, std::shared_ptr<const Config> config)
: config_(config),
reservation_manager_(config->turn.reservation_lifetime),
relay_address_generator_(config->turn.relay_ip, config->turn.relay_port_min, config->turn.relay_port_max),
io_context_(io_context){}

std::shared_ptr<Allocation> AllocationManager::getAllocation(const FiveTuple& tuple) {
    std::shared_lock<std::shared_mutex> lock{mutex_};  // allow concurrent readers

    const auto it = session_map_.find(tuple);
    if (it == session_map_.end())
        return nullptr;

    return it->second;
}

std::shared_ptr<Allocation> AllocationManager::claimReservation(const ReservationToken &token,
    const FiveTuple &five_tuple,
    long lifetime,
    const SendToClientFn& send_cb) {

    auto sock = reservation_manager_.claimReservation(token);

    if (!sock) return nullptr;

    auto udp_alloc = std::make_shared<UdpAllocation>(
            config_,
            five_tuple,
            send_cb,
            [weak_self = weak_from_this(), five_tuple](const boost::system::error_code&) {
                if (const auto self = weak_self.lock()) {
                    self->expireAllocation(five_tuple);
                }
            },
            std::move(*sock));

    {
        std::unique_lock<std::shared_mutex> lock{mutex_};
        session_map_[five_tuple] = udp_alloc;
    }

    udp_alloc->start(lifetime);

    return udp_alloc;
}

void AllocationManager::expireAllocation(const FiveTuple &five_tuple) {
    std::unique_lock<std::shared_mutex> lock{mutex_};

    const auto it = session_map_.find(five_tuple);
    if (it == session_map_.end())
        return;

    if (it->second->expired()) {
        session_map_.erase(it);
    }

}

std::pair<std::shared_ptr<Allocation>, std::optional<ReservationToken>> AllocationManager::createAllocation(
    const FiveTuple &five_tuple,
    stunxx::AddressFamily address_family,
    const PortPolicy port_policy,
    const long lifetime,
    const SendToClientFn &send_cb) {

    auto relay_address = relay_address_generator_.generateUdpRelay(io_context_.get_executor(),
        port_policy);

    if (!relay_address) return {};

    std::optional<ReservationToken> reservation_token = std::nullopt;
    if (relay_address->hasSecondary()) {
        reservation_token = reservation_manager_.createReservation(
            relay_address->releaseSecondary());
    }

    auto udp_alloc = std::make_shared<UdpAllocation>(
            config_,
            five_tuple,
            send_cb,
            [weak_self = weak_from_this(), five_tuple](const boost::system::error_code&) {
                if (const auto self = weak_self.lock()) {
                    self->expireAllocation(five_tuple);
                }
            },
            std::move(relay_address->primary()));

    {
        std::unique_lock<std::shared_mutex> lock{mutex_};
        session_map_[five_tuple] = udp_alloc;
    }

    udp_alloc->start(lifetime);

    return {std::move(udp_alloc), reservation_token};

}

bool AllocationManager::supportsFamily(stunxx::AddressFamily address_family) const {
    switch (address_family) {
        case stunxx::AddressFamily::IPv4: return relay_address_generator_.supportsIPv4();
        case stunxx::AddressFamily::IPv6: return relay_address_generator_.supportsIPv6();
    }
    return false;
}
