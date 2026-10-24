// A small zlib/deflate decoder (RFC 1950/1951), enough for Skyrim's compressed plugin records: the
// radar map generator reads Skyrim.esm's landscape, much of which is stored compressed. Header-only and
// game-free, so the protocol tests cover it, and no zlib to add to either build.
//
// Huffman codes decode through one lookup table per code (indexed by the next maxLength bits, read
// least significant first as deflate packs them), which keeps it simple and fast enough for the
// hundred-odd megabytes the generator inflates.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace AhShitRim::Inflate
{
    namespace detail
    {
        struct Bits
        {
            const std::uint8_t* at;
            const std::uint8_t* end;
            std::uint64_t       buffer = 0;
            int                 count = 0;

            void Fill()
            {
                while (count <= 56 && at < end) {
                    buffer |= static_cast<std::uint64_t>(*at++) << count;
                    count += 8;
                }
            }

            // Past the end it reads zeros; Drop then fails, so a decode that needed them does too.
            std::uint32_t Peek(int a_bits)
            {
                if (count < a_bits) {
                    Fill();
                }
                return static_cast<std::uint32_t>(buffer & ((1ull << a_bits) - 1));
            }

            bool Drop(int a_bits)
            {
                if (count < a_bits) {
                    Fill();
                    if (count < a_bits) {
                        return false;
                    }
                }
                buffer >>= a_bits;
                count -= a_bits;
                return true;
            }

            std::optional<std::uint32_t> Take(int a_bits)
            {
                if (a_bits == 0) {
                    return 0u;
                }
                const auto value = Peek(a_bits);
                if (!Drop(a_bits)) {
                    return std::nullopt;
                }
                return value;
            }

            void AlignToByte() { Drop(count % 8); }
        };

        // One canonical Huffman code as a table: entry = symbol << 4 | length, 0 for an unused pattern.
        struct Code
        {
            std::vector<std::uint16_t> table;
            int                        maxLength = 0;

            bool Build(const std::uint8_t* a_lengths, int a_count)
            {
                std::array<int, 16> perLength{};
                maxLength = 0;
                for (int i = 0; i < a_count; ++i) {
                    ++perLength[a_lengths[i]];
                    if (a_lengths[i] > maxLength) {
                        maxLength = a_lengths[i];
                    }
                }
                if (maxLength == 0) {
                    table.assign(1, 0);
                    return true;  // an empty code (a distance code for literal-only data)
                }
                perLength[0] = 0;
                std::array<int, 16> next{};
                int                 code = 0;
                for (int length = 1; length <= 15; ++length) {
                    code = (code + perLength[length - 1]) << 1;
                    next[length] = code;
                    if (code + perLength[length] > (1 << length)) {
                        return false;  // oversubscribed
                    }
                }
                table.assign(std::size_t{ 1 } << maxLength, 0);
                for (int symbol = 0; symbol < a_count; ++symbol) {
                    const int length = a_lengths[symbol];
                    if (length == 0) {
                        continue;
                    }
                    // Deflate sends codes most significant bit first; the bit reader is LSB first.
                    int c = next[length]++;
                    int reversed = 0;
                    for (int i = 0; i < length; ++i) {
                        reversed = (reversed << 1) | (c & 1);
                        c >>= 1;
                    }
                    for (int at = reversed; at < (1 << maxLength); at += 1 << length) {
                        table[static_cast<std::size_t>(at)] = static_cast<std::uint16_t>(symbol << 4 | length);
                    }
                }
                return true;
            }

            int Decode(Bits& a_bits) const
            {
                const auto entry = table[a_bits.Peek(maxLength)];
                const int  length = entry & 15;
                if (length == 0 || !a_bits.Drop(length)) {
                    return -1;
                }
                return entry >> 4;
            }
        };

        inline constexpr std::array<std::uint16_t, 29> kLengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        inline constexpr std::array<std::uint8_t, 29>  kLengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        inline constexpr std::array<std::uint16_t, 30> kDistanceBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        inline constexpr std::array<std::uint8_t, 30>  kDistanceExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

        inline bool Block(Bits& a_bits, const Code& a_literals, const Code& a_distances, std::vector<std::uint8_t>& a_out)
        {
            for (;;) {
                const int symbol = a_literals.Decode(a_bits);
                if (symbol < 0) {
                    return false;
                }
                if (symbol < 256) {
                    a_out.push_back(static_cast<std::uint8_t>(symbol));
                    continue;
                }
                if (symbol == 256) {
                    return true;
                }
                const int lengthIndex = symbol - 257;
                if (lengthIndex >= 29) {
                    return false;
                }
                const auto lengthExtra = a_bits.Take(kLengthExtra[lengthIndex]);
                const int  distanceIndex = a_distances.Decode(a_bits);
                if (!lengthExtra || distanceIndex < 0 || distanceIndex >= 30) {
                    return false;
                }
                const auto distanceExtra = a_bits.Take(kDistanceExtra[distanceIndex]);
                if (!distanceExtra) {
                    return false;
                }
                const std::size_t length = kLengthBase[lengthIndex] + *lengthExtra;
                const std::size_t distance = kDistanceBase[distanceIndex] + *distanceExtra;
                if (distance > a_out.size()) {
                    return false;
                }
                std::size_t from = a_out.size() - distance;
                for (std::size_t i = 0; i < length; ++i) {
                    a_out.push_back(a_out[from + i]);  // overlapping copies repeat, as deflate means
                }
            }
        }

        inline bool FixedCodes(Code& a_literals, Code& a_distances)
        {
            std::array<std::uint8_t, 288> lengths{};
            for (int i = 0; i < 144; ++i) lengths[i] = 8;
            for (int i = 144; i < 256; ++i) lengths[i] = 9;
            for (int i = 256; i < 280; ++i) lengths[i] = 7;
            for (int i = 280; i < 288; ++i) lengths[i] = 8;
            std::array<std::uint8_t, 30> distances{};
            distances.fill(5);
            return a_literals.Build(lengths.data(), 288) && a_distances.Build(distances.data(), 30);
        }

        inline bool DynamicCodes(Bits& a_bits, Code& a_literals, Code& a_distances)
        {
            const auto hlit = a_bits.Take(5), hdist = a_bits.Take(5), hclen = a_bits.Take(4);
            if (!hlit || !hdist || !hclen) {
                return false;
            }
            const int literalCount = static_cast<int>(*hlit) + 257;
            const int distanceCount = static_cast<int>(*hdist) + 1;
            if (literalCount > 286 || distanceCount > 30) {
                return false;
            }
            static constexpr std::array<int, 19> kOrder{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            std::array<std::uint8_t, 19>         codeLengths{};
            for (std::uint32_t i = 0; i < *hclen + 4; ++i) {
                const auto length = a_bits.Take(3);
                if (!length) {
                    return false;
                }
                codeLengths[kOrder[i]] = static_cast<std::uint8_t>(*length);
            }
            Code lengthCode;
            if (!lengthCode.Build(codeLengths.data(), 19)) {
                return false;
            }
            std::array<std::uint8_t, 316> lengths{};
            int                           at = 0;
            while (at < literalCount + distanceCount) {
                const int symbol = lengthCode.Decode(a_bits);
                if (symbol < 0) {
                    return false;
                }
                if (symbol < 16) {
                    lengths[at++] = static_cast<std::uint8_t>(symbol);
                    continue;
                }
                std::uint8_t value = 0;
                int          repeat = 0;
                if (symbol == 16) {
                    if (at == 0) {
                        return false;
                    }
                    value = lengths[at - 1];
                    const auto extra = a_bits.Take(2);
                    if (!extra) return false;
                    repeat = 3 + static_cast<int>(*extra);
                } else if (symbol == 17) {
                    const auto extra = a_bits.Take(3);
                    if (!extra) return false;
                    repeat = 3 + static_cast<int>(*extra);
                } else {
                    const auto extra = a_bits.Take(7);
                    if (!extra) return false;
                    repeat = 11 + static_cast<int>(*extra);
                }
                if (at + repeat > literalCount + distanceCount) {
                    return false;
                }
                while (repeat-- > 0) {
                    lengths[at++] = value;
                }
            }
            if (lengths[256] == 0) {
                return false;  // no end-of-block code
            }
            return a_literals.Build(lengths.data(), literalCount) && a_distances.Build(lengths.data() + literalCount, distanceCount);
        }
    }

    // A raw deflate stream. a_sizeHint reserves the output up front (Skyrim stores each record's size).
    inline std::optional<std::vector<std::uint8_t>> Raw(const std::uint8_t* a_data, std::size_t a_size, std::size_t a_sizeHint = 0)
    {
        std::vector<std::uint8_t> out;
        out.reserve(a_sizeHint);
        detail::Bits bits{ a_data, a_data + a_size };
        for (;;) {
            const auto final = bits.Take(1);
            const auto type = bits.Take(2);
            if (!final || !type) {
                return std::nullopt;
            }
            if (*type == 0) {
                bits.AlignToByte();
                const auto length = bits.Take(16), inverse = bits.Take(16);
                if (!length || !inverse || (*length ^ 0xFFFF) != *inverse) {
                    return std::nullopt;
                }
                for (std::uint32_t i = 0; i < *length; ++i) {
                    const auto byte = bits.Take(8);
                    if (!byte) {
                        return std::nullopt;
                    }
                    out.push_back(static_cast<std::uint8_t>(*byte));
                }
            } else if (*type == 1 || *type == 2) {
                detail::Code literals, distances;
                const bool   built = *type == 1 ? detail::FixedCodes(literals, distances) : detail::DynamicCodes(bits, literals, distances);
                if (!built || !detail::Block(bits, literals, distances, out)) {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
            if (*final) {
                return out;
            }
        }
    }

    // A zlib stream: the two-byte header (deflate, no preset dictionary), then raw deflate. The
    // trailing Adler-32 isn't checked; a damaged record fails to decode or comes out the wrong size.
    inline std::optional<std::vector<std::uint8_t>> Zlib(const std::uint8_t* a_data, std::size_t a_size, std::size_t a_sizeHint = 0)
    {
        if (a_size < 2 || (a_data[0] & 0x0F) != 8 || ((a_data[0] << 8) | a_data[1]) % 31 != 0 || (a_data[1] & 0x20) != 0) {
            return std::nullopt;
        }
        return Raw(a_data + 2, a_size - 2, a_sizeHint);
    }
}
