cmake_minimum_required(VERSION 3.30)
project(Taser)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED)

find_package(OpenSSL)
if(OPENSSL_FOUND)
    #target_include_directories(${PROJECT_NAME} PUBLIC ${OPENSSL_INCLUDE_DIR})
    #target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES})
    message("Openssl found")
endif()

add_subdirectory(libstunxx)

configure_file(config/config.ini ${CMAKE_BINARY_DIR}/config.ini COPYONLY)

# ---- Server library (shared by the main executable and test) ----
add_library(taser_core STATIC
        server/core/Server.hpp
        server/core/Server.cpp
        server/core/StunTurnProcessor.hpp
        server/core/StunTurnProcessor.cpp
        server/core/FiveTuple.hpp
        server/core/FiveTuple.cpp
        server/core/Endpoint.hpp
        server/core/Endpoint.cpp
        server/core/Message.hpp
        server/core/Logging.hpp

        server/auth/Authenticator.hpp
        server/auth/Authenticator.cpp
        server/auth/AuthKey.hpp
        server/auth/AuthKey.cpp

        server/allocation/Allocation.hpp
        server/allocation/Allocation.cpp
        server/allocation/AllocationManager.hpp
        server/allocation/AllocationManager.cpp
        server/allocation/UdpAllocation.hpp
        server/allocation/UdpAllocation.cpp
        server/allocation/RelayAllocation.hpp
        server/allocation/RelayAddressGenerator.hpp
        server/allocation/RelayAddressGenerator.cpp
        server/allocation/ReservationManager.hpp
        server/allocation/ReservationManager.cpp

        server/channel/ChannelManager.hpp
        server/channel/ChannelManager.cpp
        server/channel/PermissionManager.hpp
        server/channel/PermissionManager.cpp

        server/config/Config.hpp
)

target_include_directories(taser_core PUBLIC
        ${CMAKE_SOURCE_DIR}/server
)

target_link_libraries(taser_core PUBLIC stunxx OpenSSL::SSL Boost::boost)

# ---- Main executable ----
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE taser_core)

# optional logging
option(ENABLE_LOGGING "Enable spdlog-based logging" OFF)
if(ENABLE_LOGGING)
    find_package(spdlog CONFIG REQUIRED)
    Message("splog")
    # include(FetchContent)
    # FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.14.1)
    # FetchContent_MakeAvailable(spdlog)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(taser_core PUBLIC ENABLE_LOGGING SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
    else()
        target_compile_definitions(taser_core PUBLIC ENABLE_LOGGING SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
    endif()

    target_link_libraries(taser_core PUBLIC spdlog::spdlog)
endif()

# ---- Tests ----
option(BUILD_TESTING "Build the tests" ON)
include(CTest)

if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(AllTests
            test/Permission_test.cpp
            test/PermissionManagerStress_test.cpp
            test/Authenticator_test.cpp
            # add more test files here as needed
    )

    target_link_libraries(AllTests PRIVATE taser_core gtest_main)

    include(GoogleTest)
    gtest_discover_tests(AllTests)
endif()
