// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <util/check.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#ifndef Q_MOC_RUN
#include <compare>
#include <type_traits>
#endif

class UAmount;
class UAmountLiteral;

/** Amount in satoshis (Can be negative) */
class Amount
{
public:
    using inner_type = int64_t;

    // Require explicit initialization to a specified value.
    Amount() = delete;

    constexpr Amount(UAmount v);
    constexpr Amount(UAmountLiteral v);

    template <typename T>
    constexpr static Amount From(const T v)
    {
        return Amount{v};
    }

    template <typename T>
    static constexpr std::optional<Amount> From(const std::optional<T> v)
    {
        return v ? std::optional<Amount>{From(*v)} : std::nullopt;
    }

    constexpr auto operator<=>(const Amount& other) const noexcept = default;

    constexpr Amount operator-() const noexcept { return Amount{-m_sats}; }

    constexpr Amount operator+(const Amount other) const noexcept
    {
        return Amount{m_sats + other.m_sats};
    }

    constexpr Amount operator-(const Amount other) const noexcept
    {
        return Amount{m_sats - other.m_sats};
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

    template <typename T>
    constexpr Amount operator/(const T other) const noexcept
        // int64_t divided by uint64_t results in uint64_t, so disallow it.
        requires(std::is_integral_v<T> && (sizeof(T) < sizeof(inner_type) || std::is_same_v<T, inner_type>))
    {
        return Amount{m_sats / other};
    }

    template <typename T>
    constexpr Amount operator%(const T other) const noexcept
        // int64_t modulo uint64_t results in uint64_t, so disallow it.
        requires(std::is_integral_v<T> && (sizeof(T) < sizeof(inner_type) || std::is_same_v<T, inner_type>))
    {
        return Amount{m_sats % other};
    }

    template <typename T>
    constexpr Amount operator*(const T other) const noexcept
        // int64_t multiplied by uint64_t results in uint64_t, so disallow it.
        requires(std::is_integral_v<T> && (sizeof(T) < sizeof(inner_type) || std::is_same_v<T, inner_type>))
    {
        return Amount{m_sats * other};
    }

    template <typename T>
    friend constexpr Amount operator*(const T a, const Amount b) noexcept
        // uint64_t multiplied by int64_t results in uint64_t, so disallow it.
        requires(std::is_integral_v<T> && (sizeof(T) < sizeof(inner_type) || std::is_same_v<T, inner_type>))
    {
        return Amount{a * b.Int()};
    }

    constexpr UAmount TruncateToUnsigned() const noexcept;
    constexpr UAmount AssertToUnsigned() const noexcept;
    constexpr std::optional<UAmount> TryToUnsigned() const noexcept;

    constexpr const inner_type& Int() const noexcept { return m_sats; }

private:
    template <typename T>
    constexpr explicit Amount(const T v)
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
        : m_sats(v)
    {
    }

    inner_type m_sats;
};

class UAmount
{
public:
    using inner_type = uint64_t;

    constexpr UAmount(UAmountLiteral v);

    template <typename T>
    constexpr static UAmount From(const T v)
    {
        return UAmount{v};
    }

    template <typename T>
    static constexpr std::optional<UAmount> From(const std::optional<T> v)
    {
        return v ? std::optional<UAmount>{From(*v)} : std::nullopt;
    }

    constexpr auto operator<=>(const UAmount& other) const noexcept = default;

    constexpr Amount operator-() const noexcept
    {
        Assume(UInt() <= std::numeric_limits<Amount::inner_type>::max());
        return Amount::From(-static_cast<Amount::inner_type>(UInt()));
    }

    constexpr UAmount operator+(const UAmount other) const noexcept
    {
        return UAmount{m_sats + other.m_sats};
    }

    constexpr Amount operator-(const UAmount other) const noexcept
    {
        return Amount::From(static_cast<Amount::inner_type>(m_sats) - static_cast<Amount::inner_type>(other.m_sats));
    }

    constexpr Amount operator-(const Amount other) const noexcept
    {
        return Amount::From(static_cast<Amount::inner_type>(m_sats) - other.Int());
    }

    constexpr Amount operator-(UAmountLiteral other) const noexcept;

    constexpr inner_type operator/(const UAmount other) const noexcept
    {
        return m_sats / other.m_sats;
    }

    constexpr inner_type operator%(const UAmount other) const noexcept
    {
        return m_sats % other.m_sats;
    }

    constexpr UAmount& operator+=(const UAmount other) noexcept
    {
        m_sats += other.m_sats;
        return *this;
    }

