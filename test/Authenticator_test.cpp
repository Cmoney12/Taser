#include <gtest/gtest.h>
#include "../server/config/Config.hpp"
#include "../server/auth/Authenticator.hpp"

class AuthenticatorTest : public ::testing::Test {
protected:
    std::shared_ptr<Config> config = std::make_shared<Config>();
    Authenticator auth{config};
};

TEST_F(AuthenticatorTest, Base64Encode_Empty) {
    std::string result = auth.base64Encode(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

TEST_F(AuthenticatorTest, RoundTripBinaryData) {
    std::vector<std::uint8_t> input = {
        0x00, 0x01, 0x02, 0x7F, 0x80, 0xFF, 'a', 'b', 'c'
    };

    std::string encoded = auth.base64Encode(input.data(), input.size());
    auto decoded = auth.base64Decode(encoded);

    ASSERT_EQ(decoded.size(), input.size());
    EXPECT_EQ(decoded, input);
}

TEST_F(AuthenticatorTest, KnownVectors) {
    struct TestCase {
        std::string input;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"}
    };

    for (const auto& tc : cases) {
        auto encoded = auth.base64Encode(
            reinterpret_cast<const std::uint8_t*>(tc.input.data()),
            tc.input.size()
        );

        EXPECT_EQ(encoded, tc.expected);

        auto decoded = auth.base64Decode(encoded);
        std::string decoded_str(decoded.begin(), decoded.end());
        EXPECT_EQ(decoded_str, tc.input);
    }
}

TEST_F(AuthenticatorTest, GenerateValidateRoundTrip) {

    std::string nonce = auth.generateNonce();
    EXPECT_TRUE(auth.validateNonce(nonce));
}

TEST_F(AuthenticatorTest, ExpiredNonceFails) {

    // Generate a nonce that expired 10 seconds ago
    std::string nonce = auth.generateNonce(auth.currentUnixTS() - 10);
    EXPECT_FALSE(auth.validateNonce(nonce));
}

TEST_F(AuthenticatorTest, TamperedNonceFails) {

    std::string nonce = auth.generateNonce();

    // Tamper with a single byte
    nonce[5] ^= 0xFF;
    EXPECT_FALSE(auth.validateNonce(nonce));
}

TEST_F(AuthenticatorTest, ValidateMessageIntegrity_SuccessAndTamper) {

    std::vector<std::uint8_t> message = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05
    };

    std::array<std::uint8_t, 16> key = {
        0x10, 0x11, 0x12, 0x13,
        0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B,
        0x1C, 0x1D, 0x1E, 0x1F
    };

    // Compute HMAC using the same logic as validateMessageIntegrity
    unsigned int hmac_len = 0;
    std::vector<unsigned char> hmac(EVP_MAX_MD_SIZE);

    HMAC(EVP_sha1(), key.data(), key.size(), message.data(), message.size(),
         hmac.data(), &hmac_len);

    std::span<const std::uint8_t> message_span(message.data(), message.size());
    std::span<const std::uint8_t> hmac_span(hmac.data(), hmac_len);

    // Validation should succeed
    EXPECT_TRUE(auth.validateMessageIntegrity(message_span, hmac_span, key, false));

    // Tamper with a byte in the HMAC
    std::vector<unsigned char> tampered_hmac(hmac_span.begin(), hmac_span.end());
    tampered_hmac[0] ^= 0xFF;
    std::span<const std::uint8_t> tampered_span(tampered_hmac.data(), tampered_hmac.size());

    // Validation should fail
    EXPECT_FALSE(auth.validateMessageIntegrity(message_span, tampered_span, key, false));
}
