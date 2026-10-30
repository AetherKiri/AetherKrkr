#include <catch2/catch_test_macros.hpp>

#include "UtilStreams.h"
#include "XP3Archive.h"
#include "XP3ArchiveHashDecoder.h"
#include "Crypt/hxv4/Hxv4Decoder.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <zlib.h>

namespace {
using Bytes = std::vector<tjs_uint8>;
// Authored media header and ascending bytes, encoded independently of the
// production decoder. The hash is the Adler-32 of the complete clear payload.
constexpr tjs_uint32 Hash = 0xd392229a;
const Bytes Encoded = {
    0xbc, 0x3e, 0x2d, 0x34, 0x0f, 0x16, 0xb9, 0x3c, 0xf9, 0xe5, 0x0d, 0x33,
    0x97, 0xb9, 0x43, 0xe3, 0x6c, 0xc4, 0xd8, 0x80, 0x76, 0x5b, 0x0d, 0x9f,
    0x7f, 0x02, 0xe5, 0x2f, 0xb8, 0xee, 0x83, 0x88, 0xb0, 0xec, 0x18, 0x37,
    0x06, 0xf8, 0x17, 0x1a, 0x85, 0x6f, 0x12, 0x9e, 0x45, 0x17, 0xb2, 0x31,
    0xe1, 0xcd, 0x79, 0x0a, 0x14, 0x2f, 0xc0, 0x61, 0xa9, 0x86, 0xd5, 0xea,
    0x19, 0xa3, 0x99, 0x8e, 0xfe, 0x19, 0x43, 0xb9, 0x92, 0x9c, 0x93, 0x83,
    0x40, 0x55, 0x69, 0x2a, 0xf4, 0x28, 0x0a, 0x3c, 0xab, 0x4a, 0x13, 0x5f,
    0xae, 0x58, 0x0d, 0xa0, 0x19, 0x0f, 0x42, 0x45, 0xe8, 0x55, 0x99, 0x1a,
    0xe4, 0x19, 0x7f, 0x32, 0x50, 0xec, 0x40, 0x67, 0x5e, 0x6d, 0xe6, 0x27,
    0xff, 0x29, 0x13, 0xd0, 0x8e, 0xd6, 0x61, 0x7b, 0x42, 0x94, 0x4d, 0x53,
    0xd9, 0x3f, 0x65, 0xe7, 0x73, 0x8b, 0x15, 0x18, 0xa8, 0x89, 0xbb, 0xdb,
    0xee, 0x69, 0xda, 0x23, 0xf8, 0x87, 0xcd,
};
Bytes Plain() {
    Bytes bytes{'T', 'L', 'G', '0', '.', '0', 0, 's', 'd', 's', 0x1a};
    for(unsigned i = 0; i < 128; ++i) bytes.push_back(i);
    return bytes;
}

class Fixture {
public:
    tTVPXP3Archive *archive = new tTVPXP3Archive(TJS_W(""), 0);
    std::unique_ptr<tTVPXP3ArchiveStream> stream;
    Fixture(const Bytes &input, bool compressed, bool split, tjs_uint32 hash = Hash) {
        // These in-memory archives share a synthetic name; do not let one
        // section's compressed segments stand in for another fixture.
        TVPClearXP3SegmentCache();
        archive->ItemVector.resize(1);
        auto &item = archive->ItemVector[0];
        item.Name = TJS_W("fixture.tlg");
        item.FileHash = hash;
        item.Flags = 0; // This format does not require FILE_PROTECTED.
        item.OrgSize = input.size();
        Bytes stored;
        const std::size_t boundary = split ? 17 : input.size();
        for(std::size_t offset = 0; offset < input.size();) {
            const auto size = std::min(boundary, input.size() - offset);
            Bytes part(input.begin() + offset, input.begin() + offset + size);
            if(compressed) {
                Bytes packed(compressBound(part.size()));
                uLongf length = packed.size();
                REQUIRE(compress(packed.data(), &length, part.data(), part.size()) == Z_OK);
                packed.resize(length);
                part = std::move(packed);
            }
            item.Segments.push_back({stored.size(), offset, size, part.size(), compressed});
            stored.insert(stored.end(), part.begin(), part.end());
            offset += size;
        }
        auto memory = std::make_unique<tTVPMemoryStream>();
        memory->WriteBuffer(stored.data(), stored.size());
        memory->SetPosition(0);
        stream = std::make_unique<tTVPXP3ArchiveStream>(
            archive, 0, &item.Segments, memory.release(), input.size());
    }
    ~Fixture() {
        stream.reset();
        archive->Release();
    }
};
} // namespace

