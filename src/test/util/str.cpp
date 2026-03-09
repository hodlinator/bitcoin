// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/str.h>

std::span<const std::byte> StringToBytes(std::string_view str)
{
    return std::as_bytes(std::span{str});
}
