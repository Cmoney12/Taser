#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include "../server/channel/PermissionManager.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

class PermissionManagerStressTest : public ::testing::Test {
protected:
    boost::asio::io_context io;
    std::shared_ptr<PermissionManager> manager;

    void SetUp() override {
        // Short lifetime (1 second) for stress testing
        manager = std::make_shared<PermissionManager>(io.get_executor(), 1);
    }

    void runIoFor(std::chrono::milliseconds duration) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < duration) {
            io.run_for(10ms);
            io.restart();
        }
    }
};

TEST_F(PermissionManagerStressTest, HighConcurrencyStress) {
    constexpr int num_threads = 10;
    constexpr int num_permissions_per_thread = 50;

    std::atomic<int> added_count{0};
    std::atomic<int> expired_count{0};

    // Worker function to add and refresh permissions
    auto worker = [this, &added_count]() {
        for (int i = 0; i < num_permissions_per_thread; ++i) {
            auto addr_str = "127.0." + std::to_string(rand() % 255) + "." + std::to_string(rand() % 255);
            auto addr = boost::asio::ip::make_address(addr_str);

            manager->addPermission(addr);
            added_count.fetch_add(1, std::memory_order_relaxed);

            // Randomly refresh some permissions
            if (rand() % 2 == 0) {
                std::this_thread::sleep_for(10ms);
                manager->addPermission(addr);
            }
        }
    };

    // Start threads
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }

    // Run IO context in main thread concurrently
    std::thread io_thread([this] {
        runIoFor(3000ms); // 3 seconds of timer processing
    });

    // Join all worker threads
    for (auto& th : threads) {
        th.join();
    }

    io_thread.join();

    // Count expired permissions
    for (int a = 0; a < 255; ++a) {
        for (int b = 0; b < 255; ++b) {
            auto addr_str = "127.0." + std::to_string(a) + "." + std::to_string(b);
            auto addr = boost::asio::ip::make_address(addr_str);
            if (!manager->hasPermission(addr)) {
                expired_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Print counts for debugging
    std::cout << "Permissions added: " << added_count << "\n";
    std::cout << "Permissions expired: " << expired_count << "\n";

    // All permissions should have expired eventually
    EXPECT_EQ(manager->hasPermission(boost::asio::ip::make_address("127.0.0.1")), false);
}