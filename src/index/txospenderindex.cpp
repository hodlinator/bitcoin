// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <index/disktxpos.h>
#include <index/txindex.h>
#include <index/txospenderindex.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <primitives/transaction.h>
#include <uint256.h>
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

    bool WriteSpenderInfos(const std::vector<std::pair<Key, CDiskTxPos>>& items);
    bool EraseSpenderInfos(const std::vector<Key>& items);
    std::optional<CDiskTxPos> FindSpender(const Key& key) const;
    std::optional<Key> ComputeKey(const node::BlockManager& blockman, const COutPoint& prevout) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

TxoSpenderIndex::DB::DB(const TxIndex& tx_index LIFETIMEBOUND, size_t n_cache_size, bool f_memory, bool f_wipe)
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

bool TxoSpenderIndex::DB::WriteSpenderInfos(const std::vector<std::pair<Key, CDiskTxPos>>& items)
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

std::optional<CDiskTxPos> TxoSpenderIndex::DB::FindSpender(const Key& key) const
{
    CDiskTxPos value_out;
    if (Read(std::pair{DB_TXOSPENDERINDEX, key}, value_out)) {
        return value_out;
    }
    return std::nullopt;
}

std::optional<TxoSpenderIndex::DB::Key> TxoSpenderIndex::DB::ComputeKey(const node::BlockManager& blockman, const COutPoint& prevout)
{
    uint256 block_hash;
    CTransactionRef dummy_tx;
    if (!m_tx_index.FindTx(prevout.hash, /*out*/block_hash, dummy_tx)) {
        LogDebug(BCLog::BLOCKSTORAGE, "Failed finding tx %s in TxIndex.\n", prevout.hash.ToString());
        return std::nullopt;
    }

    const CBlockIndex* block_index{blockman.LookupBlockIndex(block_hash)};
    if (!block_index) return std::nullopt;

    // Can it be found but still pruned?
    assert(!blockman.IsBlockPruned(*block_index));

    std::vector<uint8_t> out_block_data{};
    assert(blockman.ReadRawBlockFromDisk(out_block_data, block_index->GetBlockPos()));

    DataStream block_stream{out_block_data};
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
    std::vector<std::pair<DB::Key, CDiskTxPos>> items;
    items.reserve(block.data->vtx.size());

    CDiskTxPos value({block.file_number, block.data_pos}, GetSizeOfCompactSize(block.data->vtx.size()));
    for (size_t i{0}; i < block.data->vtx.size(); value.nTxOffset += ::GetSerializeSize(TX_WITH_WITNESS(*block.data->vtx[i])), ++i) {
        const auto& tx{block.data->vtx[i]};
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const auto& input : tx->vin) {
            LOCK(::cs_main);
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
        for (size_t i{0}; i < block.vtx.size(); ++i) {
            const auto& tx{block.vtx[i]};
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
    CTransactionRef tx;

    auto key{m_db->ComputeKey(m_chainstate->m_blockman, txo)};
    if (!key) return std::nullopt;

    auto value = m_db->FindSpender(*key);
    if (!value) return std::nullopt;

    AutoFile file{m_chainstate->m_blockman.OpenBlockFile(*value, true)};
    if (file.IsNull()) {
        LogError("OpenBlockFile failed\n");
        return std::nullopt;
    }
    CBlockHeader header;
    try {
        file >> header;
        if (fseek(file.Get(), value->nTxOffset, SEEK_CUR)) {
            LogError("fseek(...) failed\n");
            return std::nullopt;
        }
        file >> TX_WITH_WITNESS(tx);
    } catch (const std::exception& e) {
        LogError("Deserialize or I/O error - %s\n", e.what());
        return std::nullopt;
    }

    return tx->GetHash();
}

BaseIndex::DB& TxoSpenderIndex::GetDB() const { return *m_db; }
