// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cassert>
#ifndef Q_MOC_RUN
#include <compare>
#endif
#include <cstdint>
#include <limits>
#include <ostream>
#include <optional>

template <typename T>
concept non_bool_integral =
    std::integral<std::remove_cvref_t<T>> &&
    !std::same_as<std::remove_cvref_t<T>, bool>;

class CAmountUnchecked;

constexpr bool MoneyRange(const int64_t& sats) { return (sats >= 0 && sats <= 21'000'000'0000'0000); }

/** Amount in satoshis (Can be negative) */
class CAmount
{
    int64_t m_sats;

public:
    using inner_type = decltype(m_sats);

    // Require explicit initialization to a stated monetary value.
    CAmount() = delete;

    template <non_bool_integral T>
    constexpr explicit CAmount(T sats) noexcept
        : m_sats(sats)
    {
        assert(MoneyRange(m_sats));
    }

    constexpr auto operator<=>(const CAmount& other) const noexcept = default;

    constexpr CAmountUnchecked operator-() const noexcept;

    constexpr CAmount operator+(const CAmount other) const noexcept
    {
        return CAmount{m_sats + other.m_sats};
    }

    constexpr CAmountUnchecked operator-(CAmount other) const noexcept;

    constexpr inner_type operator/(const CAmount other) const noexcept
    {
        return m_sats / other.m_sats;
    }

    constexpr inner_type operator%(const CAmount other) const noexcept
    {
        return m_sats % other.m_sats;
    }

    constexpr CAmount& operator+=(const CAmount other) noexcept
    {
        *this = CAmount{m_sats + other.m_sats};
        return *this;
    }

    constexpr CAmount& operator-=(const CAmount other) noexcept
    {
        *this = CAmount{m_sats - other.m_sats};
        return *this;
    }

    template <non_bool_integral T>
    constexpr CAmount operator*(const T other) const noexcept
    {
        return CAmount{m_sats * other};
    }

    template <non_bool_integral T>
    friend constexpr CAmount operator*(const T a, const CAmount b) noexcept
    {
        return CAmount{a * b.Int()};
    }

    template <non_bool_integral T>
    constexpr CAmount operator/(const T other) const noexcept
    {
        return CAmount{m_sats / other};
    }

    template <non_bool_integral T>
    constexpr CAmount operator%(const T other) const noexcept
    {
        return CAmount{m_sats % other};
    }

    constexpr CAmount& operator>>=(const int other) noexcept
    {
        *this = CAmount{m_sats >> other};
        return *this;
    }

    constexpr inner_type Int() const noexcept { return m_sats; }
};

/** Allows for negative satoshi amounts or exceeding 21M BTC */
class CAmountUnchecked
{
    int64_t m_sats;

public:
    using inner_type = decltype(m_sats);

    constexpr CAmountUnchecked(const CAmountUnchecked&) noexcept = default;

    template <non_bool_integral T>
    constexpr CAmountUnchecked(T sats) noexcept
        : m_sats(sats)
    {
    }

    constexpr CAmountUnchecked(CAmount checked) noexcept
        : m_sats{checked.Int()}
    {
    }

    constexpr CAmountUnchecked& operator=(const CAmountUnchecked&) noexcept = default;

    constexpr auto operator<=>(const CAmountUnchecked& other) const noexcept = default;

    constexpr CAmountUnchecked operator-() const noexcept { return CAmountUnchecked{-m_sats}; }

    constexpr CAmountUnchecked operator+(const CAmountUnchecked other) const noexcept
    {
        return CAmountUnchecked{m_sats + other.m_sats};
    }

    constexpr CAmountUnchecked operator-(const CAmountUnchecked other) const noexcept
    {
        return CAmountUnchecked{m_sats - other.m_sats};
    }

    constexpr inner_type operator/(const CAmountUnchecked other) const noexcept
    {
        return m_sats / other.m_sats;
    }

    constexpr inner_type operator%(const CAmountUnchecked other) const noexcept
    {
        return m_sats % other.m_sats;
    }

    constexpr CAmountUnchecked& operator+=(const CAmountUnchecked other) noexcept
    {
        *this = CAmountUnchecked{m_sats + other.m_sats};
        return *this;
    }

    constexpr CAmountUnchecked& operator-=(const CAmountUnchecked other) noexcept
    {
        *this = CAmountUnchecked{m_sats - other.m_sats};
        return *this;
    }

    template <non_bool_integral T>
    constexpr CAmountUnchecked operator*(const T other) const noexcept
    {
        return CAmountUnchecked{m_sats * other};
    }

    template <non_bool_integral T>
    friend constexpr CAmountUnchecked operator*(const T a, const CAmountUnchecked b) noexcept
    {
        return CAmountUnchecked{a * b.Int()};
    }

    template <non_bool_integral T>
    constexpr CAmountUnchecked operator/(const T other) const noexcept
    {
        return CAmountUnchecked{m_sats / other};
    }

    template <non_bool_integral T>
    constexpr CAmountUnchecked operator%(const T other) const noexcept
    {
        return CAmountUnchecked{m_sats % other};
    }

    constexpr const inner_type& Int() const noexcept { return m_sats; }

    constexpr CAmount AssertValid() const noexcept { return CAmount{m_sats}; }
    constexpr std::optional<CAmount> TryValid() const noexcept {
        if (MoneyRange(m_sats)) return CAmount{m_sats};
        return std::nullopt;
    }
};

constexpr CAmountUnchecked CAmount::operator-() const noexcept
{
    return CAmountUnchecked{-m_sats};
}

constexpr CAmountUnchecked CAmount::operator-(const CAmount other) const noexcept
{
    return CAmountUnchecked{m_sats - other.m_sats};
}

/** The amount of satoshis in one BTC. */
constexpr CAmount COIN{100000000};

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin
 * currently happens to be less than 21,000,000 BTC for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static constexpr CAmount MAX_MONEY = 21000000 * COIN;
constexpr bool MoneyRange(const CAmountUnchecked& value) { return MoneyRange(value.Int()); }

consteval CAmount operator""_sats(unsigned long long amount) noexcept
{
    assert(amount <= MAX_MONEY.Int());
    return CAmount{static_cast<CAmount::inner_type>(amount)};
}

inline std::ostream& operator<<(std::ostream& o, const CAmount a)
{
    o << a.Int();
    return o;
}

inline std::ostream& operator<<(std::ostream& o, const CAmountUnchecked a)
{
    o << a.Int();
    return o;
}

// Prevent accidental use
struct DisabledLimits {
    static constexpr bool is_specialized{false};
    static constexpr int radix{0};
    static constexpr int digits{0};
    static constexpr int max_digits10{0};
};

template <>
struct std::numeric_limits<CAmount> : DisabledLimits {
};
template <>
struct std::numeric_limits<CAmountUnchecked> : DisabledLimits {
};

#endif // BITCOIN_CONSENSUS_AMOUNT_H
