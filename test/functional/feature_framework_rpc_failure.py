#!/usr/bin/env python3
from test_framework.test_framework import BitcoinTestFramework

class FeatureFrameworkRPCFailure(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.rpc_timeout = 0 # Provoke timeout exception

    def run_test(self):
        pass

if __name__ == '__main__':
    FeatureFrameworkRPCFailure(__file__).main()
