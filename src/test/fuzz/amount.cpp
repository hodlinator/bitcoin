// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>

#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/amount.h>

#include <cassert>
#include <cstdint>
#include <limits>

using InnerType = Amount::inner_type;
static_assert(std::is_same_v<InnerType, int64_t>);

template <typename T>
void TestIntegerOperations(FuzzedDataProvider& provider)
{
    const auto i{provider.ConsumeIntegral<InnerType>()};
    const Amount sats{Amount::From(i)};
    assert(sats.Int() == i);
    const T other{provider.ConsumeIntegral<T>()};

#ifdef __SIZEOF_INT128__
    if constexpr (requires { Amount{sats % other}; } ||
                  requires { Amount{sats / other}; }) {
        if (other != 0 && other <= std::numeric_limits<InnerType>::max()) {
            if (const auto quotient_128{__int128{i} / __int128{other}};
                quotient_128 >= std::numeric_limits<InnerType>::min() &&
                quotient_128 <= std::numeric_limits<InnerType>::max()) {
                assert(Amount{sats % other}.Int() == InnerType(i % other));
                assert(Amount{sats / other}.Int() == InnerType(i / other));
            }
        }
    }

    Amount other_sats{sats};
    InnerType other_i{i};
    if constexpr (requires { sats * other; } ||
                  requires { other * sats; } ||
                  requires { other_sats *= other; }) {
        if (const auto product_128{__int128{i} * __int128{other}};
            product_128 >= std::numeric_limits<InnerType>::min() &&
            product_128 <= std::numeric_limits<InnerType>::max()) {
            assert(Amount{sats * other}.Int() == InnerType(i * other));
            assert(Amount{other * sats}.Int() == InnerType(other * i));

            // Tests for test/util/amount.h
            other_sats *= other;
            other_i *= other;
            assert(other_sats.Int() == other_i);
        }
    }
#endif

    if constexpr (requires { sats << other; }) {
        if (other >= 0 && other < 64) {
            assert((sats << other).Int() == InnerType(i << other));
        }
    }
}

FUZZ_TARGET(amount)
{
    FuzzedDataProvider provider(buffer.data(), buffer.size());

    const auto i{provider.ConsumeIntegral<InnerType>()};
    const Amount sats{Amount::From(i)};
    assert(sats.Int() == i);

    const auto other_i{provider.ConsumeIntegral<InnerType>()};
    const Amount other_sats{Amount::From(other_i)};
    assert(other_sats.Int() == other_i);

    assert((sats < other_sats) == (i < other_i));
    assert((sats > other_sats) == (i > other_i));
    assert((sats < other_sats) == (i < other_i));

    if (i != std::numeric_limits<InnerType>::min()) { // Sanitizer would prevent negation.
        assert(-sats.Int() == -i);
    }

#ifdef __SIZEOF_INT128__
    const auto sum_128{__int128{i} + __int128{other_i}};
    if (sum_128 >= std::numeric_limits<InnerType>::min() &&
        sum_128 <= std::numeric_limits<InnerType>::max()) {
        assert((sats + other_sats).Int() == i + other_i);
    }
    if (const auto diff_128{__int128{i} - __int128{other_i}};
        diff_128 >= std::numeric_limits<InnerType>::min() &&
        diff_128 <= std::numeric_limits<InnerType>::max()) {
        assert((sats - other_sats).Int() == i - other_i);
    }
#endif

    // Guard against division by zero and division of -9223372036854775808 by -1
    // which is 1 too big to be represented in int64_t.
    if (other_i != 0 && !(i == std::numeric_limits<InnerType>::min() && other_i == -1)) {
        assert(sats / other_sats == i / other_i);
        assert(sats % other_sats == i % other_i);
    }

    auto new_i{other_i};
    Amount new_sats{sats};
    assert(new_sats == sats); // copy-construction
    new_sats = other_sats;
    assert(new_sats == other_sats); // assignment

#ifdef __SIZEOF_INT128__
    // operator +=
    if (sum_128 >= std::numeric_limits<InnerType>::min() &&
        sum_128 <= std::numeric_limits<InnerType>::max()) {
        new_sats += sats;
        new_i += i;
        assert(new_sats.Int() == new_i);
    }
    // operator -=
    const auto sum_128_diff{new_i - i};
    if (sum_128_diff >= std::numeric_limits<InnerType>::min() &&
        sum_128_diff <= std::numeric_limits<InnerType>::max()) {
        new_sats -= sats;
        new_i -= i;
        assert(new_sats.Int() == new_i);
    }
#endif

    auto shift_amount{provider.ConsumeIntegralInRange(0, 63)};
    if (std::optional<UAmount> unew_sats{new_sats.TryToUnsigned()})
    {
        *unew_sats >>= shift_amount;
        new_i >>= shift_amount;
        assert(unew_sats->UInt() == static_cast<UAmount::inner_type>(new_i)); // operator >>=
    }

    TestIntegerOperations<int8_t>(provider);
    TestIntegerOperations<uint8_t>(provider);
    TestIntegerOperations<int16_t>(provider);
    TestIntegerOperations<uint16_t>(provider);
    TestIntegerOperations<int32_t>(provider);
    TestIntegerOperations<uint32_t>(provider);
    TestIntegerOperations<int64_t>(provider);
    TestIntegerOperations<uint64_t>(provider);
}
