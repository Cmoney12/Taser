#include "StunTurnProcessor.hpp"


StunTurnProcessor::StunTurnProcessor(const std::shared_ptr<AllocationManager>& allocation_manager,
                                     std::shared_ptr<const Config> config,
                                     SendFunc sendPacket)
    : authenticator_(config), allocation_manager_(allocation_manager),
sendPacket_(std::move(sendPacket)), config_(std::move(config)) {}

boost::asio::awaitable<void> StunTurnProcessor::processPacket(const Message& message,
    const FiveTuple& five_tuple) const {

    if (stunxx::is_turn_channel_data(message.buffer)) {
        co_await handleChannelData(message, five_tuple);
    }
    else if (stunxx::is_stun_message(message.buffer)) {
        auto decoder_opt = stunxx::Decoder::parse(std::span(message.buffer.data(), message.size));
        if (!decoder_opt) {
            LOG_WARN("Failed to decode STUN message from {}:{} ({} bytes)",
                 five_tuple.endpoint().address().to_string(),
                 five_tuple.endpoint().port(),
                 message.size);
            co_return;
        }

        switch (decoder_opt->messageClass()) {
            case stunxx::StunClass::Request: {
                switch (decoder_opt->messageMethod()) {
                    case stunxx::StunMethod::Binding:
                        co_await handleBindingRequest(decoder_opt.value(), five_tuple);
                        break;
                    case stunxx::StunMethod::Allocate:
                        co_await handleAllocationRequest(decoder_opt.value(), five_tuple);
                        break;
                    case stunxx::StunMethod::Refresh:
                        co_await handleRefreshRequest(decoder_opt.value(), five_tuple);
                        break;
                    case stunxx::StunMethod::CreatePermission:
                        co_await handlePermissionRequest(decoder_opt.value(), five_tuple);
                        break;
                    case stunxx::StunMethod::ChannelBind:
                        co_await handleChannelBindRequest(decoder_opt.value(), five_tuple);
                        break;
                    default:
                        // Request expects a reply — silent drop just stalls the client. RFC 8489 §6.3.1.
                        co_await sendErrorMessage(decoder_opt.value(), stunxx::StunErrorCode::BadRequest,
                            five_tuple.endpoint());
                        break;
                }
                break;
            }
            case stunxx::StunClass::Indication:
                switch (decoder_opt->messageMethod()) {
                    case stunxx::StunMethod::Send:
                        co_await handleSendIndication(decoder_opt.value(), five_tuple);
                        break;
                    default:
                        // No response expected for indications; fine to just drop.
                        LOG_DEBUG("Unhandled indication method {} from {}:{}",
                            static_cast<int>(decoder_opt->messageMethod()),
                            five_tuple.endpoint().address().to_string(),
                            five_tuple.endpoint().port());
                        break;
                }
                break;
            default:
                LOG_WARN("Unexpected STUN class {} (response class?) from {}:{}",
                    static_cast<int>(decoder_opt->messageClass()),
                    five_tuple.endpoint().address().to_string(),
                    five_tuple.endpoint().port());
                break;
        }
    }
    else {
        // RFC 7983 demux for anything that's neither ChannelData nor STUN.
        /**std::uint8_t first_byte = message.buffer[0];
        if (first_byte >= 20 && first_byte <= 63) {
            // DTLS - hand off to DTLS layer
        } else if (first_byte >= 128 && first_byte <= 191) {
            // RTP/RTCP
        } else {**/
            LOG_WARN("Dropped unrecognized packet type {} ({} bytes) from {}:{}",
                message.buffer[0], message.size,
                five_tuple.endpoint().address().to_string(),
                five_tuple.endpoint().port());
    }
}

