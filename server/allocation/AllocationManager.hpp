#ifndef TASER_ALLOCATIONMANAGER_HPP
#define TASER_ALLOCATIONMANAGER_HPP

// TODO set up allocation manager to replace session manager
// set up authenticator to auth client through nonce = base64(expiration || HMAC(k, expiration))
// k is the shared secret hex encoded 32 byte secret
// create a permissions class that has a timer to prune
// add a config for [auth] realm
// [auth.users] user1 = username:password

#include "Allocation.hpp"
#include "UdpAllocation.hpp"
#include "core/FiveTuple.hpp"
#include "ReservationManager.hpp"
#include "RelayAddressGenerator.hpp"
#include "config/Config.hpp"

#include <unordered_map>
#include <memory>
#include <mutex>
#include <utility>

using boost::asio::ip::udp;

class AllocationManager : public std::enable_shared_from_this<AllocationManager> {
public:
    explicit AllocationManager(boost::asio::io_context& io_context, std::shared_ptr<const Config> config);

    std::shared_ptr<Allocation> getAllocation(const FiveTuple& tuple);

    std::shared_ptr<Allocation> claimReservation(const ReservationToken& token,
        const FiveTuple& five_tuple,
        long lifetime,
        const SendToClientFn& send_cb);

    void expireAllocation(const FiveTuple& five_tuple);

    std::pair<std::shared_ptr<Allocation>, std::optional<ReservationToken>>
    createAllocation(const FiveTuple& five_tuple,
        stunxx::AddressFamily address_family,
        PortPolicy port_policy,
        long lifetime,
        const SendToClientFn& send_cb);

    bool supportsFamily(stunxx::AddressFamily address_family) const;

private:

    std::shared_ptr<const Config> config_;
    std::unordered_map<FiveTuple, std::shared_ptr<Allocation>> session_map_;
    mutable std::shared_mutex mutex_;
    ReservationManager reservation_manager_;
    RelayAddressGenerator relay_address_generator_;
    boost::asio::io_context& io_context_;
};

#endif //TASER_ALLOCATIONMANAGER_HPP