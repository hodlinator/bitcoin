// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/string.h>

#include <boost/test/unit_test.hpp>

using namespace util;

BOOST_AUTO_TEST_SUITE(util_string_tests)

template <typename... Tt>
void TfmF(const char* fmt, const std::tuple<Tt...>& t)
{
    std::apply([fmt](const Tt&... ta){
        tfm::format(fmt, ta...);
    }, t);
}

// Helper to allow compile-time sanity checks while providing the number of
// args directly. Normally PassFmt<sizeof...(Args)> would be used.
template <typename... Args>
inline void PassFmt(ConstevalFormatString<sizeof...(Args)> fmt, const Args&... args)
{
    ConstevalFormatString<sizeof...(Args)>::Detail_CheckNumFormatSpecifiers(fmt.fmt);

    // Prove parity with tinyformat
    tfm::format(fmt.fmt, args...);
    if constexpr (sizeof...(Args) > 0) {
        BOOST_CHECK_THROW(TfmF(fmt.fmt, std::tuple_cat(std::array<int, sizeof...(Args) - 1>{})), tfm::format_error);
    }
}
template <unsigned WrongNumArgs, unsigned CorrectArgs>
inline void PassFmtIncorrect(ConstevalFormatString<WrongNumArgs> fmt)
{
    // Disprove parity with tinyformat
    static_assert(WrongNumArgs != CorrectArgs);
    TfmF(fmt.fmt, std::tuple_cat(std::array<int, CorrectArgs>{}));
    BOOST_CHECK_THROW(TfmF(fmt.fmt, std::tuple_cat(std::array<int, WrongNumArgs>{})), tfm::format_error);
}
template <unsigned WrongNumArgs>
inline void FailFmtWithError(std::string_view wrong_fmt, std::string_view error)
{
    using ErrType = const char*;
    auto check_throw{[error](const ErrType& str) { return str == error; }};
    BOOST_CHECK_EXCEPTION(ConstevalFormatString<WrongNumArgs>::Detail_CheckNumFormatSpecifiers(wrong_fmt), ErrType, check_throw);
}

BOOST_AUTO_TEST_CASE(ConstevalFormatString_NumSpec)
{
    PassFmt("");
    PassFmt("%%");
    PassFmt("%s", "foo");
    PassFmt("%%s");
    PassFmt("s%%");
    PassFmt("%%%s", "foo");
    PassFmt("%s%%", "foo");
    PassFmt(" 1$s");
    PassFmt("%1$s", "foo");
    PassFmt("%1$s%1$s", "foo");
    PassFmt("%2$s", "foo", "bar");
    PassFmt("%2$s 4$s %2$s", "foo", "bar");
    PassFmt("%12$s 999$s %2$s", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12");
    PassFmt("%02d", 1);
    PassFmt("%+2s", "foo");
    PassFmt("%.6i", 1);
    PassFmt("%5.2f", 0.4f);
    PassFmt("%#x", 1);
    PassFmt("%1$5i", 1);
    PassFmt("%1$-5i", 1);
    PassFmt("%1$.5i", 12);
    // tinyformat accepts almost any "type" spec, even '%', or '_', or '\n'.
    PassFmt("%123%", 1);
    PassFmt("%123%s", 1);
    PassFmt("%_", 1);
    PassFmt("%\n", 1);

    // The `*` specifier behavior is unsupported and can lead to runtime
    // errors when used in a ConstevalFormatString. Please refer to the
    // note in the ConstevalFormatString docs.
    PassFmtIncorrect<1, 2>("%*c");
    PassFmtIncorrect<2, 3>("%2$*3$d");
    PassFmtIncorrect<1, 2>("%.*f");

    auto err_mix{"Format specifiers must be all positional or all non-positional!"};
    FailFmtWithError<1>("%s%1$s", err_mix);

    auto err_num{"Format specifier count must match the argument count!"};
    FailFmtWithError<1>("", err_num);
    FailFmtWithError<0>("%s", err_num);
    FailFmtWithError<2>("%s", err_num);
    FailFmtWithError<0>("%1$s", err_num);
    FailFmtWithError<2>("%1$s", err_num);

    auto err_0_pos{"Positional format specifier must have position of at least 1"};
    FailFmtWithError<1>("%$s", err_0_pos);
    FailFmtWithError<1>("%$", err_0_pos);
    FailFmtWithError<0>("%0$", err_0_pos);
    FailFmtWithError<0>("%0$s", err_0_pos);

    auto err_term{"Format specifier incorrectly terminated by end of string"};
    FailFmtWithError<1>("%", err_term);
    FailFmtWithError<1>("%1$", err_term);
}

BOOST_AUTO_TEST_SUITE_END()
