#ifndef TASER_AUTHENTICATOR_HPP
#define TASER_AUTHENTICATOR_HPP

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/md5.h>
#include <openssl/core_names.h>

#include <array>
#include <cstdint>
#include <chrono>
#include <string>
#include <string_view>
#include <span>
#include <cstring>
#include <charconv>
#include <vector>
#include <memory>
#include "config/Config.hpp"
#include <stunxx/attributes/PasswordAlgorithmsAttr.hpp>

class Authenticator {
public:
    explicit Authenticator(std::shared_ptr<const Config> config);
    ~Authenticator() = default;

    static constexpr std::array<stunxx::PasswordAlgorithmEntry, 2> supportedAlgorithms {{
        {stunxx::PasswordAlgorithm::SHA256, {}},
        {stunxx::PasswordAlgorithm::MD5, {}}
    }};

    std::optional<std::string> getPassword(const std::string& username) const;

    std::string generateNonce(std::int64_t expiration_ts = 0) const;

    bool validateNonce(const std::string& nonce) const;

    static bool hasPasswordAlgorithmsBit(const std::string& nonce);

    // message has to be
    static bool validateMessageIntegrity(std::span<const std::uint8_t> messageUpToMI,
                                         std::span<const std::uint8_t> receivedHmac,
                                         const std::span<const std::uint8_t> key,
                                         bool useSha256 = false);

    static bool validatePasswordAlgorithms(const stunxx::PasswordAlgorithmsAttr &algorithms) ;

    static std::string base64Encode(const std::uint8_t* data, std::size_t length);

    static std::vector<std::uint8_t> base64Decode(const std::string& b64);

    static std::int64_t currentUnixTS();

private:
    std::array<unsigned char, 16> secret_key{};
    std::shared_ptr<const Config> config_;
};


#endif //TASER_AUTHENTICATOR_HPP