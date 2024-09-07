// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <streams.h>
#include <util/fs.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

static void FindByte(benchmark::Bench& bench)
{
    // Setup
    const size_t file_size = 200;
    {
        FileWriter file{fsbridge::fopen("streams_tmp", "w+b"), [](int err) { Assert(false); }};
        uint8_t data[file_size] = {0};
        data[file_size-1] = 1;
        file << data;
    }
    FileReader file{fsbridge::fopen("streams_tmp", "r+b")};
    BufferedFile bf{file, /*nBufSize=*/file_size + 1, /*nRewindIn=*/file_size};

    bench.run([&] {
        bf.SetPos(0);
        bf.FindByte(std::byte(1));
    });

    // Cleanup
    Assert(file.fclose() == 0);
    fs::remove("streams_tmp");
}

BENCHMARK(FindByte, benchmark::PriorityLevel::HIGH);
