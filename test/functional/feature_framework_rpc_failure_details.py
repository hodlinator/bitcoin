#!/usr/bin/env python3
# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Verify timing out in connecting to bitcoind's RPC interface throws an exception
including details on what kind of failures happened before timing out.
"""

from test_framework.util import (
    assert_raises_message,
    rpc_port,
)
from test_framework.test_framework import BitcoinTestFramework

import re

class FeatureFrameworkRPCFailureDetails(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        # Lower the timeout so we don't wait that long
        self.rpc_timeout = 2

    # Overridden to avoid syncing non-started nodes.
    def setup_network(self):
        self.setup_nodes()

    # Overridden to avoid starting nodes before run_test.
    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)

    def run_test(self):
        e = assert_raises_message(
            exc=AssertionError,
            message="[node 0] Unable to connect to bitcoind after ",
            fun=BitcoinTestFramework.start_node,
            self=self,
            i=0,
            # Override RPC port to something TestNode isn't expecting so that we are
            # unable to establish an RPC connection.
            extra_args=[f'-rpcport={rpc_port(2)}'],
        )
        assert re.match(
            r"^\[node 0\] Unable to connect to bitcoind after \d*s \(ignored errors: {'.*'OSError .*': .*, latest error: .*$", \
            str(e)), \
            f'Didn\'t find expected details in "{e}"'
        self.log.info("One WARNING expected - Explicitly stopping the node to verify it completes cleanly during the test")
        self.nodes[0].stop_node(avoid_exceptions=True)

if __name__ == '__main__':
    FeatureFrameworkRPCFailureDetails(__file__).main()
