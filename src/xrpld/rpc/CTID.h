#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace xrpl::rpc {

// A CTID (Concise Transaction ID) identifies a transaction by where it is,
// rather than by its hash: the ledger that holds it, its index within that
// ledger, and the network the ledger belongs to.
//
//   https://github.com/XRPLF/XRPL-Standards/discussions/34
//

/**
 * The parts of a CTID.
 *
 * A CTID is a 64-bit value: a 4-bit marker, then the ledger sequence in the
 * rest of the upper 32 bits, then the transaction index and the network ID
 * in 16 bits each.
 */
struct CTID
{
    std::uint32_t ledgerSeq;
    std::uint16_t txnIndex;
    std::uint16_t networkID;

    [[nodiscard]] friend constexpr bool
    operator==(CTID, CTID) noexcept = default;
};

namespace ctid {

/** The bits of a CTID that hold the marker. */
inline constexpr std::uint64_t kMarkerMask = 0xF000'0000'0000'0000;

/** The value of those bits in every CTID. */
inline constexpr std::uint64_t kMarker = 0xC000'0000'0000'0000;

/** The largest ledger sequence that fits beside the marker. */
inline constexpr std::uint32_t kMaxLedgerSeq = ~kMarkerMask >> 32;

/**
 * Encodes a CTID.
 *
 * @param ledgerSeq The ledger sequence; at most kMaxLedgerSeq.
 * @param txnIndex The index of the transaction in the ledger; at most 0xFFFF.
 * @param networkID The network ID; at most 0xFFFF.
 * @returns The CTID as 16 uppercase hexadecimal digits, or std::nullopt if
 *          a value does not fit in the bits the format has for it.
 */
[[nodiscard]] inline std::optional<std::string>
encodeCTID(std::uint32_t ledgerSeq, std::uint32_t txnIndex, std::uint32_t networkID)
{
    if (ledgerSeq > kMaxLedgerSeq || !std::in_range<std::uint16_t>(txnIndex) ||
        !std::in_range<std::uint16_t>(networkID))
        return std::nullopt;

    auto const value = kMarker | (std::uint64_t{ledgerSeq} << 32) |
        (std::uint64_t{txnIndex} << 16) | std::uint64_t{networkID};

    // The marker occupies the top four bits and is never zero, so the value
    // always has 16 hex digits by construction and needs no padding.
    return std::format("{:X}", value);
}

/**
 * Encodes a CTID from its parts.
 *
 * @param ctid The ledger sequence, transaction index and network ID.
 * @returns The CTID as 16 uppercase hexadecimal digits, or std::nullopt if
 *          the ledger sequence is larger than kMaxLedgerSeq.
 */
[[nodiscard]] inline std::optional<std::string>
encodeCTID(CTID ctid)
{
    return encodeCTID(ctid.ledgerSeq, ctid.txnIndex, ctid.networkID);
}

/**
 * Decodes a CTID given as a number.
 *
 * @param ctid The CTID.
 * @returns The parts of the CTID, or std::nullopt if the value does not
 *          carry the marker.
 */
[[nodiscard]] constexpr std::optional<CTID>
decodeCTID(std::uint64_t ctid) noexcept
{
    if ((ctid & kMarkerMask) != kMarker)
        return std::nullopt;

    return CTID{
        .ledgerSeq = static_cast<std::uint32_t>((ctid & ~kMarkerMask) >> 32),
        .txnIndex = static_cast<std::uint16_t>(ctid >> 16),
        .networkID = static_cast<std::uint16_t>(ctid)};
}

/**
 * Decodes a CTID given as text.
 *
 * @param ctid The CTID: exactly 16 hexadecimal digits, in either case.
 * @returns The parts of the CTID, or std::nullopt if the text is not one.
 */
[[nodiscard]] inline std::optional<CTID>
decodeCTID(std::string_view ctid) noexcept
{
    if (ctid.size() != 16)
        return std::nullopt;

    std::uint64_t value = 0;

    auto const* const last = ctid.data() + ctid.size();
    auto const [ptr, ec] = std::from_chars(ctid.data(), last, value, 16);

    // Reject text that is not made up of hexadecimal digits only.
    if (ec != std::errc{} || ptr != last)
        return std::nullopt;

    return decodeCTID(value);
}

}  // namespace ctid

// Import the functions into the xrpl::rpc namespace
using ctid::decodeCTID;
using ctid::encodeCTID;

}  // namespace xrpl::rpc
