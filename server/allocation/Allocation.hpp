#ifndef TASER_ALLOCATION_HPP
#define TASER_ALLOCATION_HPP

#include <span>
#include <memory>
#include <functional>
#include <boost/asio.hpp>
#include <optional>
#include <utility>
#include <boost/asio/experimental/channel.hpp>
#include <stunxx/stunxx.hpp>
#include "config/Config.hpp"
#include "core/FiveTuple.hpp"
#include "core/Message.hpp"
#include "core/Endpoint.hpp"
#include "channel/PermissionManager.hpp"

// client requests the allocation
// peer
using SendToClientFn =
    std::function<boost::asio::awaitable<void>(const Message& message, const Endpoint& endpoint)>;

class Allocation : public std::enable_shared_from_this<Allocation> {
public:

    using ExpiryCallback = std::function<void(const boost::system::error_code&)>;

    Allocation(
        const boost::asio::any_io_executor& exec,
        const std::shared_ptr<const Config>& config,
        FiveTuple five_tuple,
        ExpiryCallback expiry_cb);

    virtual ~Allocation() = default;

    virtual boost::asio::awaitable<void> sendToPeer(
        const Message& message,
        const Endpoint& peer) = 0;

    virtual void start(long seconds) = 0;
    virtual void stop() = 0;

    void refresh(long seconds);

    long allocationLifetime() const noexcept;

    bool expired() const;

    virtual std::string address() const = 0;
    virtual bool supportsFamily(stunxx::AddressFamily address_family) const = 0;
    virtual std::uint16_t port() const = 0;
    virtual Endpoint endpoint() const = 0;
    virtual stunxx::Protocol protocol() const = 0;

    // permission ops
    void addPermission(const boost::asio::ip::address& peer_addr) const;
    bool hasPermission(const boost::asio::ip::address& peer_addr) const;

    // channel ops
    virtual bool addChannel(std::uint16_t channel_number, const Endpoint& peer);
    virtual bool checkChannel(std::uint16_t channel_num);
    virtual std::optional<std::uint16_t> getChannelByAddress(const Endpoint& peer);
    virtual std::optional<Endpoint> getPeerByChannel(std::uint16_t channel_number);

    Message successMessage() const;
    void successMessage(const Message& message);

    std::span<const std::uint8_t, stunxx::STUN_TRANSACTION_ID_SIZE> getTransactionId() const;

protected:
    FiveTuple five_tuple_;

private:
    long allocation_lifetime_;
    boost::asio::steady_timer timer_;
    Message success_message_{};
    ExpiryCallback expiry_cb_;
    std::shared_ptr<PermissionManager> permission_manager_;
};

#endif //TASER_ALLOCATION_HPP
