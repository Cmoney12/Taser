#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include "../server/channel/PermissionManager.hpp"
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

class PermissionManagerTest : public ::testing::Test {
protected:
    boost::asio::io_context io;
    std::shared_ptr<PermissionManager> manager;

    void SetUp() override {
        // Use a short lifetime (2 seconds) for testing
        manager = std::make_shared<PermissionManager>(io.get_executor(), 2);
    }

    // Helper to process io_context timers
    void runIo() {
        io.run_for(10ms); // process any pending async tasks/timers
        io.restart();
    }
};

TEST_F(PermissionManagerTest, PermissionAddedSuccessfully) {
    auto addr = boost::asio::ip::make_address("127.0.0.1");
    manager->addPermission(addr);
    EXPECT_TRUE(manager->hasPermission(addr));
}

TEST_F(PermissionManagerTest, PermissionExpiresAfterLifetime) {
    auto addr = boost::asio::ip::make_address("127.0.0.1");
    manager->addPermission(addr);
    EXPECT_TRUE(manager->hasPermission(addr));

    // Wait slightly longer than lifetime
    std::this_thread::sleep_for(2100ms);
    runIo();

    EXPECT_FALSE(manager->hasPermission(addr));
}

TEST_F(PermissionManagerTest, PermissionRefreshPreventsExpiry) {
    auto addr = boost::asio::ip::make_address("127.0.0.1");
    manager->addPermission(addr);
    EXPECT_TRUE(manager->hasPermission(addr));

    // Wait 1 second (half-life), then refresh
    std::this_thread::sleep_for(1000ms);
    manager->addPermission(addr); // refresh
    runIo();

    // Wait 1.5 seconds (still within refreshed lifetime)
    std::this_thread::sleep_for(1500ms);
    runIo();
    EXPECT_TRUE(manager->hasPermission(addr));

    // Wait another 1 second (total past refreshed lifetime)
    std::this_thread::sleep_for(1100ms);
    runIo();
    EXPECT_FALSE(manager->hasPermission(addr));
}

TEST_F(PermissionManagerTest, RemoveDoesNotDeleteActivePermission) {
    auto addr = boost::asio::ip::make_address("127.0.0.1");
    manager->addPermission(addr);
    EXPECT_TRUE(manager->hasPermission(addr));

    // Attempt to remove before expiry
    manager->removePermission(addr);
    EXPECT_TRUE(manager->hasPermission(addr)); // still exists

    // Wait past lifetime for proper expiry
    std::this_thread::sleep_for(2100ms);
    runIo();
    EXPECT_FALSE(manager->hasPermission(addr));
}