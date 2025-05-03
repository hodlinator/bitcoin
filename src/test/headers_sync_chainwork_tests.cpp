// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <headerssync.h>
#include <pow.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <validation.h>
#include <vector>

#include <boost/test/unit_test.hpp>

static constexpr int TARGET_BLOCKS{15000}; // Do NOT like how close this is to REDOWNLOAD_BUFFER_SIZE/14827
static const arith_uint256 CHAIN_WORK{TARGET_BLOCKS*2};

struct HeadersGeneratorSetup : public RegTestingSetup {
    /** Search for a nonce to meet (regtest) proof of work */
    void FindProofOfWork(CBlockHeader& starting_header);
    /**
     * Generate headers in a chain that build off a given starting hash, using
     * the given nVersion, advancing time by 1 second from the starting
     * prev_time, and with a fixed merkle root hash.
     */
    void GenerateHeaders(std::vector<CBlockHeader>& headers, size_t count,
            const uint256& starting_hash, const int nVersion, int prev_time,
            const uint256& merkle_root, const uint32_t nBits);
};

void HeadersGeneratorSetup::FindProofOfWork(CBlockHeader& starting_header)
{
    while (!CheckProofOfWork(starting_header.GetHash(), starting_header.nBits, Params().GetConsensus())) {
        ++(starting_header.nNonce);
    }
}

void HeadersGeneratorSetup::GenerateHeaders(std::vector<CBlockHeader>& headers,
        size_t count, const uint256& starting_hash, const int nVersion, int prev_time,
        const uint256& merkle_root, const uint32_t nBits)
{
    uint256 prev_hash = starting_hash;

    while (headers.size() < count) {
        headers.emplace_back();
        CBlockHeader& next_header = headers.back();
        next_header.nVersion = nVersion;
        next_header.hashPrevBlock = prev_hash;
        next_header.hashMerkleRoot = merkle_root;
        next_header.nTime = prev_time+1;
        next_header.nBits = nBits;

        FindProofOfWork(next_header);
        prev_hash = next_header.GetHash();
        prev_time = next_header.nTime;
    }
}

BOOST_FIXTURE_TEST_SUITE(headers_sync_chainwork_tests, HeadersGeneratorSetup)

// In this test, we construct two sets of headers from genesis, one with
// sufficient proof of work and one without.
// 1. We deliver the first set of headers and verify that the headers sync state
//    updates to the REDOWNLOAD phase successfully.
// 2. Then we deliver the second set of headers and verify that they fail
//    processing (presumably due to commitments not matching).
static void SneakyRedownload(const std::vector<CBlockHeader>& first_chain, const std::vector<CBlockHeader>& second_chain, const CBlockIndex* chain_start, size_t cached_headers);
// 3. Verify that repeating with the first set of headers in both phases is
//    successful.
static void HappyPath(const std::vector<CBlockHeader>& first_chain, const CBlockIndex* chain_start, size_t cached_headers);
// 4. Finally, repeat the second set of headers in both phases to demonstrate
//    behavior when the chain a peer provides has too little work.
static void TooLittleWork(const std::vector<CBlockHeader>& second_chain, const CBlockIndex* chain_start, size_t cached_headers);

BOOST_AUTO_TEST_CASE(headers_sync_state)
{
    std::vector<CBlockHeader> first_chain;
    std::vector<CBlockHeader> second_chain;

    const auto genesis{Params().GenesisBlock()};

    // Generate headers for two different chains (using differing merkle roots
    // to ensure the headers are different).
    GenerateHeaders(first_chain, TARGET_BLOCKS-1, genesis.GetHash(), genesis.nVersion,
            genesis.nTime, /*merkle_root=*/uint256::ZERO, genesis.nBits);

    GenerateHeaders(second_chain, TARGET_BLOCKS-2, genesis.GetHash(), genesis.nVersion,
            genesis.nTime, /*merkle_root=*/uint256::ONE, genesis.nBits);

    const CBlockIndex* chain_start = WITH_LOCK(::cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(genesis.GetHash()));

    for (size_t cached_headers : {0, 1, TARGET_BLOCKS-3, TARGET_BLOCKS-2, TARGET_BLOCKS-1, TARGET_BLOCKS, TARGET_BLOCKS+1}) {
        BOOST_TEST_MESSAGE(strprintf("Testing with cache size of %d", cached_headers));
        SneakyRedownload(first_chain, second_chain, chain_start, cached_headers);
        HappyPath(first_chain, chain_start, cached_headers);
        TooLittleWork(second_chain, chain_start, cached_headers);
    }
}

