#ifndef TASER_STUNTURNPROCESSOR_HPP
#define TASER_STUNTURNPROCESSOR_HPP

#include "Message.hpp"
#include "allocation/AllocationManager.hpp"
#include "auth/Authenticator.hpp"
#include "config/Config.hpp"
#include "Endpoint.hpp"
#include "Logging.hpp"
#include "auth/AuthKey.hpp"
#include <stunxx/stunxx.hpp>

#include <functional>
#include <utility>

using SendFunc =
    std::function<boost::asio::awaitable<void>(const Message& message, const Endpoint& endpoint)>;

class StunTurnProcessor {
public:
    explicit StunTurnProcessor(const std::shared_ptr<AllocationManager>& allocation_manager,
        std::shared_ptr<const Config> config,
        SendFunc sendPacket);

    boost::asio::awaitable<void> processPacket(const Message& message, const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleBindingRequest(const stunxx::Decoder& decoder,
        const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleAllocationRequest(stunxx::Decoder& decoder,
                                                         const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleRefreshRequest(const stunxx::Decoder& decoder,
        const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handlePermissionRequest(const stunxx::Decoder& decoder,
        const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleChannelBindRequest(const stunxx::Decoder& decoder,
        const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleChannelData(const Message& message, const FiveTuple& five_tuple) const;

    boost::asio::awaitable<void> handleSendIndication(const stunxx::Decoder& decoder, const FiveTuple& five_tuple) const;

    boost::asio::awaitable<std::optional<AuthKey>> authenticate(const stunxx::Decoder& decoder,
        const Endpoint& endpoint) const;

    long computeLifetime(const stunxx::Decoder& decoder) const;

    static boost::asio::ip::address toAsioAddress(std::span<const std::uint8_t> addr,
        stunxx::AddressFamily address_family);

private:

    boost::asio::awaitable<void> sendErrorMessage(const stunxx::Decoder& decoder,
                                                  stunxx::StunErrorCode ec,
                                                  const Endpoint& endpoint,
                                                  std::optional<AuthKey> auth_key = std::nullopt) const;

    Authenticator authenticator_;
    std::shared_ptr<AllocationManager> allocation_manager_;
    SendFunc sendPacket_;
    std::shared_ptr<const Config> config_;
};

#endif //TASER_STUNTURNPROCESSOR_HPP