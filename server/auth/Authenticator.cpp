#include "Authenticator.hpp"
#include "stunxx/Stun.hpp"


Authenticator::Authenticator(std::shared_ptr<const Config> config) : config_(std::move(config)) {
    // Generate 32 secure random bytes
    if (RAND_bytes(secret_key.data(), secret_key.size()) != 1) {
        throw std::runtime_error("Failed to generate secure secret key");
    }
}

std::optional<std::string> Authenticator::getPassword(const std::string& username) const {
    auto it = config_->auth.users.find(username);
    if (it != config_->auth.users.end())
        return it->second;
    return std::nullopt;
}

std::string Authenticator::generateNonce(std::int64_t expiration_ts) const {
    if (expiration_ts == 0)
        expiration_ts = currentUnixTS() + 3600;

    std::string expiration_str = std::to_string(expiration_ts);

    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         secret_key.data(), secret_key.size(),
         reinterpret_cast<const unsigned char*>(expiration_str.data()),
         expiration_str.size(),
         hmac, &hmac_len);

    // Build the actual nonce payload
    std::vector<std::uint8_t> raw_nonce(expiration_str.size() + hmac_len);
    std::memcpy(raw_nonce.data(), expiration_str.data(), expiration_str.size());
    std::memcpy(raw_nonce.data() + expiration_str.size(), hmac, hmac_len);

    // RFC 8489 §9.2 nonce cookie:
    // "obMatJos2" + base64(24-bit security features) + <actual nonce>
    //
    // Bit 0 (MSB of byte 0) = Password algorithms
    // Bit 1                  = Username anonymity
    // for password algorithms set 0b10000000,
    constexpr std::array<uint8_t, 3> security_features = {
        0b10000000,  // set bit 0 here if supporting PASSWORD-ALGORITHMS
        0b00000000,
        0b00000000,
    };

    return std::string("obMatJos2")
         + base64Encode(security_features.data(), 3)
         + base64Encode(raw_nonce.data(), raw_nonce.size());
}

bool Authenticator::validateNonce(const std::string& nonce) const {

    constexpr std::size_t COOKIE_LEN = 13; // 9 + 4
    if (nonce.size() <= COOKIE_LEN) return false;

    // Optionally: decode and inspect the security features bits here
    // base64Decode(nonce.substr(9, 4)) → 3 bytes of feature bits
    const std::string actual_nonce = nonce.substr(COOKIE_LEN);

    auto raw = base64Decode(actual_nonce);

    constexpr std::size_t MIN_SIZE = 10 + 32; // timestamp + HMAC-SHA256
    if (raw.size() < MIN_SIZE) return false;

    // Find the end of the numeric prefix (expiration timestamp)
    auto it = std::find_if_not(raw.begin(), raw.end(),
                               [](const unsigned char c) { return std::isdigit(c); });

    if (it == raw.begin()) return false; // no digits at start

    const std::string_view expiration_sv(
        reinterpret_cast<const char*>(raw.data()),
        it - raw.begin()
    );

    std::int64_t expiration_ts = 0;
    auto [ptr, ec] = std::from_chars(
        expiration_sv.data(),
        expiration_sv.data() + expiration_sv.size(),
        expiration_ts
    );
    if (ec != std::errc()) return false;

    if (currentUnixTS() > expiration_ts) return false; // expired

    // Remaining bytes are the HMAC
    const auto hmac_offset = std::distance(raw.begin(), it);
    const std::span<const std::uint8_t> hmac_given(raw.data() + hmac_offset, raw.size() - hmac_offset);

    std::array<unsigned char, EVP_MAX_MD_SIZE> hmac_expected{};
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         secret_key.data(), secret_key.size(),
         reinterpret_cast<const unsigned char*>(expiration_sv.data()),
         expiration_sv.size(),
         hmac_expected.data(), &hmac_len);

    if (hmac_given.size() != hmac_len) return false;

    // Constant-time comparison using OpenSSL
    return CRYPTO_memcmp(hmac_given.data(), hmac_expected.data(), hmac_len) == 0;
}

