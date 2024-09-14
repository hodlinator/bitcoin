// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <crypto/common.h>
#include <index/disktxpos.h>
#include <index/txospenderindex.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <cstdint>

std::unique_ptr<TxoSpenderIndex> g_txospenderindex;

/** Access to the txo spender index database (indexes/txospenderindex/)
 *
 * Since LevelDB only supports unique keys, prefix keyed versions of transaction
 * outpoints may collide. (If non-unique keys were allowed like in a multimap,
 * one could disambiguate identical prefix keys by de-serializing the
 * transaction in each value and searching the vin's for the full outpoint being
 * queried).
 * We solve this by replacing collided prefixed keys with the tombstone value -1
 * and writing the previous value into a "moved" entry, then writing otherwise
 * colliding entries with full tx hash + n. */
class TxoSpenderIndex::DB : public BaseIndex::DB
{
    static constexpr int COLLISION_TOMBSTONE = -1;

    // LevelDB key prefixes.
    static constexpr uint8_t DB_TXOSPENDERINDEX_P{'p'}; // Prefixes, value may be COLLISION_TOMBSTONE
    static constexpr uint8_t DB_TXOSPENDERINDEX_F{'f'}; // Fully unique outpoints: tx hash + n
    static constexpr uint8_t DB_TXOSPENDERINDEX_M{'m'}; // Moved prefixes that ran into collisions

    static constexpr size_t PREFIX_KEY_SIZE = 8;

    static std::array<uint8_t, PREFIX_KEY_SIZE> MakePrefixKey(const COutPoint& txo)
    {
        std::array<uint8_t, PREFIX_KEY_SIZE> rv;
        const uint256& hash = txo.hash.ToUint256();
        std::copy(hash.begin(), hash.begin() + rv.size(), rv.begin());
        // (Hash + N)
        assert(rv.size() >= sizeof(txo.n));
        // Endian-neutral
        static_assert(sizeof(txo.n) == 4);
        rv[0] += (txo.n >>  0) & 0xFF;
        rv[1] += (txo.n >>  8) & 0xFF;
        rv[2] += (txo.n >> 16) & 0xFF;
        rv[3] += (txo.n >> 24) & 0xFF;
        return rv;
    }

    static std::array<uint8_t, 36> MakeFullKey(const COutPoint& txo)
    {
        const uint256& hash = txo.hash.ToUint256();
        std::array<uint8_t, uint256::size() + sizeof(COutPoint::n)> rv;
        // In order to fully prevent collisions, the full version keeps hash + n separate.
        // (Hash | N)
        std::copy(hash.begin(), hash.end(), rv.begin());
        static_assert(sizeof(txo.n) == 4);
        WriteLE32(rv.begin() + uint256::size(), txo.n);
        return rv;
    }

public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    struct Key {
        std::array<uint8_t, PREFIX_KEY_SIZE> inner;
        Key(const COutPoint& txo) : inner{MakePrefixKey(txo)} {}

        SERIALIZE_METHODS(Key, k) { READWRITE(VARINT(k.inner)); }
    };

    bool WriteSpenderInfos(const std::vector<std::pair<COutPoint, CDiskTxPos>>& items);
    bool EraseSpenderInfos(const std::vector<COutPoint>& items);
    std::optional<CDiskTxPos> FindSpender(const COutPoint& txo);
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
    for (const auto& [txo, pos] : items) {
        const auto prefix = MakePrefixKey(txo);
        CDiskTxPos pos_old;
        // Do we have a previous entry with the same key?
        if (Read(std::pair{DB_TXOSPENDERINDEX_P, prefix}, pos_old)) {
            if (pos_old == pos) {
                // We already have this exact entry. Weird that we are getting writes for it multiple times, but okay.
                continue;
            } else if (pos_old.nFile != COLLISION_TOMBSTONE) {
                // We found a different non-collision entry at the current prefix length. Write it into a moved entry.
                batch.Write(std::pair{DB_TXOSPENDERINDEX_M, prefix}, pos_old);
                // Replace old entry with tombstone.
                batch.Write(std::pair{DB_TXOSPENDERINDEX_P, prefix}, CDiskTxPos{FlatFilePos{COLLISION_TOMBSTONE, 0}, 0});
            }
            batch.Write(std::pair{DB_TXOSPENDERINDEX_F, MakeFullKey(txo)}, pos);
        } else {
            batch.Write(std::pair{DB_TXOSPENDERINDEX_P, prefix}, pos);
        }
    }
    return WriteBatch(batch);
}

bool TxoSpenderIndex::DB::EraseSpenderInfos(const std::vector<COutPoint>& items)
{
    CDBBatch batch(*this);
    for (const auto& txo : items) {
        const auto prefix = MakePrefixKey(txo);
        CDiskTxPos pos_old;
        // Do we have a previous entry with the same key?
        if (Read(std::pair{DB_TXOSPENDERINDEX_P, prefix}, pos_old)) {
            if (pos_old.nFile == COLLISION_TOMBSTONE) {
                const auto full_key = MakeFullKey(txo);
                if (Exists(std::pair{DB_TXOSPENDERINDEX_F, full_key})) {
                    batch.Erase(std::pair{DB_TXOSPENDERINDEX_F, full_key});
                } else {
                    // No full key match, this must be the moved one...
                    // Assuming we never get duplicate deletes for the same entry.
                    assert(Exists(std::pair{DB_TXOSPENDERINDEX_M, prefix}));
                    batch.Erase(std::pair{DB_TXOSPENDERINDEX_M, prefix});
                }
            } else {
                batch.Erase(std::pair{DB_TXOSPENDERINDEX_P, prefix});
            }
        } else {
            assert(false); // Erasing an entry that doesn't exist?
        }
    }
    return WriteBatch(batch);
}

std::optional<CDiskTxPos> TxoSpenderIndex::DB::FindSpender(const COutPoint& txo)
{
    const auto prefix = MakePrefixKey(txo);
    CDiskTxPos pos_out;
    if (Read(std::pair{DB_TXOSPENDERINDEX_P, prefix}, pos_out)) {
        if (pos_out.nFile == COLLISION_TOMBSTONE) {
            // We had a collision. Check if we have an exact match on the full key.
            if (Read(std::pair{DB_TXOSPENDERINDEX_F, MakeFullKey(txo)}, pos_out)) {
                return pos_out;
            // No full key, check if we are the moved prefix version.
            } else if (Read(std::pair{DB_TXOSPENDERINDEX_M, prefix}, pos_out)) {
                return pos_out;
            }
        } else {
            // We found an normal entry at the short prefix length.
            return pos_out;
        }
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
