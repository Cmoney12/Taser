#include "AuthKey.hpp"

constexpr std::size_t kMd5Len = 16;
constexpr std::size_t kSha256Len = 32;

AuthKey::AuthKey(const stunxx::PasswordAlgorithm alg, std::span<const std::uint8_t> key)
: algorithm_(alg), key_len_(key.size()) {
    assert(key.size() <= key_.size());
    std::ranges::copy(key, key_.begin());
}

AuthKey AuthKey::md5(const std::span<const std::uint8_t> key) {
    assert(key.size() == kMd5Len);
    return AuthKey{stunxx::PasswordAlgorithm::MD5, key};
}

AuthKey AuthKey::sha256(const std::span<const std::uint8_t> key) {
    assert(key.size() == kSha256Len);
    return AuthKey{stunxx::PasswordAlgorithm::SHA256, key};
}