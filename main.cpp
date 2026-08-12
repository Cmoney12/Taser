#include <iostream>
#include <memory>
#include <filesystem>
#include <vector>
#include <thread>
#include <boost/asio.hpp>

#include "core/Server.hpp"
#include "core/Logging.hpp"

int main() {
    try {

        const std::filesystem::path config_path = std::filesystem::current_path() / "config.ini";
        const auto threads = std::thread::hardware_concurrency();
        std::shared_ptr<const Config> config = loadConfig(config_path.string());

#ifdef ENABLE_LOGGING
        init_logging(config->logging.level, config->logging.console_log);
#endif

        boost::asio::io_context io_context{};
        auto allocation_manager = std::make_shared<AllocationManager>(io_context, config);

        const auto server = std::make_shared<Server>(io_context, config, allocation_manager);
        server->start();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto ec, const int signal_number) {
            std::cout << "Caught signal " << ::strsignal(signal_number) << "\n";
            server->stop();
            io_context.stop();
        });

        std::vector<std::thread> v;
        v.reserve(threads - 1);
        for (auto i = threads - 1; i > 0; --i)
            v.emplace_back(
            [&io_context] {
                io_context.run();
            });
        io_context.run();

        for (auto& t: v) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
