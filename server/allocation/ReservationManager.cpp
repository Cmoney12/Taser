#include "ReservationManager.hpp"


ReservationManager::ReservationManager(long reservation_lifetime) : reservation_lifetime_(reservation_lifetime) {}

std::optional<boost::asio::ip::udp::socket> ReservationManager::claimReservation(const ReservationToken& token) {
    std::lock_guard<std::mutex> lock(udp_mtx_);
    auto it = reservations_.find(token);
    if (it == reservations_.end())
        return std::nullopt;

    auto reservation = it->second;
    reservations_.erase(it);

    reservation->stop();

    return reservation->takeSocket();
}

ReservationToken ReservationManager::createReservation(boost::asio::ip::udp::socket&& socket) {
    std::lock_guard<std::mutex> lock(udp_mtx_);

    ReservationToken token = generateToken();
    while (reservations_.contains(token)) {
        token = generateToken();
    }

    auto weak_self = weak_from_this();

    auto reservation = std::make_shared<Reservation>(
        std::move(socket),
        reservation_lifetime_,
        token,
        [weak_self](const ReservationToken& res_token) {
            if (const auto self = weak_self.lock()) {
                self->expireReservation(res_token);
            }
        });

    auto [it, inserted] = reservations_.emplace(token, std::move(reservation));
    it->second->startTimer();

    return token;
}

void ReservationManager::expireReservation(const ReservationToken& token) {
    std::lock_guard<std::mutex> lock(udp_mtx_);
    reservations_.erase(token);
}

ReservationToken ReservationManager::generateToken() {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<std::uint8_t> dis(0, 255);

    ReservationToken token;
    std::ranges::generate(token, [&]{ return dis(gen); });
    return token;
}