TEST_CASE("XP3 hash media decoding preserves file offsets", "[storage][xp3]") {
    const auto plain = Plain();
    REQUIRE(TVPIsXP3HashDecoderCandidate(Hash, Encoded.data(), Encoded.size()));
    CHECK_FALSE(TVPIsXP3HashDecoderCandidate(Hash, plain.data(), plain.size()));
    CHECK_FALSE(TVPIsXP3HashDecoderCandidate(Hash, Encoded.data(), 3));
    CHECK_FALSE(TVPIsXP3HashDecoderCandidate(Hash ^ 0x8000, Encoded.data(), Encoded.size()));
    auto decoded = Encoded;
    TVPDecodeXP3Hash(Hash, 0, decoded.data(), decoded.size());
    CHECK(decoded == plain);
    for(std::size_t offset : {1u, 30u, 31u, 60u, 61u, 97u}) {
        Bytes part(Encoded.begin() + offset, Encoded.end());
        TVPDecodeXP3Hash(Hash, offset, part.data(), part.size());
        CHECK(part == Bytes(plain.begin() + offset, plain.end()));
    }
    Bytes part(Encoded.begin() + 61, Encoded.end());
    TVPDecodeXP3Hash(Hash, 31ULL * 61 * (1ULL << 30) + 61, part.data(), part.size());
    CHECK(part == Bytes(plain.begin() + 61, plain.end()));
}

TEST_CASE("XP3 fallback recognizes encrypted Vorbis identification pages", "[storage][xp3]") {
    const Bytes encoded = {
        0x09, 0x33, 0xe4, 0x90, 0x00, 0x5b, 0x6d, 0x2f, 0xf1, 0x3b, 0x41, 0x61,
        0x46, 0x9b, 0x69, 0xc6, 0x02, 0x50, 0x79, 0x0c, 0x78, 0x18, 0x57, 0x49,
        0xf8, 0xd0, 0xf5, 0x78, 0xe5, 0x58, 0xb4, 0x58, 0xdb, 0xea, 0xbd, 0x76,
        0x7c, 0x0a, 0xae, 0xaa, 0x31, 0xe3, 0x21, 0x28, 0x80, 0x04, 0x76, 0xb6,
        0x2e, 0x6d, 0xc4, 0xd7, 0x84, 0x58, 0xdf, 0xaf, 0x58, 0xf2,
    };
    Bytes plain{'O', 'g', 'g', 'S', 0, 2};
    plain.resize(26);
    plain.insert(plain.end(), {1, 30, 1, 'v', 'o', 'r', 'b', 'i', 's'});
    plain.resize(58);
    Fixture fixture(encoded, true, true, 0x9a3b0428);
    Bytes actual(plain.size());
    fixture.stream->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == plain);
}

TEST_CASE("XP3 media fallback verifies checksum and seeks across segments", "[storage][xp3]") {
    bool compressed = false, split = false;
    SECTION("raw") {}
    SECTION("zlib") { compressed = true; }
    SECTION("raw segments") { split = true; }
    SECTION("zlib segments") { compressed = split = true; }
    Fixture fixture(Encoded, compressed, split);
    // First read need not start at zero. Probing must restore its position.
    fixture.stream->SetPosition(61);
    std::array<tjs_uint8, 31> part{};
    REQUIRE(fixture.stream->Read(part.data(), part.size()) == part.size());
    const auto plain = Plain();
    CHECK(std::equal(part.begin(), part.end(), plain.begin() + 61));
    fixture.stream->SetPosition(0);
    Bytes all(plain.size());
    fixture.stream->ReadBuffer(all.data(), all.size());
    CHECK(all == plain);
}

