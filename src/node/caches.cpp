// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/caches.h>

#include <common/args.h>
#include <index/txindex.h>
#include <kernel/caches.h>

#include <algorithm>
#include <string>

// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference: https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static constexpr int64_t MAX_TX_INDEX_CACHE{1024};
//! Max memory allocated to all block filter index caches combined in MiB.
static constexpr int64_t MAX_FILTER_INDEX_CACHE{1024};

namespace node {
std::tuple<IndexCacheSizes, kernel::CacheSizes> CalculateCacheSizes(const ArgsManager& args, size_t n_indexes)
{
    const int64_t dbcache_bytes{args.GetIntArg("-dbcache", DEFAULT_DB_CACHE) << 20};
    Assert(dbcache_bytes >= 0 && static_cast<uint64_t>(dbcache_bytes) <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()));
    size_t nTotalCache{static_cast<size_t>(dbcache_bytes)};
    static_assert((MIN_DB_CACHE << 20) <= std::numeric_limits<size_t>::max());
    nTotalCache = std::max(nTotalCache, static_cast<size_t>(MIN_DB_CACHE << 20));
    IndexCacheSizes sizes;
    static_assert((MAX_TX_INDEX_CACHE << 20) <= std::numeric_limits<size_t>::max());
    sizes.tx_index = {std::min(nTotalCache / 8, args.GetBoolArg("-txindex", DEFAULT_TXINDEX) ? static_cast<size_t>(MAX_TX_INDEX_CACHE << 20) : 0)};
    nTotalCache -= sizes.tx_index;
    sizes.filter_index = 0;
    if (n_indexes > 0) {
        static_assert((MAX_FILTER_INDEX_CACHE << 20) <= std::numeric_limits<size_t>::max());
        size_t max_cache{std::min(nTotalCache / 8, static_cast<size_t>(MAX_FILTER_INDEX_CACHE << 20))};
        sizes.filter_index = max_cache / n_indexes;
        nTotalCache -= sizes.filter_index * n_indexes;
    }
    return {sizes, kernel::CacheSizes{nTotalCache}};
}
} // namespace node
