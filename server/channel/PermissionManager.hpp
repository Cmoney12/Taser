#ifndef TASER_PERMISSIONMANAGER_HPP
#define TASER_PERMISSIONMANAGER_HPP

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/address.hpp>
#include <unordered_map>
#include <memory>
#include <shared_mutex>

class PermissionManager : public std::enable_shared_from_this<PermissionManager> {
public:

    explicit PermissionManager(boost::asio::any_io_executor io_context,
        const long lifetime_seconds);

    ~PermissionManager();

    void addPermission(const boost::asio::ip::address& peer_addr);

    void removePermission(const boost::asio::ip::address& peer_addr);

    bool hasPermission(const boost::asio::ip::address& peer_addr) const;

private:
    long lifetime_seconds_;
    boost::asio::any_io_executor io_context_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<boost::asio::ip::address, boost::asio::steady_timer> permissions_;
};

#endif //TASER_PERMISSIONMANAGER_HPP
