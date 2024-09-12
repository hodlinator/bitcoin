// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <index/txindex.h>
#include <index/txospenderindex.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <algorithm>
#include <cstdint>

std::unique_ptr<TxoSpenderIndex> g_txospenderindex;

/** Access to the txo spender index database (indexes/txospenderindex/) */
class TxoSpenderIndex::DB : public BaseIndex::DB
{
    // LeveLDB key prefix. We only have one key for now but it will make it easier to add others if needed.
    static constexpr uint8_t DB_TXOSPENDERINDEX{'s'};

    const TxIndex& m_tx_index;

public:
    explicit DB(const TxIndex& tx_index LIFETIMEBOUND, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    struct Key {
        uint64_t inner;
        Key(int32_t block_height, uint16_t tx_index, uint16_t output_index)
            : inner{static_cast<uint64_t>(block_height) << 32 | tx_index << 16 | output_index}
        {
        }

        SERIALIZE_METHODS(Key, k) { READWRITE(VARINT(k.inner)); }
    };

    // Stores block height where a transaction spends a given outpoint described by Key.
    using Value = int32_t;

    bool WriteSpenderInfos(const std::vector<std::pair<Key, Value>>& items);
    bool EraseSpenderInfos(const std::vector<Key>& items);
    std::optional<Value> FindSpender(const Key& key) const;
    std::optional<Key> ComputeKey(const node::BlockManager& blockman, const COutPoint& prevout);
};

TxoSpenderIndex::DB::DB(const TxIndex& tx_index, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex::DB(gArgs.GetDataDirNet() / "indexes" / "txospenderindex", n_cache_size, f_memory, f_wipe)
    , m_tx_index(tx_index)
{
}

TxoSpenderIndex::TxoSpenderIndex(const TxIndex& tx_index LIFETIMEBOUND, std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "txospenderindex")
    , m_db(std::make_unique<TxoSpenderIndex::DB>(tx_index, n_cache_size, f_memory, f_wipe))
{
}

TxoSpenderIndex::~TxoSpenderIndex() = default;

bool TxoSpenderIndex::DB::WriteSpenderInfos(const std::vector<std::pair<Key, Value>>& items)
{
    CDBBatch batch(*this);
    for (const auto& [key, value] : items) {
        batch.Write(std::pair{DB_TXOSPENDERINDEX, key}, value);
    }
    return WriteBatch(batch);
}

bool TxoSpenderIndex::DB::EraseSpenderInfos(const std::vector<Key>& items)
{
    CDBBatch batch(*this);
    for (const auto& key : items) {
        batch.Erase(std::pair{DB_TXOSPENDERINDEX, key});
    }
    return WriteBatch(batch);
}

std::optional<TxoSpenderIndex::DB::Value> TxoSpenderIndex::DB::FindSpender(const Key& key) const
{
    Value value_out;
    if (Read(std::pair{DB_TXOSPENDERINDEX, key}, value_out)) {
        return value_out;
    }
    return std::nullopt;
}

std::optional<TxoSpenderIndex::DB::Key> TxoSpenderIndex::DB::ComputeKey(const node::BlockManager& blockman, const COutPoint& prevout)
{
    uint256 block_hash;
    CTransactionRef dummy_tx;
    // Maybe indexing hasn't completed yet?
    if (!m_tx_index.FindTx(prevout.hash, /*out*/block_hash, dummy_tx)) return std::nullopt;

    LOCK(::cs_main);
    const CBlockIndex* block_index{blockman.LookupBlockIndex(block_hash)};
    // Maybe indexing hasn't completed yet?
    if (!block_index) return std::nullopt;

    assert(!blockman.IsBlockPruned(*block_index)); // We don't support pruning.
    std::vector<uint8_t> block_data{};
    assert(blockman.ReadRawBlockFromDisk(block_data, block_index->GetBlockPos()));
    DataStream block_stream{block_data};
    CBlock input_block{};
    block_stream >> TX_WITH_WITNESS(input_block);

    auto it = std::ranges::find_if(input_block.vtx, [&prevout](CTransactionRef& ref) { return ref->GetHash() == prevout.hash; });
    assert(it != input_block.vtx.end());

    const size_t diff = it - input_block.vtx.begin();
    assert(diff < std::numeric_limits<uint16_t>::max());
    const uint16_t index = static_cast<uint16_t>(diff);
    assert(prevout.n < std::numeric_limits<uint16_t>::max());
    const uint16_t output_index = static_cast<uint16_t>(prevout.n);
    return DB::Key{block_index->nHeight, index, output_index};
}

bool TxoSpenderIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    std::vector<std::pair<DB::Key, DB::Value>> items;
    items.reserve(block.data->vtx.size());

    for (size_t i{0}; i < block.data->vtx.size(); ++i) {
        const auto& tx{block.data->vtx[i]};
        if (tx->IsCoinBase()) {
            continue;
        }
        assert(i < std::numeric_limits<uint16_t>::max());
        const DB::Value value{block.height};
        for (const auto& input : tx->vin) {
            auto key{m_db->ComputeKey(m_chainstate->m_blockman, input.prevout)};
            assert(key);
            items.emplace_back(*key, value);
        }
    }
    return m_db->WriteSpenderInfos(items);
}

bool TxoSpenderIndex::CustomRewind(const interfaces::BlockKey& current_tip, const interfaces::BlockKey& new_tip)
{
    LOCK(cs_main);
    const CBlockIndex* iter_tip{m_chainstate->m_blockman.LookupBlockIndex(current_tip.hash)};
    const CBlockIndex* new_tip_index{m_chainstate->m_blockman.LookupBlockIndex(new_tip.hash)};

    do {
        CBlock block;
        if (!m_chainstate->m_blockman.ReadBlockFromDisk(block, *iter_tip)) {
            LogError("Failed to read block %s from disk\n", iter_tip->GetBlockHash().ToString());
            return false;
        }
        std::vector<DB::Key> items;
        items.reserve(block.vtx.size());
        for (const auto& tx : block.vtx) {
            if (tx->IsCoinBase()) {
                continue;
            }
            for (const auto& input : tx->vin) {
                auto key{m_db->ComputeKey(m_chainstate->m_blockman, input.prevout)};
                assert(key);
                items.emplace_back(*key);
            }
        }
        if (!m_db->EraseSpenderInfos(items)) {
            LogError("Failed to erase indexed data for disconnected block %s from disk\n", iter_tip->GetBlockHash().ToString());
            return false;
        }

        iter_tip = iter_tip->GetAncestor(iter_tip->nHeight - 1);
    } while (new_tip_index != iter_tip);

    return true;
}

std::optional<Txid> TxoSpenderIndex::FindSpender(const COutPoint& txo) const
{
    node::BlockManager& blockman = m_chainstate->m_blockman;
    LOCK(::cs_main);
    auto key{m_db->ComputeKey(m_chainstate->m_blockman, txo)};
    // Maybe indexing hasn't completed yet?
    if (!key) return std::nullopt;

    auto block_height = m_db->FindSpender(*key);
    // Maybe indexing hasn't completed yet?
    if (!block_height) return std::nullopt;

    // WARNING! WARNING! WARNING! WARNING! WARNING!
    // No idea if this is permissible in production. Passes tests.
    std::vector<CBlockIndex*> sorted_by_height{blockman.GetAllBlockIndices()};
    std::sort(sorted_by_height.begin(), sorted_by_height.end(), node::CBlockIndexHeightOnlyComparator());
    // Ensure no gaps and starting from genesis.
    for (size_t i{0}; i < sorted_by_height.size(); ++i) {
        assert(sorted_by_height[i]->nHeight == static_cast<int>(i));
    }
    // Maybe IBD hasn't completed yet?
    if (static_cast<size_t>(*block_height) >= sorted_by_height.size()) return std::nullopt;

    const CBlockIndex* block_index{sorted_by_height[*block_height]};
    assert(!blockman.IsBlockPruned(*block_index)); // We don't support pruning.
    std::vector<uint8_t> block_data{};
    // Not sure how this could happen.
    assert(blockman.ReadRawBlockFromDisk(block_data, block_index->GetBlockPos()));

    DataStream block_stream{block_data};
    CBlock block{};
    block_stream >> TX_WITH_WITNESS(block);

    for (const auto& tx : block.vtx) {
        for (const auto& input : tx->vin) {
            if (input.prevout == txo) {
                return tx->GetHash();
            }
        }
    }
    // Really should have found a tx.
    return std::nullopt;
}

BaseIndex::DB& TxoSpenderIndex::GetDB() const { return *m_db; }
