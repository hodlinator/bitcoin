// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_AMOUNT_H
#define BITCOIN_TEST_UTIL_AMOUNT_H

#include <consensus/amount.h>

template <typename T>
constexpr Amount& operator*=(Amount& a, const T b) noexcept
    requires(std::is_integral_v<T> && sizeof(T) <= sizeof(Amount::inner_type))
{
    a = Amount{a.Int() * b};
    return a;
}

template <typename T>
constexpr Amount operator<<(const Amount a, const T b) noexcept
    requires(std::is_integral_v<T> && sizeof(T) <= sizeof(Amount::inner_type))
{
    return Amount{a.Int() << b};
}

/** Imprecise fractional expressions only allowed for tests.
  * @{*/
consteval Amount operator""_BTC(long double coins) noexcept
{
    return Amount{static_cast<Amount::inner_type>(coins * COIN.Int())};
}
/** @}*/

#endif // BITCOIN_TEST_UTIL_AMOUNT_H