bool Authenticator::hasPasswordAlgorithmsBit(const std::string &nonce) {
    constexpr std::string_view cookie = "obMatJos2";
    if (!nonce.starts_with(cookie)) return false;

    // Next 4 base64 chars = 3 bytes of security features
    const auto decoded = base64Decode(nonce.substr(cookie.size(), 4));
    if (decoded.size() < 3) return false;

    return (static_cast<uint8_t>(decoded[0]) & 0b10000000) != 0;
}

bool Authenticator::validateMessageIntegrity(const std::span<const std::uint8_t> messageUpToMI,
                                             const std::span<const std::uint8_t> receivedHmac,
                                             const std::span<const std::uint8_t> key,
                                             const bool useSha256) {

    std::size_t computed_len{0};
    std::uint8_t computed_hmac[EVP_MAX_MD_SIZE]{};
    std::array<std::uint8_t, stunxx::STUN_HEADER_SIZE> header{};
    std::memcpy(header.data(), messageUpToMI.data(), stunxx::STUN_HEADER_SIZE);
    const std::size_t mi_value_size = receivedHmac.size();

    const std::uint16_t corrected_len =
            stunxx::host_to_be16(static_cast<std::uint16_t>(
                messageUpToMI.size() - stunxx::STUN_HEADER_SIZE +
                stunxx::ATTR_HEADER_SIZE + mi_value_size));

    // STUN length field is bytes 2–3
    std::memcpy(header.data() + 2, &corrected_len, sizeof(corrected_len));

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) return false;

    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!ctx) return false;

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(
            OSSL_MAC_PARAM_DIGEST,
            const_cast<char*>(useSha256 ? "SHA256" : "SHA1"),
            0),
        OSSL_PARAM_construct_end()
    };

    if (EVP_MAC_init(ctx, key.data(), key.size(), params) != 1) {
        EVP_MAC_CTX_free(ctx);
        return false;
    }

    EVP_MAC_update(ctx, header.data(), header.size());

    EVP_MAC_update(
        ctx,
    messageUpToMI.data() + stunxx::STUN_HEADER_SIZE,
    messageUpToMI.size() - stunxx::STUN_HEADER_SIZE);

    if (EVP_MAC_final(ctx, computed_hmac, &computed_len, sizeof(computed_hmac)) != 1) {
        EVP_MAC_CTX_free(ctx);
        return false;
    }

    EVP_MAC_CTX_free(ctx);

    if (computed_len != receivedHmac.size()) return false;

    return CRYPTO_memcmp(receivedHmac.data(), computed_hmac, computed_len) == 0;
}

bool Authenticator::validatePasswordAlgorithms(const stunxx::PasswordAlgorithmsAttr &algorithms) {
    const auto& entries = algorithms.entries();

    if (entries.size() != supportedAlgorithms.size())
        return false;

    // Must be an exact ordered echo of what we advertised
    return std::ranges::equal(entries, supportedAlgorithms,
        [](const auto& a, const auto& b) {
            return a.algorithm == b.algorithm && a.parameters == b.parameters;
        });
}

std::string Authenticator::base64Encode(const std::uint8_t* data, const std::size_t length) {
    if (length == 0) return {};

    const size_t encoded_len = ((length + 2) / 3) * 4;
    std::string out(encoded_len, '\0');

    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, length);

    return out;
}

std::vector<std::uint8_t> Authenticator::base64Decode(const std::string& b64) {
    if (b64.empty()) return {};

    const size_t estimated_len = (b64.size() * 3) / 4;
    std::vector<std::uint8_t> out(estimated_len);

    int decoded_len = EVP_DecodeBlock(out.data(),
                                      reinterpret_cast<const unsigned char*>(b64.data()),
                                      b64.size());

    if (decoded_len < 0 || static_cast<size_t>(decoded_len) > out.size()) {
        // invalid Base64
        return {};
    }

    // EVP_DecodeBlock doesn't account for padding, adjust size
    size_t padding = 0;
    if (b64.size() >= 2) {
        if (b64[b64.size() - 1] == '=') padding++;
        if (b64[b64.size() - 2] == '=') padding++;
    }

    out.resize(decoded_len - padding);
    return out;
}

std::int64_t Authenticator::currentUnixTS() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}
