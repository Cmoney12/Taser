#ifndef TASER_AUTHKEY_HPP
#define TASER_AUTHKEY_HPP

#include <array>
#include <span>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <stunxx/Stun.hpp>

class AuthKey {
public:
    static AuthKey md5(std::span<const std::uint8_t> key);
    static AuthKey sha256(std::span<const std::uint8_t> key);

    stunxx::PasswordAlgorithm algorithm() const { return algorithm_; }
    std::span<const std::uint8_t> span() const { return {key_.data(), key_len_}; }

private:
    AuthKey(stunxx::PasswordAlgorithm alg, std::span<const std::uint8_t> key);
    stunxx::PasswordAlgorithm algorithm_;
    std::array<std::uint8_t, 32> key_{};  // 32 = SHA256_DIGEST_LENGTH, covers MD5's 16 too
    std::size_t key_len_;
};

#endif //TASER_AUTHKEY_HPP
