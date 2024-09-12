// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <index/disktxpos.h>
#include <index/txospenderindex.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <primitives/transaction.h>
#include <validation.h>

std::unique_ptr<TxoSpenderIndex> g_txospenderindex;

/** Access to the txo spender index database (indexes/txospenderindex/) */
class TxoSpenderIndex::DB : public BaseIndex::DB
{
    // LeveLDB key prefix. We only have one key for now but it will make it easier to add others if needed.
    static constexpr uint8_t DB_TXOSPENDERINDEX{'s'};

public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    bool WriteSpenderInfos(const std::vector<std::pair<COutPoint, CDiskTxPos>>& items);
    bool EraseSpenderInfos(const std::vector<COutPoint>& items);
    std::optional<CDiskTxPos> FindSpender(const COutPoint& txo) const;
};

TxoSpenderIndex::DB::DB(size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex::DB(gArgs.GetDataDirNet() / "indexes" / "txospenderindex", n_cache_size, f_memory, f_wipe)
{
}

TxoSpenderIndex::TxoSpenderIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "txospenderindex")
    , m_db(std::make_unique<TxoSpenderIndex::DB>(n_cache_size, f_memory, f_wipe))
{
}

TxoSpenderIndex::~TxoSpenderIndex() = default;

bool TxoSpenderIndex::DB::WriteSpenderInfos(const std::vector<std::pair<COutPoint, CDiskTxPos>>& items)
{
    CDBBatch batch(*this);
    for (const auto& [outpoint, pos] : items) {
        batch.Write(std::pair{DB_TXOSPENDERINDEX, outpoint}, pos);
    }
    return WriteBatch(batch);
}

bool TxoSpenderIndex::DB::EraseSpenderInfos(const std::vector<COutPoint>& items)
{
    CDBBatch batch(*this);
    for (const auto& outpoint : items) {
        batch.Erase(std::pair{DB_TXOSPENDERINDEX, outpoint});
    }
    return WriteBatch(batch);
}

std::optional<CDiskTxPos> TxoSpenderIndex::DB::FindSpender(const COutPoint& txo) const
{
    CDiskTxPos pos_out;
    if (Read(std::pair{DB_TXOSPENDERINDEX, txo}, pos_out)) {
        return pos_out;
    }
    return std::nullopt;
}

bool TxoSpenderIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    std::vector<std::pair<COutPoint, CDiskTxPos>> items;
    items.reserve(block.data->vtx.size());

    CDiskTxPos pos({block.file_number, block.data_pos}, GetSizeOfCompactSize(block.data->vtx.size()));
    for (size_t i{0}; i < block.data->vtx.size();
        pos.nTxOffset += ::GetSerializeSize(TX_WITH_WITNESS(*block.data->vtx[i])), ++i) {
        const auto& tx{block.data->vtx[i]};
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const auto& input : tx->vin) {
            items.emplace_back(input.prevout, pos);
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
        std::vector<COutPoint> items;
        items.reserve(block.vtx.size());
        for (const auto& tx : block.vtx) {
            if (tx->IsCoinBase()) {
                continue;
            }
            for (const auto& input : tx->vin) {
                items.emplace_back(input.prevout);
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
    auto pos = m_db->FindSpender(txo);
    if (!pos) return std::nullopt;

    AutoFile file{m_chainstate->m_blockman.OpenBlockFile(*pos, true)};
    if (file.IsNull()) {
        LogError("OpenBlockFile failed\n");
        return std::nullopt;
    }
    CBlockHeader header;
    CTransactionRef tx;
    try {
        file >> header;
        if (fseek(file.Get(), pos->nTxOffset, SEEK_CUR)) {
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
