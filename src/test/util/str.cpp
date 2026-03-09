// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/str.h>

std::vector<std::byte> StringToBuffer(std::string_view str)
{
    auto span = std::as_bytes(std::span{str});
    return {span.begin(), span.end()};
}

std::span<const std::byte> StringToBytes(std::string_view str)
{
    return std::as_bytes(std::span{str});
}
