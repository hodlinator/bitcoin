#!/usr/bin/env python3
# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Verify various startup failures only raise one exception since multiple
exceptions being raised muddies the waters of what actually went wrong.
We should maintain this bar of only raising one exception as long as
additional maintenance and complexity is low.

Test relaunches itself into a child processes in order to trigger failure
without the parent process' BitcoinTestFramework also failing.
"""

from test_framework.util import (
    assert_equal,
    rpc_port,
)
from test_framework.test_framework import BitcoinTestFramework

import re
import subprocess
import sys

class FeatureFrameworkRPCFailure(BitcoinTestFramework):
    def set_test_params(self):
        # Only run a node for child processes
        self.num_nodes = 1 if self.options.rpc_timeout is not None or self.options.extra_args else 0

        if self.options.rpc_timeout is not None:
            self.rpc_timeout = self.options.rpc_timeout
        if self.options.extra_args:
            self.extra_args = [[self.options.extra_args]]

    def add_options(self, parser):
        parser.add_argument("--rpc_timeout", dest="rpc_timeout", help="ONLY TO BE USED WHEN TEST RELAUNCHES ITSELF")
        parser.add_argument("--extra_args", dest="extra_args", help="ONLY TO BE USED WHEN TEST RELAUNCHES ITSELF")

    def setup_network(self):
        # Can only call base if num_nodes > 0, otherwise it will fail.
        if self.num_nodes > 0:
            BitcoinTestFramework.setup_network(self)

    def _run_test_internal(self, args, expected_exception):
        try:
            result = subprocess.run([sys.executable, __file__] + args, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10 * self.options.timeout_factor)
        except subprocess.TimeoutExpired as e:
            print(f"Unexpected timeout, subprocess output:\n{e.output}\nSubprocess output end", file=sys.stderr)
            raise

        # Expect exactly 1 traceback, coming from the RPC timeout assertion.
        assert_equal(len(re.findall("Traceback", result.stdout)), 1)

        # Expect exactly 1 AssertionError output.
        assert_equal(len(re.findall(expected_exception, result.stdout)), 1)

        # Expect exactly 1 test failure output message.
        assert_equal(len(re.findall("Test failed. Test logging available at", result.stdout)), 1)

    def test_instant_rpc_timeout(self):
        self.log.info("Verifying timeout in connecting to bitcoind's RPC interface results in only one exception.")
        self._run_test_internal(
            ["--rpc_timeout=0"],
            "AssertionError: \\[node 0\\] Unable to connect to bitcoind after 0s"
        )

    def test_wrong_rpc_port(self):
        self.log.info("Verifying inability to connect to bitcoind's RPC interface due to wrong port results in one exception containing at least one OSError.")
        self._run_test_internal(
            # Lower the timeout so we don't wait that long.
            ["--rpc_timeout=2",
            # Override RPC port to something TestNode isn't expecting so that we
            # are unable to establish an RPC connection.
            f"--extra_args=-rpcport={rpc_port(2)}"],
            r"AssertionError: \[node 0\] Unable to connect to bitcoind after \d+s \(ignored errors: {[^}]*'OSError \w+'?: \d+[^}]*}, latest error: \w+\([^)]+\)\)"
        )

    def test_init_error(self):
        self.log.info("Verify startup failure due to invalid arg results in only one exception.")
        self._run_test_internal(
            ["--extra_args=-nonexistentarg"],
            "FailedToStartError: \\[node 0\\] bitcoind exited with status 1 during initialization. Error: Error parsing command line arguments: Invalid parameter -nonexistentarg"
        )

    def run_test(self):
        if self.options.rpc_timeout is None and self.options.extra_args is None:
            self.test_instant_rpc_timeout()
            self.test_wrong_rpc_port()
            self.test_init_error()


if __name__ == '__main__':
    FeatureFrameworkRPCFailure(__file__).main()