    constexpr UAmount& operator-=(const UAmount other) noexcept
    {
        Assume(m_sats >= other.m_sats);
        m_sats -= other.m_sats;
        return *this;
    }

    constexpr UAmount& operator-=(UAmountLiteral other) noexcept;

    constexpr UAmount& operator-=(const Amount other) noexcept
    {
        Assume(static_cast<Amount::inner_type>(m_sats) >= other.Int());
        m_sats -= other.Int();
        return *this;
    }

    constexpr UAmount& operator >>=(const int other) noexcept
    {
        m_sats >>= other;
        return *this;
    }

    template <typename T>
    friend constexpr UAmount operator*(const T a, const UAmount b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount{a * b.UInt()};
    }

    template <typename T>
    friend constexpr UAmount operator*(const UAmount a, const T b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount{a.UInt() * b};
    }

    template <typename T>
    friend constexpr UAmount operator/(const UAmount a, const T b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount{a.UInt() / b};
    }

    template <typename T>
    friend constexpr UAmount operator%(const UAmount a, const T b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount{a.UInt() % b};
    }

    Amount TruncateToSigned() const noexcept
    {
        return Amount::From(m_sats <= std::numeric_limits<Amount::inner_type>::max() ?
                            static_cast<Amount::inner_type>(m_sats) :
                            std::numeric_limits<Amount::inner_type>::max());
    }

    constexpr const inner_type& UInt() const noexcept { return m_sats; }

private:
    template <typename T>
    constexpr explicit UAmount(const T v)
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
        : m_sats{v}
    {
    }

    inner_type m_sats;
};

// Allows comparisons with signed counterpart, much like integer literals.
class UAmountLiteral
{
public:
    using inner_type = UAmount::inner_type;

    template <typename T>
    static consteval UAmountLiteral From(const T v)
    {
        return UAmountLiteral{v};
    }

    constexpr auto operator<=>(const UAmountLiteral& other) const noexcept = default;

    constexpr auto operator<=>(const UAmount& other) const noexcept
    {
        return UInt() <=> other.UInt();
    }

    constexpr auto operator<=>(const Amount& other) const noexcept
    {
        if (other.Int() < 0) {
            return std::strong_ordering::greater;
        } else {
            return UInt() <=> static_cast<inner_type>(other.Int());
        }
    }

    constexpr bool operator==(const UAmountLiteral& other) const noexcept = default;

    constexpr bool operator==(const UAmount& other) const noexcept
    {
        return UInt() == other.UInt();
    }

    constexpr bool operator==(const Amount& other) const noexcept
    {
        if (other.Int() < 0) {
            return false;
        } else {
            return UInt() == static_cast<inner_type>(other.Int());
        }
    }

    consteval Amount operator-() const noexcept
    {
        Assume(UInt() < std::numeric_limits<Amount::inner_type>::max());
        return Amount::From(-static_cast<Amount::inner_type>(UInt()));
    }

    consteval UAmountLiteral operator+( UAmountLiteral other) const noexcept;

    consteval UAmountLiteral operator-(UAmountLiteral other) const noexcept;

    friend constexpr UAmount operator+(const UAmountLiteral a, const UAmount b) noexcept
    {
        return UAmount::From(a.m_sats + b.UInt());
    }

    friend constexpr UAmount operator-(const UAmountLiteral a, const UAmount b) noexcept
    {
        assert(a.m_sats >= b.UInt());
        return UAmount::From(a.m_sats - b.UInt());
    }

    template <typename T>
    friend constexpr UAmount operator/(const UAmountLiteral a, const T b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount::From(a.UInt() / b);
    }

    template <typename T>
    friend constexpr Amount operator/(const UAmountLiteral a, const T b) noexcept
        requires(std::is_integral_v<T> && !std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        Assume(a.UInt() < std::numeric_limits<Amount::inner_type>::max());
        return Amount::From(static_cast<Amount::inner_type>(a.UInt()) / b);
    }

    template <typename T>
    friend constexpr UAmount operator*(const T a, const UAmountLiteral b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount::From(a * b.UInt());
    }

    template <typename T>
    friend constexpr Amount operator*(const T a, const UAmountLiteral b) noexcept
        requires(std::is_integral_v<T> && !std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        Assume(b.UInt() < std::numeric_limits<Amount::inner_type>::max());
        return Amount::From(a * static_cast<Amount::inner_type>(b.UInt()));
    }

    template <typename T>
    friend constexpr UAmount operator*(const UAmountLiteral a, const T b) noexcept
        requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        return UAmount::From(a.UInt() * b);
    }

