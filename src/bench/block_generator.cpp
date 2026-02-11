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

auto createScriptFactory(FastRandomContext& rng, const benchmark::ScriptRecipe& rec)
{
    std::array<std::pair<double, std::function<CScript()>>, 11> table{
        std::pair{rec.anchor_prob, [&] { return GetScriptForDestination(PayToAnchor{}); }},
        std::pair{rec.multisig_prob, [&] {
            const size_t keys_count{1 + rng.randrange<size_t>(15)};
            const size_t required{1 + rng.randrange<size_t>(keys_count)};
            std::vector<CPubKey> keys;
            keys.reserve(keys_count);
            for (size_t i{}; i < keys_count; ++i) keys.emplace_back(RandPub(rng));
            return GetScriptForMultisig(required, keys);
        }},
        {rec.null_data_prob, [&] {
            const auto len{1 + rng.randrange<size_t>(90)}; // sometimes exceed policy rules
            return CScript() << OP_RETURN << rng.randbytes<unsigned char>(len);
        }},
        std::pair{rec.pubkey_prob, [&] { return GetScriptForRawPubKey(RandPub(rng)); }},
        std::pair{rec.pubkeyhash_prob, [&] { return GetScriptForDestination(PKHash(RandPub(rng))); }},
        std::pair{rec.scripthash_prob, [&] { return GetScriptForDestination(ScriptHash(CScript() << OP_TRUE)); }},
        std::pair{rec.witness_v1_taproot_prob, [&] { return GetScriptForDestination(WitnessV1Taproot(XOnlyPubKey(RandPub(rng)))); }},
        std::pair{rec.witness_v0_keyhash_prob, [&] { return GetScriptForDestination(WitnessV0KeyHash(RandPub(rng))); }},
        std::pair{rec.witness_v0_scripthash_prob, [&] { return GetScriptForDestination(WitnessV0ScriptHash(CScript() << OP_TRUE)); }},
        std::pair{rec.witness_unknown_prob, [&] { return CScript() << OP_2 << rng.randbytes<uint8_t>(32); }},
        std::pair{rec.nonstandard_prob, [&] { return CScript() << OP_TRUE; }},
    };

    double sum{};
    for (const auto& p : table | std::views::keys) sum += p;
    // Verify that probabilities add up to ~1.0.
    assert(sum <= 1);
    assert(sum + 0.01 > 1.0);

    return table;
}

CBlock BuildBlock(const CChainParams& params, const benchmark::ScriptRecipe& rec, const uint256& seed)
{
    assert(params.IsTestChain());
    FastRandomContext rng{seed};

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

    auto scriptFactory{createScriptFactory(rng, rec)};
    auto rand_script{[&] {
        double probability{MakeUnitDouble(rng.rand64())};
        for (const auto& [p, factory] : scriptFactory) {
            if (probability < p) return factory();
            probability -= p;
        }
        return scriptFactory.back().second();
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
            tx_in.scriptSig = rand_script();

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
            tx_out.scriptPubKey = rand_script();
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
