#include <catch2/catch_test_macros.hpp>

#include "UtilStreams.h"
#include "XP3Archive.h"
#include "Crypt/hxv4/Hxv4Decoder.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {
using AetherKiri::Hxv4::Hxv4DecoderPlugin;

struct FilterCleanup {
    tTVPXP3ArchiveExtractionFilter previous = TVPXP3ArchiveExtractionFilter;
    ~FilterCleanup() {
        Hxv4DecoderPlugin::Teardown();
        TVPSetXP3ArchiveExtractionFilter(previous);
    }
};
} // namespace

TEST_CASE("idle Hxv4 provider preserves extraction filter registration", "[storage][xp3]") {
    FilterCleanup cleanup;
    TVPSetXP3ArchiveExtractionFilter(nullptr);
    SECTION("no game filter") {}
    SECTION("existing game filter") {
        TVPSetXP3ArchiveExtractionFilter([](tTVPXP3ExtractionFilterInfo *info, tTJSVariant *) {
            auto *bytes = static_cast<tjs_uint8 *>(info->Buffer);
            for(tjs_uint i = 0; i < info->BufferSize; ++i) bytes[i] ^= 0x5a;
        });
    }
    const auto filter = TVPXP3ArchiveExtractionFilter;
    Hxv4DecoderPlugin::Initialize();
    CHECK(TVPXP3ArchiveExtractionFilter == filter);

    std::array<tjs_uint8, 4> bytes{0x12, 0x34, 0x56, 0x78};
    auto expected = bytes;
    if(filter) for(auto &byte : expected) byte ^= 0x5a;
    auto *archive = new tTVPXP3Archive(TJS_W("idle-hxv4-fixture.xp3"), 0);
    archive->ItemVector.resize(1);
    auto &item = archive->ItemVector[0];
    item.Name = TJS_W("fixture.bin");
    item.OrgSize = bytes.size();
    item.Segments.push_back({0, 0, bytes.size(), bytes.size(), false});
    {
        tTVPXP3ArchiveStream stream(archive, 0, &item.Segments,
            new tTVPMemoryStream(bytes.data(), bytes.size()), bytes.size());
        stream.ReadBuffer(bytes.data(), bytes.size());
    }
    archive->Release();
    CHECK(bytes == expected);
    Hxv4DecoderPlugin::Teardown();
    CHECK(TVPXP3ArchiveExtractionFilter == filter);
}

TEST_CASE("Hxv4 installs its filter when a matching chunk is found", "[storage][xp3]") {
    FilterCleanup cleanup;
    const auto explicitFilter = +[](tTVPXP3ExtractionFilterInfo *, tTJSVariant *) {};
    TVPSetXP3ArchiveExtractionFilter(explicitFilter);
    Hxv4DecoderPlugin::Initialize();
    CHECK(TVPXP3ArchiveExtractionFilter == explicitFilter);
    std::vector<tjs_uint8> bytes{'X', 'P', '3', '\r', '\n', ' ', '\n', 0x1a, 0x8b, 0x67, 1};
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
