// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>
#include <limits>
#include <ostream>
#ifndef Q_MOC_RUN
#include <compare>
#include <type_traits>
#endif

/** Amount in satoshis (Can be negative) */
class Amount
{
public:
    using inner_type = int64_t;

    // Require explicit initialization to a specified value.
    Amount() = delete;

    template <typename T>
    constexpr Amount(T v)
        requires(std::is_integral_v<T> && sizeof(T) <= sizeof(inner_type))
        : m_sats(v)
    {
    }

    constexpr auto operator<=>(const Amount& other) const noexcept = default;

    constexpr Amount operator-() const noexcept { return {-m_sats}; }

    constexpr Amount operator+(const Amount other) const noexcept
    {
        return {m_sats + other.m_sats};
    }

    constexpr Amount operator-(const Amount other) const noexcept
    {
        return {m_sats - other.m_sats};
    }

    constexpr inner_type operator/(const Amount other) const noexcept
    {
        return m_sats / other.m_sats;
    }

    constexpr inner_type operator%(const Amount other) const noexcept
    {
        return m_sats % other.m_sats;
    }

    constexpr Amount& operator+=(const Amount other) noexcept
    {
        m_sats += other.m_sats;
        return *this;
    }

    constexpr Amount& operator-=(const Amount other) noexcept
    {
        m_sats -= other.m_sats;
        return *this;
    }

    constexpr Amount& operator >>=(const int other) noexcept
    {
        m_sats >>= other;
        return *this;
    }

    template <typename T>
    constexpr Amount operator%(const T other) const noexcept
        requires(std::is_integral_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return {m_sats % other};
    }

    template <typename T>
    constexpr Amount operator*(const T other) const noexcept
        requires(std::is_integral_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return {m_sats * other};
    }

    template <typename T>
    friend constexpr Amount operator*(const T a, const Amount b) noexcept
        requires(std::is_integral_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return {a * b.Int()};
    }

    template <typename T>
    friend constexpr Amount operator/(const Amount a, const T b) noexcept
        requires(std::is_integral_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return {a.Int() / b};
    }

    constexpr const inner_type& Int() const noexcept { return m_sats; }

private:
    inner_type m_sats;
};

consteval Amount operator""_sats(unsigned long long amount) noexcept
{
    return Amount{amount};
}

/** The amount of satoshis in one BTC. */
constexpr Amount COIN{100000000};

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin
 * currently happens to be less than 21,000,000 BTC for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
constexpr Amount MAX_MONEY{21000000 * COIN};
inline bool MoneyRange(const Amount& nValue) { return (nValue >= 0_sats && nValue <= MAX_MONEY); }

// Disable accidental use
template <>
struct std::numeric_limits<Amount> {
    static constexpr bool is_specialized{false};
    static constexpr int radix{0};
    static constexpr int digits{0};
    static constexpr int max_digits10{0};
};

inline std::ostream& operator<<(std::ostream& o, const Amount a)
{
    o << a.Int();
    return o;
}

#endif // BITCOIN_CONSENSUS_AMOUNT_H