static void SneakyRedownload(const std::vector<CBlockHeader>& first_chain, const std::vector<CBlockHeader>& second_chain, const CBlockIndex* chain_start, size_t cached_headers)
{
    std::vector<CBlockHeader> headers_batch;

    // Feed the first chain to HeadersSyncState, by delivering 1 header
    // initially and then the rest.
    headers_batch.insert(headers_batch.end(), std::next(first_chain.begin()), first_chain.end());

    HeadersSyncState hss{0, Params().GetConsensus(), chain_start, CHAIN_WORK, /*cache_bytes=*/cached_headers * sizeof(CompressedHeader)};
    auto result = hss.ProcessNextHeaders({first_chain.front()}, true);
    BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::PRESYNC);
    BOOST_CHECK(result.success);
    BOOST_CHECK(result.request_more);
    BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.front(), first_chain.front().GetHash());
    BOOST_CHECK(result.pow_validated_headers.empty());

    // Pretend the first header is still "full", so we don't abort.
    result = hss.ProcessNextHeaders(headers_batch, true);
    if (cached_headers < first_chain.size()) {
        // This chain should look valid, and we should have met the proof-of-work
        // requirement during PRESYNC and transitioned to REDOWNLOAD.
        BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::REDOWNLOAD);
        BOOST_CHECK(result.success);
        BOOST_CHECK(result.request_more);
        if (cached_headers == 0) {
            // The locator should reset to genesis.
            BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.front(), Params().GenesisBlock().GetHash());
        } else {
            // The locator should reset to the Nth header after genesis, since it was cached.
            BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.front(), (first_chain.begin() + std::min(cached_headers, first_chain.size()) - 1)->GetHash());
        }
        if (cached_headers <= 1) {
            BOOST_CHECK(result.pow_validated_headers.empty());
        } else if (cached_headers >= TARGET_BLOCKS-1) {
            BOOST_CHECK(!result.pow_validated_headers.empty());
        }

        // Try to sneakily feed back the second chain during REDOWNLOAD.
        result = hss.ProcessNextHeaders(second_chain, true);
        BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::FINAL);
        BOOST_CHECK(!result.success); // foiled!
        BOOST_CHECK(result.pow_validated_headers.empty());
    } else {
        // If our cache was big enough to fit the first set of headers, we actually succeed.
        BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::FINAL);
        BOOST_CHECK(result.success);
        BOOST_CHECK(!result.request_more);
        BOOST_CHECK(result.pow_validated_headers.size() == first_chain.size());
    }
}

static void HappyPath(const std::vector<CBlockHeader>& first_chain, const CBlockIndex* chain_start, size_t cached_headers)
{
    // This time we feed the first chain twice.
    HeadersSyncState hss{0, Params().GetConsensus(), chain_start, CHAIN_WORK, /*cache_bytes=*/cached_headers * sizeof(CompressedHeader)};
    auto result = hss.ProcessNextHeaders(first_chain, true);
    std::vector<CBlockHeader> validated_headers;
    if (cached_headers < first_chain.size()) {
        // Well, we only feed it a second time if our cache is insufficient.
        BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::REDOWNLOAD);
        BOOST_CHECK(result.success);
        BOOST_CHECK(result.request_more);
        if (cached_headers == 0) {
            // The locator should reset to genesis.
            BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.front(), Params().GenesisBlock().GetHash());
            BOOST_CHECK(result.pow_validated_headers.empty());
        } else {
            // The locator should reset to the Nth header after genesis, since it was cached.
            BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.front(), (first_chain.begin() + std::min(cached_headers, first_chain.size()) - 1)->GetHash());
        }
        validated_headers = std::move(result.pow_validated_headers);

        std::vector<CBlockHeader> headers_batch;
        if (cached_headers < first_chain.size()) {
            headers_batch.insert(headers_batch.end(), first_chain.begin() + std::min(cached_headers, first_chain.size()), first_chain.end());
        }
        result = hss.ProcessNextHeaders(headers_batch, /*full_headers_message=*/true);
    }
    // Nothing left for the sync logic to do:
    BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::FINAL);
    BOOST_CHECK(result.success);
    BOOST_CHECK(!result.request_more);
    // All headers should be ready for acceptance:
    validated_headers.insert(validated_headers.end(), result.pow_validated_headers.begin(), result.pow_validated_headers.end());
    BOOST_CHECK_EQUAL(validated_headers.size(), first_chain.size());
}

static void TooLittleWork(const std::vector<CBlockHeader>& second_chain, const CBlockIndex* chain_start, size_t cached_headers)
{
    // Verify that just trying to process the second chain would not succeed
    // (too little work).
    HeadersSyncState hss{0, Params().GetConsensus(), chain_start, CHAIN_WORK, /*cache_bytes=*/cached_headers * sizeof(CompressedHeader)};
    BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::PRESYNC);
    // Pretend just the first message is "full", so we don't abort.
    auto result = hss.ProcessNextHeaders({second_chain.front()}, true);
    BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::PRESYNC);
    BOOST_CHECK(result.success);
    BOOST_CHECK(result.request_more);

    std::vector<CBlockHeader> headers_batch;
    headers_batch.insert(headers_batch.end(), std::next(second_chain.begin(), 1), second_chain.end());
    // Tell the sync logic that the headers message was not full, implying no
    // more headers can be requested. For a low-work-chain, this should cause
    // the sync to end with no headers for acceptance.
    result = hss.ProcessNextHeaders(headers_batch, false);
    BOOST_REQUIRE_EQUAL(hss.GetState(), HeadersSyncState::State::FINAL);
    BOOST_CHECK(result.pow_validated_headers.empty());
    BOOST_CHECK(!result.request_more);
    // Nevertheless, no validation errors should have been detected with the
    // chain:
    BOOST_CHECK(result.success);
}

BOOST_AUTO_TEST_SUITE_END()
