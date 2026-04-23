// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_AMOUNT_H
#define BITCOIN_TEST_UTIL_AMOUNT_H

#include <consensus/amount.h>

/** Potentially imprecise calculations only allowed for tests.
  * @{*/
consteval Amount operator""_BTC(long double coins) noexcept
{
    return static_cast<Amount>(coins * COIN);
}
/** @}*/

#endif // BITCOIN_TEST_UTIL_AMOUNT_H
