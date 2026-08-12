#ifndef TASER_MESSAGE_HPP
#define TASER_MESSAGE_HPP

#include <span>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

struct Message {
    Message() = default;

    Message(Message const& other) {
        size = other.size;
        buffer = other.buffer;
    }

    explicit Message(std::span<const std::uint8_t> data) {
        size = std::min(data.size(), buffer.size());
        std::copy_n(data.begin(), size, buffer.begin());
    }

    std::size_t size;
    std::array<std::uint8_t, 2048> buffer;
};

#endif //TASER_MESSAGE_HPP