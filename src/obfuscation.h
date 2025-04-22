// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_OBFUSCATION_H
#define BITCOIN_OBFUSCATION_H

#include <serialize.h>
#include <span.h>

#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstdint>

class Obfuscation
{
public:
    static constexpr size_t SIZE_BYTES{sizeof(uint64_t)};

private:
    // Cached key rotations for different offsets.
    std::array<uint64_t, SIZE_BYTES> m_rotations;

    void SetRotations(const uint64_t key)
    {
        for (size_t i{0}; i < SIZE_BYTES; ++i) {
            size_t key_rotation_bits{CHAR_BIT * i};
            if constexpr (std::endian::native == std::endian::big) key_rotation_bits *= -1;
            m_rotations[i] = std::rotr(key, key_rotation_bits);
        }
    }

    static uint64_t ToUint64(const std::span<const std::byte, SIZE_BYTES> key_span)
    {
        uint64_t key{};
        std::memcpy(&key, key_span.data(), SIZE_BYTES);
        return key;
    }

    static void Xor(std::span<std::byte> write, const uint64_t key, const size_t size)
    {
        assert(size <= write.size());
        uint64_t raw{};
        std::memcpy(&raw, write.data(), size);
        raw ^= key;
        std::memcpy(write.data(), &raw, size);
    }

public:
    Obfuscation(const uint64_t key) { SetRotations(key); }
    Obfuscation(std::span<const std::byte, SIZE_BYTES> key_span) : Obfuscation(ToUint64(key_span)) {}
    Obfuscation(std::span<const uint8_t, SIZE_BYTES> key_span) : Obfuscation(MakeByteSpan(key_span).first<SIZE_BYTES>()) {}

    uint64_t Key() const { return m_rotations[0]; }
    operator bool() const { return Key() != 0; }
    void operator()(std::span<std::byte> target, const size_t key_offset_bytes = 0) const
    {
        if (!*this) return;
        const uint64_t rot_key{m_rotations[key_offset_bytes % SIZE_BYTES]}; // Continue obfuscation from where we left off
        for (; target.size() >= SIZE_BYTES; target = target.subspan(SIZE_BYTES)) { // Process multiple bytes at a time
            Xor(target, rot_key, SIZE_BYTES);
        }
        Xor(target, rot_key, target.size());
    }

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        std::array<std::byte, SIZE_BYTES> bytes;
        std::memcpy(bytes.data(), &m_rotations[0], SIZE_BYTES); // MISMATCH IN size, byte vs i64
        s << bytes;
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        std::array<std::byte, SIZE_BYTES> bytes;
        s >> bytes;
        SetRotations(ToUint64(bytes));
    }
};

#endif // BITCOIN_OBFUSCATION_H
