#!/usr/bin/env python3
# Copyright (c) The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that we emit proper warnings for invalidated blocks

Blocks have been invalidated in the wild due to a corrupt/out of sync UTXO set
which resulted in tx inputs not being found:
https://github.com/bitcoin/bitcoin/pull/33553#issuecomment-3406488480
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import MAX_HEADERS_RESULTS
from test_framework.util import assert_equal

class HeadersSyncInvalidatedTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def run_test(self):
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]
        n2.stop_node()

        genesis = n1.getbestblockhash()
        self.log.info("Generate and share a 7-block chain (not counting genesis) among our nodes")
        self.generate(n0, 7, sync_fun=lambda: self.sync_blocks([n0, n1]))
        assert_equal(n1.getblockcount(), 7)

        self.log.info("Invalidate tip (block 7) on node 1")
        temp_invalid = n1.getbestblockhash()
        assert temp_invalid != genesis
        n1.invalidateblock(temp_invalid)

        self.log.info("Verify that we detect peer trying to feed us an invalidated chain (block), but we don't blame corruption yet.")
        corruption_msg = "Found invalid chain at least ~6 blocks longer than our best chain. Chain state database corruption likely."
        with n1.assert_debug_log(expected_msgs=["received from peer=0 was previously marked as invalid. If this happens with all peers, consider database corruption (that -reindex may fix) or a potential consensus incompatibility."],
                                 unexpected_msgs=[corruption_msg], timeout=5):
            self.restart_node(1)
            self.connect_nodes(1, 0)
        n0.stop_node()  # Don't need this until later.

        self.log.info("Invalidate block 1 on node 1")
        temp_invalid = n1.getblockhash(1)
        n1.invalidateblock(temp_invalid)

        self.log.info("Restart node 1 to verify we blame corruption on load now that the invalid chain is 6 blocks longer than the valid one")
        with n1.assert_debug_log(expected_msgs=[corruption_msg], timeout=5):
            self.restart_node(1)
        n1.stop_node()

        self.log.info(f"Generate {MAX_HEADERS_RESULTS + 1} new blocks on node 2 in order to be able to trigger creation of new HeadersSyncState in net_processing.cpp")
        self.start_node(2)
        self.generate(n2, MAX_HEADERS_RESULTS + 1, sync_fun=self.no_op)

        self.log.info("Start node 1 and connect to node 2 to see if we get the expected pre-synchronization loop")
        self.start_node(1, extra_args=[f"-minimumchainwork={hex(MAX_HEADERS_RESULTS*3)}"])  # Need to a higher limit than MAX_HEADERS_RESULTS to initiate headers-sync.
        with n1.assert_debug_log(expected_msgs=["Pre-synchronizing blockheaders", "Initial headers sync aborted with peer=0: incomplete headers message at height=2001 (presync phase)"], timeout=5):
            self.connect_nodes(1, 2)

        self.restart_node(1, extra_args=["-minimumchainwork=0x0"])  # Accept any number of blocks
        self.connect_nodes(1, 2)
        self.sync_blocks([n1, n2])

        assert_equal(n1.getblockcount(), 2001)

        temp_invalid = n1.getbestblockhash()
        n1.invalidateblock(temp_invalid)
        n1.stop_node()

        self.log.info(f"Continue generating {MAX_HEADERS_RESULTS * 2} new blocks on node 2")
        self.generate(n2, MAX_HEADERS_RESULTS * 2, sync_fun=self.no_op)

        self.log.info("Connect node 1 to node 2 to see if we get another pre-synchronization loop")
        with n1.assert_debug_log(expected_msgs=["Pre-synchronizing blockheaders", "received from peer=0 was previously marked as invalid. If this happens with all peers, consider database corruption (that -reindex may fix) or a potential consensus incompatibility."], timeout=5):
            self.start_node(1, extra_args=[f"-minimumchainwork={hex(MAX_HEADERS_RESULTS*5)}"])
            self.connect_nodes(1, 2)

        self.disconnect_nodes(1, 2)
        self.start_node(0, extra_args=["-minimumchainwork=0x0"])
        self.connect_nodes(0, 2)
        self.log.info("Synchronize blocks between nodes 0 and 2")
        self.sync_blocks([n0, n2], timeout=500)

        self.log.info("Verify that second node with same chain triggers same issue")
        with n1.assert_debug_log(expected_msgs=["Pre-synchronizing blockheaders", "received from peer=1 was previously marked as invalid. If this happens with all peers, consider database corruption (that -reindex may fix) or a potential consensus incompatibility."], timeout=5):
            self.connect_nodes(1, 0)

if __name__ == '__main__':
    HeadersSyncInvalidatedTest(__file__).main()
