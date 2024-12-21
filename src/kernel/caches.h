// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_CACHES_H
#define BITCOIN_KERNEL_CACHES_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

//! Suggested default amount of cache reserved for the kernel (MiB)
static constexpr int64_t DEFAULT_KERNEL_CACHE{450};
//! Max memory allocated to block tree DB specific cache (MiB)
static constexpr int64_t MAX_BLOCK_DB_CACHE{2};
//! Max memory allocated to coin DB specific cache (MiB)
static constexpr int64_t MAX_COINS_DB_CACHE{8};

namespace kernel {

constexpr size_t MiBToBytes(int64_t mib)
{
    assert(mib >= 0);
    const int64_t bytes{mib << 20};
    assert(bytes >= 0);
    assert(static_cast<uint64_t>(bytes) <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()));
    return static_cast<size_t>(bytes);
}

struct CacheSizes {
    size_t block_tree_db;
    size_t coins_db;
    size_t coins;

    CacheSizes(size_t total_cache)
    {
        block_tree_db = {std::min(total_cache / 8, MiBToBytes(MAX_BLOCK_DB_CACHE))};
        total_cache -= block_tree_db;
        coins_db = {std::min(total_cache / 2, MiBToBytes(MAX_COINS_DB_CACHE))};
        total_cache -= coins_db;
        coins = {total_cache}; // the rest goes to the coins cache
    }
};
} // namespace kernel

#endif // BITCOIN_KERNEL_CACHES_H
