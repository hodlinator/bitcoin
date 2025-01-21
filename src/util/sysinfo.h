// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SYSINFO_H
#define BITCOIN_SYSINFO_H

#include <cstddef>
#include <optional>

struct RAMInfo {
	size_t total{0};
	size_t free{0};
};

std::optional<RAMInfo> QueryRAMInfo();

#endif // BITCOIN_SYSINFO_H