boost::asio::awaitable<void> StunTurnProcessor::handleBindingRequest(const stunxx::Decoder& decoder,
    const FiveTuple& five_tuple) const {

    Message resp{};
    // create the response
    auto builder = stunxx::StunMessageBuilder(
        stunxx::StunMethod::Binding,
        stunxx::StunClass::SuccessResp,
        decoder.transactionId(),
        resp.buffer);

    // may want to use the other constructor but for simplicity
    auto addr = makeStunAddress(five_tuple.clientAddress(), five_tuple.clientPort());

    const stunxx::Encoder& encoder = builder
    .add<stunxx::XorMappedAddrAttr>(decoder.transactionId(), addr)
    .add<stunxx::SoftwareAttr>("My STUN server").finalize();

    resp.size = encoder.totalSize();
    // set the endpoint to send response

    co_await sendPacket_(resp, five_tuple.endpoint());
}

boost::asio::awaitable<void> StunTurnProcessor::handleAllocationRequest(stunxx::Decoder& decoder,
                                                                        const FiveTuple& five_tuple) const {

    // 1. The server MUST require that the request be authenticated.  This
    //    authentication MUST be done using the long-term credential
    //    mechanism of [https://tools.ietf.org/html/rfc5389#section-10.2.2]
    //    unless the client and server agree to use another mechanism through
    //    some procedure outside the scope of this document.
    // Auth request
    const auto auth = co_await authenticate(decoder, five_tuple.endpoint());
    if (!auth) {
        LOG_WARN("Could not authenticate client at {}:{}", five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    // 2. The server checks if the 5-tuple is currently in use by an
    //    existing allocation.  If yes, the server rejects the request with
    //    a 437 (Allocation Mismatch) error.
    const auto alloc = allocation_manager_->getAllocation(five_tuple);
    if (alloc != nullptr) {
        if (std::ranges::equal(decoder.transactionId(), alloc->getTransactionId())) {
            // retry alloc with success response create a new success responses
            auto success_message = alloc->successMessage();
            co_await sendPacket_(success_message, five_tuple.endpoint());
            co_return;
        }

        // send error
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::AllocationMismatch, five_tuple.endpoint(), auth);
        co_return;
    }

    // 3. Validate REQUESTED-TRANSPORT: 400 if missing/malformed, 442 if unsupported protocol.
    auto requested_transport = decoder.getAttribute<stunxx::RequestedTransAttr>();
    if (!requested_transport) {
        // No REQUESTED-TRANSPORT attribute → Bad Request
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, five_tuple.endpoint(), auth);
        co_return;
    }

    // support tcp and udp
    const auto proto = requested_transport->protocol();
    //if (proto != 17 && proto != 6) {  // 17=UDP, 6=TCP
    // right now we just support UDP
    if (proto != 17) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::UnsupportedTransport, five_tuple.endpoint(), auth);
        co_return;
    }

    // 4. If DONT-FRAGMENT is present but the server can't set the DF bit
    //    on outgoing UDP, treat it as an unknown comprehension-required attribute.
    auto df_attr = decoder.getAttribute<stunxx::DontFragmentAttr>();
    if (df_attr) {
        // Server does not support DF → reject
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::UnknownAttribute, five_tuple.endpoint(), auth);
        co_return;
    }

    // 5. RESERVATION-TOKEN is mutually exclusive with EVEN-PORT/
    //    REQUESTED-ADDRESS-FAMILY/ADDITIONAL-ADDRESS-FAMILY (400 if combined);
    //    otherwise validate the token, 508 if invalid/expired.
    auto reservation_token_attr = decoder.getAttribute<stunxx::ReservationTokenAttr>();
    auto even_port_attr = decoder.getAttribute<stunxx::EvenPortAttr>();
    auto req_fam_attr = decoder.getAttribute<stunxx::RequestedAddressFamilyAttr>();
    auto add_req_fam_attr = decoder.getAttribute<stunxx::AdditionalAddressFamilyAttr>();
    if (reservation_token_attr) {
        if (even_port_attr || req_fam_attr || add_req_fam_attr) {
            // Cannot combine RESERVATION-TOKEN and EVEN-PORT → Bad Request
            co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, five_tuple.endpoint(), auth);
            co_return;
        }

        // Check if the reservation token is valid
        auto token = reservation_token_attr->value();
        long lifetime = computeLifetime(decoder);
        auto allocation = allocation_manager_->claimReservation(token, five_tuple,
            lifetime, sendPacket_);

        if (!allocation) {
            // Token invalid, expired, or relay address unavailable
            co_await sendErrorMessage(decoder,
                stunxx::StunErrorCode::InsufficientCapacity,
                five_tuple.endpoint(),
                auth);
            co_return;
        }

        // Then successful creation
        Message success{};
        auto alloc_addr = makeStunAddress(boost::asio::ip::make_address(allocation->address()),
            allocation->port());

        auto relay_addr = makeStunAddress(five_tuple.clientAddress(), five_tuple.clientPort());
        auto builder = stunxx::StunMessageBuilder(
            stunxx::StunMethod::Allocate,
            stunxx::StunClass::SuccessResp,
            decoder.transactionId(),
            success.buffer).add<stunxx::XorRelayedAddrAttr>(decoder.transactionId(), alloc_addr)
        .add<stunxx::LifetimeAttr>(lifetime)
        .add<stunxx::XorMappedAddrAttr>(decoder.transactionId(), relay_addr);

        if (auth->algorithm() == stunxx::PasswordAlgorithm::SHA256) {
            builder.add<stunxx::MessageIntegritySHA256Attr>(auth->span());
        } else {
            builder.add<stunxx::MessageIntegritySHA1Attr>(auth->span());
        }

        auto encoder = builder.finalize();
        success.size = encoder.totalSize();
        allocation->successMessage(success);
        co_await sendPacket_(success, five_tuple.endpoint());
        co_return;
    }

    // 6. Reject the request with 400 (Bad Request) if both
    // REQUESTED-ADDRESS-FAMILY and ADDITIONAL-ADDRESS-FAMILY are present.
    if (req_fam_attr && add_req_fam_attr) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, five_tuple.endpoint(), auth);
        co_return;
    }

    /*
     * 7. Reject with 440 if the requested family (or IPv4, if
     *    REQUESTED-ADDRESS-FAMILY is absent) isn't supported/enabled;
     *    otherwise allocate IPv4 by default when the attribute is absent.
     */
    stunxx::AddressFamily alloc_family = req_fam_attr ? req_fam_attr->family() : stunxx::AddressFamily::IPv4;
    if (!allocation_manager_->supportsFamily(alloc_family)) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::AddressFamNotSupported,
            five_tuple.endpoint(), auth);
        co_return;
    }

    /*
     * 8. Reject with 400 if EVEN-PORT (R=1) is combined with
     *    ADDITIONAL-ADDRESS-FAMILY; otherwise attempt allocation,
     *    rejecting with 508 if capacity can't be satisfied.
     */
    if (even_port_attr && even_port_attr->reservePair() && add_req_fam_attr) {
        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::BadRequest,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    /*
     * 9. Reject with 400 if ADDITIONAL-ADDRESS-FAMILY == 0x01 (IPv4).
     *    Otherwise, attempt dual-stack allocation: 508 if neither family
     *    can be satisfied; on partial success, include ADDRESS-ERROR-CODE
     *    (440 or 508) in the response; on full success, return both
     *    IPv4 and IPv6 relay addresses as two XOR-RELAYED-ADDRESS attributes.
     */
    if (add_req_fam_attr && add_req_fam_attr->family() == stunxx::AddressFamily::IPv4) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, five_tuple.endpoint(), auth);
        co_return;
    }

    auto port_policy = PortPolicy::Any;
    if (even_port_attr) {
        port_policy = even_port_attr->reservePair()
            ? PortPolicy::EvenAndReserveNext
            : PortPolicy::Even;
    }

    /*
     * 10. May reject with 486 (Allocation Quota Reached) if the client
     *     exceeds a locally defined quota; quota should be keyed on
     *     the authenticated username, not the client's transport address.
     */

    /*
     * 11. May reject with 300 (Try Alternate) to redirect the client to
     *     a different server, per [RFC8489].
     */

    // figure out lifetime
    const auto default_lifetime = config_->turn.allocation_lifetime;
    const auto max_lifetime = config_->turn.max_allocation_lifetime;

    long lifetime = default_lifetime;
    if (auto lifetime_attr = decoder.getAttribute<stunxx::LifetimeAttr>();
        lifetime_attr && lifetime_attr->value() > 0)
    {
        const auto computed = std::min<long>(lifetime_attr->value(), max_lifetime);
        if (computed > default_lifetime)
            lifetime = computed;
    }

    auto [allocation, reservation_token] = allocation_manager_->createAllocation(
        five_tuple,
        alloc_family,
        port_policy,
        lifetime,
        sendPacket_);

    if (!allocation) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::InsufficientCapacity, five_tuple.endpoint(), auth);
        co_return;
    }

    LOG_INFO("Successfully Allocated Client at {}:{}", five_tuple.endpoint().address().to_string(),
        five_tuple.endpoint().port());

    Message success{};
    auto alloc_addr = makeStunAddress(boost::asio::ip::make_address(allocation->address()), allocation->port());
    auto relay_addr = makeStunAddress(five_tuple.clientAddress(), five_tuple.clientPort());

    auto builder = stunxx::StunMessageBuilder(
        stunxx::StunMethod::Allocate,
        stunxx::StunClass::SuccessResp,
        decoder.transactionId(),
        success.buffer)
        .add<stunxx::XorRelayedAddrAttr>(decoder.transactionId(), alloc_addr)
        .add<stunxx::LifetimeAttr>(lifetime)
        .add<stunxx::XorMappedAddrAttr>(decoder.transactionId(), relay_addr);

    if (add_req_fam_attr) {
        // Stub: we never support the additional family, so this is always 508.
        builder.add<stunxx::AddressErrorCodeAttr>(stunxx::AddressFamily::IPv6,
                                                    stunxx::StunErrorCode::InsufficientCapacity);
    }

    if (reservation_token) {
        builder.add<stunxx::ReservationTokenAttr>(*reservation_token);
    }

    if (auth->algorithm() == stunxx::PasswordAlgorithm::SHA256) {
        builder.add<stunxx::MessageIntegritySHA256Attr>(auth->span());
    } else {
        builder.add<stunxx::MessageIntegritySHA1Attr>(auth->span());
    }

    auto encoder = builder.finalize();
    success.size = encoder.totalSize();

    allocation->successMessage(success);
    co_await sendPacket_(success, five_tuple.endpoint());
    co_return;
}

