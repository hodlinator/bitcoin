#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPC misc output."""
import xml.etree.ElementTree as ET

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_raises_rpc_error,
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)

from test_framework.authproxy import JSONRPCException


class RpcMiscTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        for i in range(200):
            self.log.info(f"Restart {i}")
            self.restart_node(0)


if __name__ == '__main__':
    RpcMiscTest(__file__).main()
