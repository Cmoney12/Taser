#ifndef TASER_CONFIG_HPP
#define TASER_CONFIG_HPP

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

namespace pt = boost::property_tree;

struct Network {
    std::string bind_address = "0.0.0.0";
    std::uint16_t bind_port = 3478;
};

struct Auth {
    std::string realm = "example.com";
    long nonce_lifetime = 600;

    // username → password
    std::unordered_map<std::string, std::string> users;
};

struct Turn {
    long allocation_lifetime{600};
    long max_allocation_lifetime{3600};
    long channel_lifetime{600};
    long permission_lifetime{300};
    long reservation_lifetime{300};

    // Optional relay IP pool
    std::string relay_ip = "127.0.0.1";

    // Optional port range
    std::uint16_t relay_port_min{49152};
    std::uint16_t relay_port_max{65535};

    bool has_port_range() const {
        return relay_port_min && relay_port_max;
    }
};

#ifdef ENABLE_LOGGING
struct Logging {
    std::string level = "info";
    bool console_log = false;
};
#endif

struct Config {
    Network network;
    Auth auth;
    Turn turn;
#ifdef ENABLE_LOGGING
    Logging logging;
#endif
};

inline std::shared_ptr<const Config> loadConfig(const std::string& path) {
    pt::ptree tree;
    auto config = std::make_shared<Config>();

    try {
        pt::ini_parser::read_ini(path, tree);
    } catch (const pt::ini_parser_error& e) {
        std::cerr << "Could not read config file " << path << std::endl;
        return config;
    }

    config->network.bind_address = tree.get<std::string>("network.bind_address",
        config->network.bind_address);

    config->network.bind_port = tree.get<std::uint16_t>("network.bind_port",
        config->network.bind_port);

    auto ip_str = tree.get<std::string>("network.ip_version", "ipv4");

    config->auth.realm = tree.get<std::string>("realm", config->auth.realm);

    config->auth.nonce_lifetime = tree.get<long>(
       "auth.nonce_lifetime", config->auth.nonce_lifetime);

    config->auth.users.clear();
    if (const auto users_node = tree.get_child_optional("users")) {
        for (const auto& [fst, snd] : *users_node) {
            const std::string& username = fst;
            const std::string& password = snd.get_value<std::string>();
            config->auth.users.emplace(username, password);
        }
    }

    auto& turn = config->turn;

    turn.allocation_lifetime = tree.get<long>(
        "turn.allocation_lifetime", turn.allocation_lifetime);

    turn.max_allocation_lifetime = tree.get<long>(
        "turn.max_allocation_lifetime", turn.max_allocation_lifetime);

    turn.channel_lifetime = tree.get<long>(
        "turn.channel_lifetime", turn.channel_lifetime);

    turn.permission_lifetime = tree.get<long>(
        "turn.permission_lifetime", turn.permission_lifetime);

    turn.reservation_lifetime = tree.get<long>(
        "turn.reservation_lifetime", turn.reservation_lifetime);

    // Optional relay IPs
    turn.relay_ip = tree.get<std::string>("turn.relay_ip", config->turn.relay_ip);

    // Relay port range
    turn.relay_port_min = tree.get<std::uint16_t>(
        "turn.relay_port_min", turn.relay_port_min);

    turn.relay_port_max = tree.get<std::uint16_t>(
        "turn.relay_port_max", turn.relay_port_max);

#ifdef ENABLE_LOGGING
    config->logging.level = tree.get<std::string>("logging.level", config->logging.level);
    config->logging.console_log = tree.get<bool>("logging.console_log", false);
#endif

    return config;
}



#endif //TASER_CONFIG_HPP