TEST_CASE("XP3 fallback leaves plain and invalid-checksum entries alone", "[storage][xp3]") {
    Bytes stored;
    SECTION("plain") { stored = Plain(); }
    SECTION("damaged encrypted payload") { stored = Encoded; stored.back() ^= 1; }
    Fixture fixture(stored, false, false);
    Bytes actual(stored.size());
    fixture.stream->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == stored);
}

TEST_CASE("explicit XP3 extraction filter takes priority over media fallback", "[storage][xp3]") {
    struct FilterGuard {
        tTVPXP3ArchiveExtractionFilter previous = TVPXP3ArchiveExtractionFilter;
        ~FilterGuard() { TVPSetXP3ArchiveExtractionFilter(previous); }
    } guard;
    TVPSetXP3ArchiveExtractionFilter([](tTVPXP3ExtractionFilterInfo *info, tTJSVariant *) {
        auto *bytes = static_cast<tjs_uint8 *>(info->Buffer);
        for(tjs_uint i = 0; i < info->BufferSize; ++i) bytes[i] ^= 0x5a;
    });
    Fixture fixture(Encoded, true, true);
    Bytes actual(Encoded.size()), expected = Encoded;
    for(auto &byte : expected) byte ^= 0x5a;
    fixture.stream->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == expected);
}

TEST_CASE("idle Hxv4 provider does not mask built-in XP3 decoding", "[storage][xp3]") {
    using AetherKiri::Hxv4::Hxv4DecoderPlugin;
    const auto previous = TVPXP3ArchiveExtractionFilter;
    Hxv4DecoderPlugin::Initialize();
    struct Cleanup {
        ~Cleanup() { Hxv4DecoderPlugin::Teardown(); }
    } cleanup;
    REQUIRE(TVPXP3ArchiveExtractionFilter == previous);
    Fixture fixture(Encoded, false, false);
    Bytes actual(Encoded.size());
    fixture.stream->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == Plain());
}

TEST_CASE("Hxv4 installs its filter when a matching chunk is found", "[storage][xp3]") {
    using AetherKiri::Hxv4::Hxv4DecoderPlugin;
    struct Cleanup {
        tTVPXP3ArchiveExtractionFilter previous = TVPXP3ArchiveExtractionFilter;
        ~Cleanup() {
            Hxv4DecoderPlugin::Teardown();
            TVPSetXP3ArchiveExtractionFilter(previous);
        }
    } cleanup;
    const auto explicitFilter = +[](tTVPXP3ExtractionFilterInfo *, tTJSVariant *) {};
    TVPSetXP3ArchiveExtractionFilter(explicitFilter);
    Hxv4DecoderPlugin::Initialize();
    CHECK(TVPXP3ArchiveExtractionFilter == explicitFilter);
    Bytes bytes{'X', 'P', '3', '\r', '\n', ' ', '\n', 0x1a, 0x8b, 0x67, 1};
    auto integer = [&](std::uint64_t value, unsigned width) {
        for(unsigned i = 0; i < width; ++i) bytes.push_back(value >> (8 * i));
    };
    integer(36, 8); // Index follows the 19-byte header and 17-byte payload.
    bytes.resize(36);
    bytes.push_back(0);
    integer(26, 8);
    bytes.insert(bytes.end(), {'H', 'x', 'v', '4'});
    integer(14, 8);
    integer(19, 8); // Payload offset.
    integer(17, 4); // Includes a 16-byte authentication tag.
    integer(0, 2);
    auto *archive = new tTVPXP3Archive(TJS_W("hxv4-fixture.xp3"),
        new tTVPMemoryStream(bytes.data(), bytes.size()), 0);
    archive->Release();
    CHECK(TVPXP3ArchiveExtractionFilter != explicitFilter);
    REQUIRE(TVPXP3ArchiveExtractionFilter != nullptr);
    Hxv4DecoderPlugin::Teardown();
    CHECK(TVPXP3ArchiveExtractionFilter == explicitFilter);
}
