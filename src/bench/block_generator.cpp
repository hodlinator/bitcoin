// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <bench/block_generator.h>
#include <consensus/merkle.h>
#include <pow.h>
#include <primitives/block.h>
#include <random.h>
#include <script/script.h>
#include <script/solver.h>
#include <streams.h>
#include <test/util/transaction_utils.h>
#include <validation.h>
#include <versionbits.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <ranges>
#include <vector>

using namespace util::hex_literals;

namespace {
size_t GeomCount(FastRandomContext& rng, double thresh_prob)
{
    size_t n{1};
    while (rng.randrange<uint8_t>(100) < thresh_prob * 100) {
        ++n;
    }
    return n;
}

CPubKey RandPub(FastRandomContext& rng)
{
    auto pubkey{rng.randbytes<CPubKey::COMPRESSED_SIZE, unsigned char>()};
    pubkey[0] = rng.randbool() ? 0x02 : 0x03;
    return CPubKey(pubkey.begin(), pubkey.end());
}

struct FactoryEntry {
    const double prob;
    const std::function<CScript()> lock_script;
    const std::function<CScript()> unlock_script;
    const std::function<CScript()> witness_script; // TODO write + use field
};

auto createScriptFactory(FastRandomContext& rng, const CExtKey& xprv, const benchmark::ScriptRecipe& rec)
{
    FactoryEntry table[] = {
        {
            .prob = rec.anchor_prob,
            .lock_script = [&] { return GetScriptForDestination(PayToAnchor{}); },
            .unlock_script = {}, // TODO: confirm correctness
            .witness_script = {}, // TODO: confirm correctness
        },
        {
            .prob = rec.multisig_prob,
            .lock_script = [&] {
                const size_t keys_count{1 + rng.randrange<size_t>(MAX_PUBKEYS_PER_MULTISIG)};
                const size_t required{1 + rng.randrange<size_t>(keys_count)};
                std::vector<CPubKey> keys;
                keys.reserve(keys_count);
                for (size_t i{}; i < keys_count; ++i) keys.emplace_back(RandPub(rng));
                return GetScriptForMultisig(required, keys);
            },
            .unlock_script = [&] {
                const size_t keys_count{1 + rng.randrange<size_t>(MAX_PUBKEYS_PER_MULTISIG)};
                const size_t required{1 + rng.randrange<size_t>(keys_count)};
                std::vector<CKey> priv_keys;
                priv_keys.reserve(keys_count);
                std::vector<CPubKey> pub_keys;
                pub_keys.reserve(keys_count);

                for (size_t i{}; i < keys_count; ++i) {
                    CExtKey child_xprv;
                    Assert(xprv.Derive(child_xprv, rng.rand<unsigned int>()));
                    const CKey priv{child_xprv.key};
                    priv_keys.push_back(priv);
                    pub_keys.push_back(priv.GetPubKey());
                }

                const CScript prevout_lock_script{GetScriptForMultisig(required, pub_keys)};

                CScript s{OP_0}; // Extra required dummy value for CHECKMULTISIG.
                const uint8_t sighash_type{SIGHASH_ALL};
                CMutableTransaction tx;
                tx.vin.emplace_back(COutPoint{}, CScript{}, rng.rand32());
                const uint256 sighash{SignatureHash(prevout_lock_script, tx, 0, sighash_type, CAmount{0}, SigVersion::BASE)};
                for (size_t i{}; i < required; ++i) {
                    std::vector<uint8_t> sig;
                    Assert(priv_keys[i].Sign(sighash, sig));
                    sig.push_back(sighash_type);
                    s << sig;
                }

                return s;
            },
            .witness_script = {}, // TODO: confirm correctness
        },
        {
            .prob = rec.null_data_prob,
            .lock_script = [&] {
                const auto len{1 + rng.randrange<size_t>(90)}; // sometimes exceed policy rules
                return CScript{} << OP_RETURN << rng.randbytes(len);
            },
            .unlock_script = {},  // Unspendable
            .witness_script = {}, // Unspendable
        },
        {
            .prob = rec.pubkey_prob,
            .lock_script = [&] { return GetScriptForRawPubKey(RandPub(rng)); },
            .unlock_script = [&] {
                CExtKey child_xprv;
                Assert(xprv.Derive(child_xprv, rng.rand<unsigned int>()));
                const CKey priv{child_xprv.key};
                const CPubKey pub{priv.GetPubKey()};
                const CScript prevout_lock_script{GetScriptForRawPubKey(pub)};

                const uint8_t sighash_type{SIGHASH_ALL};
                CMutableTransaction tx;
                tx.vin.emplace_back(COutPoint{}, CScript{}, rng.rand32());
                const uint256 sighash{SignatureHash(prevout_lock_script, tx, 0, sighash_type, CAmount{0}, SigVersion::BASE)};
                std::vector<uint8_t> sig;
                Assert(priv.Sign(sighash, sig));
                sig.push_back(sighash_type);

                return CScript{} << sig;
            },
            .witness_script = {},
        },
        {
            .prob = rec.pubkeyhash_prob,
            .lock_script = [&] { return GetScriptForDestination(PKHash(RandPub(rng))); },
            .unlock_script = [&] {
                CExtKey child_xprv;
                Assert(xprv.Derive(child_xprv, rng.rand<unsigned int>()));
                const CKey priv{child_xprv.key};
                const CPubKey pub{priv.GetPubKey()};
                const CScript prevout_lock_script{GetScriptForRawPubKey(pub)};

                const uint8_t sighash_type{SIGHASH_ALL};
                CMutableTransaction tx;
                tx.vin.emplace_back(COutPoint{}, CScript{}, rng.rand32());
                const uint256 sighash{SignatureHash(prevout_lock_script, tx, 0, sighash_type, CAmount{0}, SigVersion::BASE)};
                std::vector<uint8_t> sig;
                Assert(priv.Sign(sighash, sig));
                sig.push_back(sighash_type);

                return CScript{} << sig << pub;
            },
            .witness_script = {},
        },
        {
            .prob = rec.scripthash_prob,
            .lock_script = [&] { return GetScriptForDestination(ScriptHash(CScript() << OP_TRUE)); },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
        {
            .prob = rec.witness_v1_taproot_prob,
            .lock_script = [&] { return GetScriptForDestination(WitnessV1Taproot(XOnlyPubKey(RandPub(rng)))); },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
        {
            .prob = rec.witness_v0_keyhash_prob,
            .lock_script = [&] { return GetScriptForDestination(WitnessV0KeyHash(RandPub(rng))); },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
        {
            .prob = rec.witness_v0_scripthash_prob,
            .lock_script = [&] { return GetScriptForDestination(WitnessV0ScriptHash(CScript() << OP_TRUE)); },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
        {
            .prob = rec.witness_unknown_prob,
            .lock_script = [&] { return CScript() << OP_2 << rng.randbytes<uint8_t>(32); },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
        {
            .prob = rec.nonstandard_prob,
            .lock_script = [&] { return CScript() << OP_TRUE; },
            .unlock_script = {}, // TODO
            .witness_script = {}, // TODO
        },
    };

    double sum{};
    for (const auto& e : table) sum += e.prob;
    // Verify that probabilities add up to ~1.0.
    assert(sum <= 1);
    assert(sum + 0.01 > 1.0);

    return std::to_array(table);
}

CBlock BuildBlock(const CChainParams& params, const benchmark::ScriptRecipe& rec, const uint256& seed)
{
    assert(params.IsTestChain());
    FastRandomContext rng{seed};

    CExtKey xprv;
    constexpr auto xprv_seed{std::to_array({std::byte{'2'}, std::byte{'1'}})};
    xprv.SetSeed(xprv_seed);

    assert(rec.geometric_base_prob >= 0 && rec.geometric_base_prob <= 1);
    const auto tx_occupancy_limit{rec.tx_occupancy_limit != 0.0 ? rec.tx_occupancy_limit : MakeUnitDouble(rng.rand64())};

    CBlock block{};
    block.vtx.reserve(1 + (4000 * tx_occupancy_limit));

    // coinbase
    {
        CMutableTransaction cb;
        cb.vin = {CTxIn(COutPoint())};
        cb.vin[0].scriptSig = CScript() << CScriptNum(rng.randrange(1'000'000)) << OP_0;
        cb.vout = {CTxOut(rng.randrange(50 * COIN), CScript() << OP_TRUE)};
        block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    }

    auto scriptFactory{createScriptFactory(rng, xprv, rec)};
    auto rand_lock_script{[&] {
        double probability{MakeUnitDouble(rng.rand64())};
        for (const auto& entry : scriptFactory) {
            if (probability < entry.prob) return entry.lock_script();
            probability -= entry.prob;
        }
        return scriptFactory.back().lock_script();
    }};
    const double unlock_script_prob{[&] {
        double sum{0.0};
        for (const auto& entry : scriptFactory) {
            if (entry.unlock_script) sum += entry.prob;
        }
        return sum;
    }()};
    auto rand_unlock_script{[&] {
        const FactoryEntry* last_unlock_entry{nullptr};
        double probability{MakeUnitDouble(rng.rand64()) * unlock_script_prob};
        for (const auto& entry : scriptFactory) {
            if (!entry.unlock_script) continue;
            last_unlock_entry = &entry;
            if (probability < entry.prob) return entry.unlock_script();
            probability -= entry.prob;
        }
        assert(last_unlock_entry);
        return last_unlock_entry->unlock_script();
    }};

    // Add 2 bytes to account for compact size representation of vtx vector increasing from 1 to 3 bytes.
    uint64_t block_size_no_witness{::GetSerializeSize(TX_NO_WITNESS(block)) + 2};
    uint64_t block_size_with_witness{::GetSerializeSize(TX_WITH_WITNESS(block)) + 2};
    const uint64_t block_size_limit(tx_occupancy_limit * MAX_BLOCK_WEIGHT);
    while (block_size_no_witness * WITNESS_SCALE_FACTOR < block_size_limit
           && block_size_no_witness * WITNESS_SCALE_FACTOR + (block_size_with_witness - block_size_no_witness) < block_size_limit) {
        CMutableTransaction tx;
        tx.version = 1 + rng.randrange<int>(3);
        tx.nLockTime = (rng.randrange<uint8_t>(100) < 90) ? 0 : rng.rand32();

        const size_t in_count{GeomCount(rng, rec.geometric_base_prob)};
        tx.vin.resize(in_count);
        for (size_t in{0}; in < in_count; ++in) {
            auto& tx_in{tx.vin[in]};
            tx_in.prevout = {Txid::FromUint256(rng.rand256()), uint32_t(GeomCount(rng, rec.geometric_base_prob))};
            tx_in.scriptSig = rand_unlock_script();

            const size_t witness_count{GeomCount(rng, rec.geometric_base_prob)};
            tx_in.scriptWitness.stack.reserve(witness_count);
            for (size_t w{0}; w < witness_count; ++w) {
                tx_in.scriptWitness.stack.emplace_back(rng.randbytes<uint8_t>(1 + rng.randrange(100)));
            }

            tx_in.nSequence = (rng.randrange<uint8_t>(100) < 99) ? CTxIn::SEQUENCE_FINAL : rng.rand32();
        }

        const size_t out_count{GeomCount(rng, rec.geometric_base_prob)};
        tx.vout.resize(out_count);
        for (size_t out{0}; out < out_count; ++out) {
            auto& tx_out{tx.vout[out]};
            tx_out.nValue = rng.randrange(GeomCount(rng, rec.geometric_base_prob) * COIN);
            tx_out.scriptPubKey = rand_lock_script();
        }

        block_size_no_witness += ::GetSerializeSize(TX_NO_WITNESS(tx));
        block_size_with_witness += ::GetSerializeSize(TX_WITH_WITNESS(tx));
        block.vtx.push_back(MakeTransactionRef(std::move(tx)));
    }
    // Remove the transaction that had us exceed the limit.
    block.vtx.pop_back();

    block.nVersion = 1 + rng.randrange<int>(VERSIONBITS_LAST_OLD_BLOCK_VERSION);
    block.nTime = params.GenesisBlock().nTime;
    block.hashPrevBlock.SetNull();
    block.nBits = UintToArith256(params.GetConsensus().powLimit).GetCompact(); // lowest difficulty
    block.nNonce = rng.rand32();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    while (!CheckProofOfWork(block.GetHash(), block.nBits, params.GetConsensus())) {
        ++block.nNonce;
    }

    // Make sure we've generated a valid block
    {
        BlockValidationState validationState;
        const bool checked{CheckBlock(block, validationState, params.GetConsensus())};
        assert(checked);
    }

    return block;
}

DataStream SerializeBlock(const CBlock& blk)
{
    DataStream ds;
    ds << TX_WITH_WITNESS(blk);
    return ds;
}
} // namespace

namespace benchmark {
DataStream GetBlockData(const CChainParams& chain_params, const ScriptRecipe& recipe, const uint256& seed)
{
    return SerializeBlock(BuildBlock(chain_params, recipe, seed));
}

CBlock GetBlock(const CChainParams& chain_params, const ScriptRecipe& recipe, const uint256& seed)
{
    return BuildBlock(chain_params, recipe, seed);
}
} // namespace benchmark