    template <typename T>
    friend constexpr Amount operator*(const UAmountLiteral a, const T b) noexcept
        requires(std::is_integral_v<T> && !std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
    {
        Assume(a.UInt() < std::numeric_limits<Amount::inner_type>::max());
        return Amount::From(static_cast<Amount::inner_type>(a.UInt()) * b);
    }

    friend constexpr UAmount::inner_type operator/(const UAmount a, const UAmountLiteral b) noexcept
    {
        return a.UInt() / b.UInt();
    }

    friend constexpr UAmount::inner_type operator%(const UAmount a, const UAmountLiteral b) noexcept
    {
        return a.UInt() % b.UInt();
    }

    constexpr const inner_type& UInt() const noexcept { return m_sats; }

private:
    template <typename T>
    consteval explicit UAmountLiteral(const T v)
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(inner_type))
        : m_sats{v}
    {
    }

    const inner_type m_sats;
};

constexpr Amount::Amount(UAmount v)
    : m_sats(v.UInt())
{
    Assume(v.UInt() <= static_cast<UAmount::inner_type>(std::numeric_limits<inner_type>::max())); // Accidental wraparound, use TruncateToSigned() if intentional.
}

constexpr Amount::Amount(const UAmountLiteral v)
    : m_sats(v.UInt())
{
    Assume(v.UInt() <= static_cast<UAmount::inner_type>(std::numeric_limits<inner_type>::max())); // Accidental wraparound, use TruncateToSigned() if intentional.
}

constexpr UAmount Amount::TruncateToUnsigned() const noexcept
{
    return UAmount::From(m_sats >= 0 ? UAmount::inner_type(m_sats) : 0);
}

constexpr UAmount Amount::AssertToUnsigned() const noexcept
{
    assert(m_sats >= 0); // Accidental wraparound, use TruncateToUnsigned() if intentional.
    return UAmount::From(static_cast<UAmount::inner_type>(m_sats));
}

constexpr std::optional<UAmount> Amount::TryToUnsigned() const noexcept
{
    if (m_sats < 0) return std::nullopt;
    return UAmount::From(static_cast<UAmount::inner_type>(m_sats));
}

constexpr UAmount::UAmount(const UAmountLiteral v)
    : m_sats{v.UInt()}
{
}

constexpr UAmount& UAmount::operator-=(const UAmountLiteral other) noexcept
{
    Assume(m_sats >= other.UInt());
    m_sats -= other.UInt();
    return *this;
}

constexpr Amount UAmount::operator-(const UAmountLiteral other) const noexcept
{
    return Amount::From(static_cast<Amount::inner_type>(m_sats) - static_cast<Amount::inner_type>(other.UInt()));
}

consteval UAmountLiteral UAmountLiteral::operator+(const UAmountLiteral other) const noexcept
{
    return UAmountLiteral{m_sats + other.m_sats};
}

consteval UAmountLiteral UAmountLiteral::operator-(const UAmountLiteral other) const noexcept
{
    assert(m_sats >= other.m_sats);
    return UAmountLiteral{m_sats - other.m_sats};
}


consteval UAmountLiteral operator""_sats(unsigned long long amount) noexcept
{
    return UAmountLiteral::From(amount);
}

/** The amount of satoshis in one BTC. */
constexpr UAmountLiteral COIN{100000000_sats};

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin
 * currently happens to be less than 21,000,000 BTC for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
constexpr UAmount MAX_MONEY{21000000U * COIN};
constexpr bool MoneyRange(const Amount& nValue) { return (nValue >= 0_sats && nValue <= MAX_MONEY); }
constexpr bool MoneyRange(const UAmount& nValue) { return (nValue >= 0_sats && nValue <= MAX_MONEY); }
constexpr bool MoneyRange(const UAmountLiteral& nValue) { return MoneyRange(UAmount{nValue}); }

// Disable accidental use
struct DisabledLimits {
    static constexpr bool is_specialized{false};
    static constexpr int radix{0};
    static constexpr int digits{0};
    static constexpr int max_digits10{0};
};

template <>
struct std::numeric_limits<Amount> : DisabledLimits {};
template <>
struct std::numeric_limits<UAmount> : DisabledLimits {};
template <>
struct std::numeric_limits<UAmountLiteral> : DisabledLimits {};

inline std::ostream& operator<<(std::ostream& o, const Amount a)
{
    o << a.Int();
    return o;
}

inline std::ostream& operator<<(std::ostream& o, const UAmount a)
{
    o << a.UInt();
    return o;
}

inline std::ostream& operator<<(std::ostream& o, const UAmountLiteral a)
{
    o << a.UInt();
    return o;
}

#endif // BITCOIN_CONSENSUS_AMOUNT_H
