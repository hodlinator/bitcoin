// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_AMOUNT_H
#define BITCOIN_TEST_UTIL_AMOUNT_H

#include <consensus/amount.h>

template <typename T>
constexpr Amount& operator*=(Amount& a, const T b) noexcept
    // int64_t multiplied by uint64_t results in uint64_t, so disallow it.
    requires(std::is_integral_v<T> && (sizeof(T) < sizeof(Amount::inner_type) || std::is_same_v<T, Amount::inner_type>))
{
    a = Amount{a.Int() * b};
    return a;
}

template <typename T>
constexpr UAmount& operator*=(UAmount& a, const T b) noexcept
    requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(UAmount::inner_type))
{
    a = UAmount{a.UInt() * b};
    return a;
}

template <typename T>
constexpr UAmount operator<<(const UAmount a, const T b) noexcept
    requires(std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(UAmount::inner_type))
{
    return UAmount{a.UInt() << b};
}

/** Imprecise fractional expressions only allowed for tests.
  * @{*/
consteval UAmountLiteral operator""_BTC(long double coins) noexcept
{
    assert(coins >= 0.0); // Since we return unsigned we don't support negative doubles
    return UAmountLiteral{static_cast<UAmountLiteral::inner_type>(coins * COIN.UInt())};
}
/** @}*/

#endif // BITCOIN_TEST_UTIL_AMOUNT_H