boost::asio::awaitable<void> StunTurnProcessor::handleRefreshRequest(const stunxx::Decoder &decoder,
    const FiveTuple &five_tuple) const {

    LOG_DEBUG("Refresh request received from {}:{}",
        five_tuple.endpoint().address().to_string(),
        five_tuple.endpoint().port());

    const auto auth = co_await authenticate(decoder, five_tuple.endpoint());
    if (!auth) {
        LOG_WARN("Could not authenticate client at {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    auto allocation = allocation_manager_->getAllocation(five_tuple);
    if (!allocation) {
        LOG_WARN("Refresh request from {}:{} with no existing allocation (responding 437 Allocation Mismatch)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::AllocationMismatch,
            five_tuple.endpoint(),
            auth);

        co_return;
    }

    auto req_fam_attr = decoder.getAttribute<stunxx::RequestedAddressFamilyAttr>();
    if (req_fam_attr && allocation->supportsFamily(req_fam_attr->family())) {
        LOG_DEBUG("Refresh request from {}:{} requested unsupported address family (ignoring attribute)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::AddressFamNotSupported,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    const auto lifetime = computeLifetime(decoder);
    allocation->refresh(lifetime);

    Message success{};
    auto builder = stunxx::StunMessageBuilder(
        stunxx::StunMethod::Refresh,
        stunxx::StunClass::SuccessResp,
        decoder.transactionId(),
        success.buffer)
        .add<stunxx::LifetimeAttr>(lifetime);

    if (auth->algorithm() == stunxx::PasswordAlgorithm::SHA256) {
        builder.add<stunxx::MessageIntegritySHA256Attr>(auth->span());
    } else {
        builder.add<stunxx::MessageIntegritySHA1Attr>(auth->span());
    }

    const auto encoder = builder.finalize();
    success.size = encoder.totalSize();
    co_await sendPacket_(success, five_tuple.endpoint());
}

boost::asio::awaitable<void> StunTurnProcessor::handlePermissionRequest(const stunxx::Decoder &decoder,
                                                                        const FiveTuple &five_tuple) const {

    LOG_DEBUG("Create Permission request received from {}:{}",
        five_tuple.endpoint().address().to_string(),
        five_tuple.endpoint().port());

    /*
     * 10.2. Receiving a CreatePermission Request
     * https://datatracker.ietf.org/doc/html/rfc8656#name-receiving-a-createpermissio
     */

    const auto auth = co_await authenticate(decoder, five_tuple.endpoint());
    if (!auth) {
        LOG_WARN("Could not authenticate client at {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    const auto alloc = allocation_manager_->getAllocation(five_tuple);
    if (!alloc) {
        LOG_DEBUG(
            "CreatePermission request from {}:{} with no existing allocation (responding 437 Allocation Mismatch)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::AllocationMismatch,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    const auto xor_addr_attrs = decoder.getAttributes<stunxx::XorPeerAddrAttr>();
    if (xor_addr_attrs.empty()) {
        LOG_DEBUG("CreatePermission request from {}:{} missing XOR-PEER-ADDRESS attribute (400 Bad Request)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(decoder,
            stunxx::StunErrorCode::BadRequest,
            five_tuple.endpoint());
        co_return;
    }

    for (const auto& xor_attr : xor_addr_attrs) {
        // install permission for xor_attr.address
        if (!allocation_manager_->supportsFamily(xor_attr.family())) {
            co_await sendErrorMessage(decoder,
                stunxx::StunErrorCode::BadRequest,
                five_tuple.endpoint());
            co_return;
        }

        auto address = toAsioAddress(xor_attr.address(), xor_attr.family());

        alloc->addPermission(address);

        LOG_DEBUG("Installed permission for peer {} on allocation {}:{}",
            address.to_string(),
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
    }

    Message perm_success{};
    auto builder = stunxx::StunMessageBuilder(
        stunxx::StunMethod::CreatePermission,
        stunxx::StunClass::SuccessResp,
        decoder.transactionId(),
        perm_success.buffer);

    if (auth->algorithm() == stunxx::PasswordAlgorithm::SHA256) {
        builder.add<stunxx::MessageIntegritySHA256Attr>(auth->span());
    } else {
        builder.add<stunxx::MessageIntegritySHA1Attr>(auth->span());
    }

    const auto encoder = builder.finalize();
    perm_success.size = encoder.totalSize();
    co_await sendPacket_(perm_success, five_tuple.endpoint());
}

boost::asio::awaitable<void> StunTurnProcessor::handleChannelBindRequest(const stunxx::Decoder &decoder,
    const FiveTuple &five_tuple) const {

    LOG_DEBUG("Channel bind request received from {}:{}",
        five_tuple.endpoint().address().to_string(),
        five_tuple.endpoint().port());

    const auto auth = co_await authenticate(decoder, five_tuple.endpoint());
    if (!auth) {
        LOG_WARN("Could not authenticate client at {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    const auto allocation = allocation_manager_->getAllocation(five_tuple);
    if (!allocation) {
        LOG_DEBUG(
            "Channel bind request from {}:{} with no existing allocation (responding 437 Allocation Mismatch)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::AllocationMismatch,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    const auto channel_number_attr = decoder.getAttribute<stunxx::ChannelNumberAttr>();
    if (!channel_number_attr) {
        LOG_WARN("Channel number attribute {}:{} not found for Channel Bind",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(decoder,
            stunxx::StunErrorCode::BadRequest,
            five_tuple.endpoint());
        co_return;
    }

    const auto xor_peer_addr_attr = decoder.getAttribute<stunxx::XorPeerAddrAttr>();
    if (!xor_peer_addr_attr) {
        LOG_WARN("XorPeer address attribute {}:{} not found for channel bind",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_await sendErrorMessage(decoder,
            stunxx::StunErrorCode::BadRequest,
            five_tuple.endpoint());
        co_return;
    }

    if (!allocation->supportsFamily(xor_peer_addr_attr->family())) {
        LOG_DEBUG("Channel bind request from {}:{} requested unsupported address family (ignoring attribute)",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(
            decoder,
            stunxx::StunErrorCode::AddressFamNotSupported,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    const Endpoint endpoint{boost::asio::ip::make_address(xor_peer_addr_attr->addressStr()),
        xor_peer_addr_attr->port(),
        stunxx::Protocol::UDP};

    if (!allocation->addChannel(channel_number_attr->channelNumber(), endpoint)) {
        LOG_WARN("Channel bind request failed for {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());

        co_await sendErrorMessage(decoder,
            stunxx::StunErrorCode::BadRequest,
            five_tuple.endpoint(),
            auth);
        co_return;
    }

    Message channel_success{};
    auto builder = stunxx::StunMessageBuilder(
        stunxx::StunMethod::ChannelBind,
        stunxx::StunClass::SuccessResp,
        decoder.transactionId(),
        channel_success.buffer);

    if (auth->algorithm() == stunxx::PasswordAlgorithm::SHA256) {
        builder.add<stunxx::MessageIntegritySHA256Attr>(auth->span());
    } else {
        builder.add<stunxx::MessageIntegritySHA1Attr>(auth->span());
    }

    const auto encoder = builder.finalize();
    channel_success.size = encoder.totalSize();
    co_await sendPacket_(channel_success, five_tuple.endpoint());
}

boost::asio::awaitable<void> StunTurnProcessor::handleChannelData(const Message& message, const FiveTuple& five_tuple) const {
    const auto channel_data = stunxx::ChannelData::decode(message.buffer);
    if (!channel_data) {
        LOG_WARN("Malformed or invalid ChannelData from {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return; // covers: too short, bad channel range, length > buffer
    }

    const auto allocation = allocation_manager_->getAllocation(five_tuple);
    if (!allocation) {
        LOG_WARN("Channel Data received from {}:{} but allocation does not exist",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return; // silent discard, no STUN txn id to reply to
    }

    const auto peer_endpoint = allocation->getPeerByChannel(channel_data->channel());
    if (!peer_endpoint) {
        LOG_WARN("Channel Data received on unbound channel {:#x} from {}:{}",
            channel_data->channel(),
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    Message peer_message{};
    if (!channel_data->encodeData(peer_message.buffer)) {
        LOG_WARN("Channel Data encode failed {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    peer_message.size = channel_data->payloadLength();
    co_await allocation->sendToPeer(peer_message, *peer_endpoint);
}

boost::asio::awaitable<void> StunTurnProcessor::handleSendIndication(const stunxx::Decoder &decoder,
    const FiveTuple &five_tuple) const {

    LOG_DEBUG("Send indication received from {}:{}",
        five_tuple.endpoint().address().to_string(),
        five_tuple.endpoint().port());

    const auto allocation = allocation_manager_->getAllocation(five_tuple);
    if (!allocation) {
        LOG_WARN("Send indication received from {}:{} but allocation does not exist",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return; // silent discard, no STUN txn id to reply to
    }

    const auto data_attr = decoder.getAttribute<stunxx::DataAttr>();
    if (!data_attr) {
        LOG_WARN("Malformed or invalid DataAttr from {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    auto xor_peer_addr = decoder.getAttribute<stunxx::XorPeerAddrAttr>();
    if (!xor_peer_addr) {
        LOG_WARN("Send indication received from {}:{} no xor peer address found",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }

    auto addr = toAsioAddress(xor_peer_addr->address(), xor_peer_addr->family());
    Endpoint peer_endpoint{addr, xor_peer_addr->port(), stunxx::Protocol::UDP};

    Message data_message{};
    if (!data_attr->encode(data_message.buffer)) {
        LOG_WARN("Channel Data encode failed {}:{}",
            five_tuple.endpoint().address().to_string(),
            five_tuple.endpoint().port());
        co_return;
    }
    data_message.size = data_attr->length();

    co_await allocation->sendToPeer(data_message, peer_endpoint);
}

boost::asio::awaitable<std::optional<AuthKey>> StunTurnProcessor::authenticate(const stunxx::Decoder& decoder,
                                                                               const Endpoint& endpoint) const {

    /* ------------------------------------------------------------
     * 1. MESSAGE-INTEGRITY presence & legality
     * ------------------------------------------------------------ */
    const auto mi_sha1   = decoder.getAttribute<stunxx::MessageIntegritySHA1Attr>();
    const auto mi_sha256 = decoder.getAttribute<stunxx::MessageIntegritySHA256Attr>();

    // Must have exactly one MI attribute
    if ((!mi_sha1 && !mi_sha256) || (mi_sha1 && mi_sha256)) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::Unauthenticated, endpoint);
        co_return std::nullopt;
    }

    /* ------------------------------------------------------------
     * 2. Required attributes
     * ------------------------------------------------------------ */
    const auto username_attr = decoder.getAttribute<stunxx::UsernameAttr>();
    const auto realm_attr    = decoder.getAttribute<stunxx::RealmAttr>();
    const auto nonce_attr    = decoder.getAttribute<stunxx::NonceAttr>();

    if (!username_attr || !realm_attr || realm_attr->value() != config_->auth.realm || !nonce_attr) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, endpoint);
        co_return std::nullopt;
    }

    auto chosen_algorithm = stunxx::PasswordAlgorithm::MD5;
    /* ------------------------------------------------------------
     * 3. Nonce cookie password algorithms check (RFC 8489 §9.2.4)
     * ----------------------------------------------------------- */
    const auto pw_algorithm  = decoder.getAttribute<stunxx::PasswordAlgorithmAttr>();
    const auto pw_algorithms = decoder.getAttribute<stunxx::PasswordAlgorithmsAttr>();
    if (Authenticator::hasPasswordAlgorithmsBit(nonce_attr->value())) {
        if (!pw_algorithm && !pw_algorithms) {
            // Process as though PASSWORD-ALGORITHM were MD5 — fall through
            // Neither present → treat as MD5 (RFC 8489 §9.2.4)
            chosen_algorithm = stunxx::PasswordAlgorithm::MD5;
        } else {
            // All three conditions must hold
            // Both attributes must appear together
            if (!pw_algorithm || !pw_algorithms) {
                co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, endpoint);
                co_return std::nullopt;
            }

            // Must match server-advertised algorithms
            if (!Authenticator::validatePasswordAlgorithms(*pw_algorithms)) {
                co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, endpoint);
                co_return std::nullopt;
            }

            // Selected algorithm must appear in list
            if (!pw_algorithms->contains(pw_algorithm->algorithm())) {
                co_await sendErrorMessage(decoder, stunxx::StunErrorCode::BadRequest, endpoint);
                co_return std::nullopt;
            }

            chosen_algorithm = pw_algorithm->algorithm();
        }
    }

    /* ------------------------------------------------------------
     * 4. Username / credential lookup (RFC 8489 §9.2.4)
     * ------------------------------------------------------------ */
    const std::string username = username_attr->value();

    // Look up the long-term password for this username
    const auto password_opt = authenticator_.getPassword(username);
    if (!password_opt) {
        // Username unknown → 401 Unauthenticated
        // Must include REALM, fresh NONCE, PASSWORD-ALGORITHMS
        // MAY include MI using the *previous* key (we skip that — it's optional)
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::Unauthenticated, endpoint);
        co_return std::nullopt;
    }

    /* ------------------------------------------------------------
     * 5. Key derivation (RFC 8489 §14.5 / §14.6)
     * ------------------------------------------------------------ */
    const std::string& password = *password_opt;
    const std::string& realm    = realm_attr->value();

    AuthKey out_key = (chosen_algorithm == stunxx::PasswordAlgorithm::SHA256)
    ? AuthKey::sha256(stunxx::computeSHA256Key(username, realm, password))
    : AuthKey::md5(stunxx::computeMD5Key(username, realm, password));

    const std::span<const std::uint8_t> key_span = out_key.span();

    /* ------------------------------------------------------------
     * 6. MESSAGE-INTEGRITY validation (RFC 8489 §15.4 / §15.5)
     * ------------------------------------------------------------ */
    const bool use_sha256 = (chosen_algorithm == stunxx::PasswordAlgorithm::SHA256);

    const stunxx::StunAttrType mi_type = use_sha256
        ? stunxx::StunAttrType::MessageIntegritySHA256
        : stunxx::StunAttrType::MessageIntegrity;

    const auto mi_header = decoder.getAttributeHeader(mi_type);
    if (!mi_header) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::Unauthenticated, endpoint, out_key);
        co_return std::nullopt;
    }

    const std::span<const std::uint8_t> received_hmac =
        use_sha256 ? mi_sha256->value() : mi_sha1->value();

    const std::span<const std::uint8_t> message_up_to_mi(
        decoder.getBuffer().data(),
        mi_header->offset - stunxx::ATTR_HEADER_SIZE);

    if (!Authenticator::validateMessageIntegrity(message_up_to_mi, received_hmac, key_span, use_sha256)) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::Unauthenticated, endpoint, out_key);
        co_return std::nullopt;
    }

    if (!authenticator_.validateNonce(nonce_attr->value())) {
        co_await sendErrorMessage(decoder, stunxx::StunErrorCode::StaleNonce, endpoint, out_key);
        co_return std::nullopt;
    }

    co_return out_key;
}

long StunTurnProcessor::computeLifetime(const stunxx::Decoder &decoder) const {
    const auto default_lifetime = config_->turn.allocation_lifetime;
    const auto max_lifetime = config_->turn.max_allocation_lifetime;

    long lifetime = default_lifetime;
    if (const auto lifetime_attr = decoder.getAttribute<stunxx::LifetimeAttr>();
        lifetime_attr && lifetime_attr->value() > 0) {

        if (const auto computed = std::min<long>(lifetime_attr->value(), max_lifetime); computed > default_lifetime)
            lifetime = computed;
    }
    return lifetime;
}

boost::asio::ip::address StunTurnProcessor::toAsioAddress(std::span<const std::uint8_t> addr,
    const stunxx::AddressFamily address_family) {

    if (address_family == stunxx::AddressFamily::IPv4) {
        boost::asio::ip::address_v4::bytes_type v4{};
        std::ranges::copy(addr, v4.begin());
        return boost::asio::ip::address_v4(v4);
    }
    boost::asio::ip::address_v6::bytes_type v6{};
    std::ranges::copy(addr, v6.begin());
    return boost::asio::ip::address_v6(v6);
}

boost::asio::awaitable<void> StunTurnProcessor::sendErrorMessage(const stunxx::Decoder& decoder,
                                                                 const stunxx::StunErrorCode ec,
                                                                 const Endpoint& endpoint,
                                                                 const std::optional<AuthKey> auth_key) const {

    Message reply{};
    auto builder = stunxx::StunMessageBuilder(
        decoder.messageMethod(),
        stunxx::StunClass::ErrorResp,
        decoder.transactionId(),
        reply.buffer).add<stunxx::ErrorCodeAttr>(ec);

    if (ec == stunxx::StunErrorCode::Unauthenticated ||
        ec == stunxx::StunErrorCode::StaleNonce) {
        builder
            .add<stunxx::RealmAttr>(config_->auth.realm)
            .add<stunxx::NonceAttr>(authenticator_.generateNonce())
            .add<stunxx::PasswordAlgorithmsAttr>(Authenticator::supportedAlgorithms);
    }

    if (auth_key) {
        if (auth_key->algorithm() == stunxx::PasswordAlgorithm::SHA256)
            builder.add<stunxx::MessageIntegritySHA256Attr>(auth_key->span());
        else
            builder.add<stunxx::MessageIntegritySHA1Attr>(auth_key->span());
    }

    if (!builder.valid())
        co_return;

    const auto& encoder = builder.finalize();
    reply.size = encoder.totalSize();

    co_await sendPacket_(reply, endpoint);
}
