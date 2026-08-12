# Single test executable for all test
add_executable(AllTests
        #../server/PermissionManager.cpp
        #../server/
        ../server/channel/PermissionManager.cpp
        ../server/channel/PermissionManager.hpp
        test/Permission_test.cpp
        test/PermissionManagerStress_test.cpp
        test/Authenticator_test.cpp
        # Add more test files here if needed
)

# Include project headers
target_include_directories(AllTests PRIVATE server)

# Link with GoogleTest, Boost, stun, OpenSSL
target_link_libraries(AllTests PRIVATE gtest_main ${Boost_LIBRARIES} stun OpenSSL::SSL)

# Automatically discover and register test
include(GoogleTest)
gtest_discover_tests(AllTests)