#include "XP3ArchiveHashDecoder.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {
constexpr auto MakeAddKey() {
    std::array<std::uint8_t, 61> key{};
    std::uint64_t seed = 0x1e9029ecc988302eULL;
    for(auto &byte : key) {
        byte = static_cast<std::uint8_t>(seed);
        seed = ((seed >> 8) | (seed << 53)) & ((1ULL << 61) - 1);
    }
    return key;
}
constexpr auto AddKey = MakeAddKey();

bool IsMediaHeader(const std::uint8_t *data, std::size_t size) {
    if(size >= 11 && std::memcmp(data, "TLG", 3) == 0 &&
       std::memcmp(data + 4, ".0\0", 3) == 0) {
        return (data[3] == '0' && std::memcmp(data + 7, "sds\x1a", 4) == 0) ||
               ((data[3] == '5' || data[3] == '6') &&
                std::memcmp(data + 7, "raw\x1a", 4) == 0);
    }
    // Require a complete Vorbis identification signature in the first Ogg
    // page, not just the four-byte capture pattern.
    if(size < 35 || std::memcmp(data, "OggS\0\x02", 6) != 0 || data[26] == 0)
        return false;
    const std::size_t packet = 27 + data[26];
    return packet + 7 <= size &&
           std::memcmp(data + packet, "\x01vorbis", 7) == 0;
}
} // namespace

void TVPDecodeXP3Hash(std::uint32_t hash, std::uint64_t offset, void *buffer,
                     std::size_t size) {
    std::array<std::uint8_t, 31> xorKey{};
    hash &= 0x7fffffff;
    for(auto &byte : xorKey) {
        byte = static_cast<std::uint8_t>(hash);
        hash = (hash << 23) | (hash >> 8);
    }
    auto *bytes = static_cast<std::uint8_t *>(buffer);
    std::size_t xorIndex = offset % xorKey.size();
    std::size_t addIndex = offset % AddKey.size();
    for(std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>((bytes[i] ^ xorKey[xorIndex]) +
                                           AddKey[addIndex]);
        if(++xorIndex == xorKey.size()) xorIndex = 0;
        if(++addIndex == AddKey.size()) addIndex = 0;
    }
}

bool TVPIsXP3HashDecoderCandidate(std::uint32_t hash, const void *header,
                                std::size_t size) {
    std::array<std::uint8_t, 64> decoded{};
    size = std::min(size, decoded.size());
    if(!header || IsMediaHeader(static_cast<const std::uint8_t *>(header), size))
        return false;
    std::memcpy(decoded.data(), header, size);
    TVPDecodeXP3Hash(hash, 0, decoded.data(), size);
    return IsMediaHeader(decoded.data(), size);
}
