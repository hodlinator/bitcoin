// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/string.h>

#include <iterator>
#include <regex>
#include <string>

namespace util {
void ReplaceAll(std::string& in_out, const std::string& search, const std::string& substitute)
{
    if (search.empty()) return;
    in_out = std::regex_replace(in_out, std::regex(search), substitute);
}

LineReader::LineReader(std::string_view str, size_t max_line_length)
    : m_str{str}, m_max_line_length{max_line_length}, m_it{str.begin()} {}

util::Expected<std::string_view, LineReader::Error> LineReader::ReadLine() noexcept
{
    if (m_it == m_str.end()) {
        return util::Unexpected{Error::EndOfBuffer};
    }

    const auto line_start = m_it;
    while (m_it != m_str.end()) {
        // Read a character from the incoming buffer and increment the iterator
        const bool new_line{*m_it == '\n'};
        ++m_it;
        // If the character we just consumed was \n, the line is terminated.
        // The \n itself does not count against max_line_length.
        if (new_line) {
            const std::string_view untrimmed_line{line_start, m_it};
            return TrimStringView(untrimmed_line); // delete leading and trailing whitespace including \r and \n
        }
        // If the character we just consumed gives us a line length greater
        // than max_line_length, and we are not at the end of the line (or buffer) yet,
        // that means the line we are currently reading is too long, and we throw.
        if (std::distance(line_start, m_it) > static_cast<int64_t>(m_max_line_length)) {
            // Reset iterator
            m_it = line_start;
            return util::Unexpected{Error::LineLengthExceeded};
        }
    }
    // End of buffer reached without finding a \n or exceeding max_line_length.
    // Reset the iterator so the rest of the buffer can be read granularly
    // with ReadLength() and return null to indicate a line was not found.
    m_it = line_start;
    return util::Unexpected{Error::EndOfBuffer};
}

// Ignores max_line_length but won't overflow
util::Expected<std::string_view, LineReader::Error> LineReader::ReadLength(size_t len) noexcept
{
    if (len == 0) return {};
    if (Remaining() < len) return util::Unexpected{Error::EndOfBuffer};
    std::string_view out{m_it, m_it + len};
    m_it += len;
    return out;
}

size_t LineReader::Remaining() const
{
    return std::distance(m_it, m_str.end());
}

size_t LineReader::Consumed() const
{
    return std::distance(m_str.begin(), m_it);
}
} // namespace util
