#ifndef TASER_RESERVATIONMANAGER_HPP
#define TASER_RESERVATIONMANAGER_HPP

#include <array>
#include <cstdint>
#include <random>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

using boost::asio::ip::udp;
using ReservationToken = std::array<std::uint8_t, 8>;

namespace std {
template<>
struct hash<ReservationToken> {
    std::size_t operator()(const ReservationToken& token) const noexcept {
        // maybe overkill but I want 32 bit support
        std::uint64_t v;
        std::memcpy(&v, token.data(), sizeof(v));
        v ^= v >> 30;
        v *= 0xbf58476d1ce4e5b9ULL;
        v ^= v >> 27;
        v *= 0x94d049bb133111ebULL;
        v ^= v >> 31;
        return static_cast<std::size_t>(v);
    }
};
}

class Reservation : public std::enable_shared_from_this<Reservation> {
public:
    Reservation(udp::socket&& socket,
        long lifetime_seconds,
        ReservationToken reservation_token,
        std::function<void(const ReservationToken&)> expiry_callback)
    : lifetime_seconds_(lifetime_seconds), socket_(std::move(socket)), timer_(socket_.get_executor()),
    reservation_token_(reservation_token), expiry_callback_(std::move(expiry_callback)) {}

    void startTimer() {
        timer_.expires_after(std::chrono::seconds(lifetime_seconds_));
        expiry_time = std::chrono::steady_clock::now() + std::chrono::seconds(lifetime_seconds_);
        timer_.async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
            if (!ec && self->expiry_callback_) {
                self->expiry_callback_(self->reservation_token_);
            }
        });
    }

    boost::asio::ip::udp::socket takeSocket() {
        return std::move(socket_);
    }

    void stop() {
        timer_.cancel(); // cancel pending async_wait
    }

    bool expired() const {
        return std::chrono::steady_clock::now() >= expiry_time;
    }


private:
    long lifetime_seconds_;
    udp::socket socket_;
    boost::asio::steady_timer timer_;
    std::chrono::steady_clock::time_point expiry_time;
    ReservationToken reservation_token_;
    std::function<void(const ReservationToken&)> expiry_callback_;
};

class ReservationManager : public std::enable_shared_from_this<ReservationManager> {
public:
    explicit ReservationManager(long reservation_lifetime);

    std::optional<boost::asio::ip::udp::socket> claimReservation(const ReservationToken& token);

    ReservationToken createReservation(boost::asio::ip::udp::socket&& socket);

    void expireReservation(const ReservationToken& token);

    static ReservationToken generateToken();

private:
    std::mutex udp_mtx_;
    long reservation_lifetime_;
    std::unordered_map<ReservationToken, std::shared_ptr<Reservation>> reservations_{};
};


#endif //TASER_RESERVATIONMANAGER_HPP