#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "XP3Archive.h"
#include "XP3ArchiveCxDecoder.h"
#include "Crypt/hxv4/Hxv4Decoder.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendLE(Bytes &bytes, std::uint64_t value, unsigned size) {
    for(unsigned i = 0; i < size; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void appendName(Bytes &bytes, const std::string &name) {
    appendLE(bytes, name.size(), 2);
    for(unsigned char ch : name)
        appendLE(bytes, ch, 2);
}

void appendChunk(Bytes &bytes, const std::string &tag, const Bytes &content) {
    bytes.insert(bytes.end(), tag.begin(), tag.end());
    appendLE(bytes, content.size(), 8);
    bytes.insert(bytes.end(), content.begin(), content.end());
}

Bytes makeIndex(const Bytes &content, bool compressed, bool continued) {
    Bytes result{static_cast<std::uint8_t>((compressed ? 1 : 0) |
                                         (continued ? 0x80 : 0))};
    Bytes packed = content;
    if(compressed) {
        packed.resize(compressBound(content.size()));
        uLongf size = packed.size();
        REQUIRE(compress(packed.data(), &size, content.data(), content.size()) == Z_OK);
        packed.resize(size);
    }
    appendLE(result, packed.size(), 8);
    if(compressed)
        appendLE(result, content.size(), 8);
    result.insert(result.end(), packed.begin(), packed.end());
    return result;
}

struct TemporaryXP3 {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("aether-v3-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()) + ".xp3");
    ~TemporaryXP3() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

struct Hxv4PluginScope {
    Hxv4PluginScope() { AetherKiri::Hxv4::Hxv4DecoderPlugin::Initialize(); }
    ~Hxv4PluginScope() { AetherKiri::Hxv4::Hxv4DecoderPlugin::Teardown(); }
};

} // namespace

TEST_CASE("built-in XP3 Cx selection rejects pre-decoded bytecode") {
    constexpr std::uint32_t fingerprint = 0xe425cd85u;
    const std::array<std::uint8_t, 8> bytecode = {
        'T', 'J', 'S', '2', '1', '0', '0', 0,
    };
    const std::array<std::uint8_t, 8> encrypted = {
        0x31, 0x2f, 0x36, 0x57, 0x54, 0x55, 0x55, 0x65,
    };

    CHECK(TVPIsBuiltinXP3CxScheme(fingerprint));
    CHECK_FALSE(TVPShouldUseBuiltinXP3CxDecoder(
        fingerprint, bytecode.data(), bytecode.size()));
    CHECK(TVPShouldUseBuiltinXP3CxDecoder(
        fingerprint, encrypted.data(), encrypted.size()));
    CHECK_FALSE(TVPShouldUseBuiltinXP3CxDecoder(
        0x12345678u, encrypted.data(), encrypted.size()));
}

TEST_CASE("built-in XP3 Cx decoder is shared by sibling archives") {
    constexpr std::uint32_t fingerprint = 0xe425cd85u;

    TVPResetBuiltinXP3CxDecoder();
    CHECK_FALSE(TVPIsBuiltinXP3CxDecoderActive());

    CHECK(TVPActivateBuiltinXP3CxDecoder(fingerprint));
    CHECK(TVPIsBuiltinXP3CxDecoderActive());

    TVPResetBuiltinXP3CxDecoder();
    CHECK_FALSE(TVPIsBuiltinXP3CxDecoderActive());
}

TEST_CASE("XP3 v3 mapped files supersede literal-name placeholders") {
    const bool placeholderFirst = GENERATE(false, true);
    const bool compressed = GENERATE(false, true);
    const bool continued = GENERATE(false, true);
    CAPTURE(placeholderFirst, compressed, continued);
    // The application autoloads Hxv4 before mounting the project's XP3s.
    Hxv4PluginScope hxv4;
    REQUIRE(TVPXP3ArchiveExtractionFilter == nullptr);
    struct ResetDecoder {
        ~ResetDecoder() { TVPResetBuiltinXP3CxDecoder(); }
    } resetDecoder;

    // Synthetic bytecode and archive metadata: no proprietary game content.
    const Bytes expected{'T', 'J', 'S', '2', '1', '0', '0', 0, 1, 2, 3, 4};
    Bytes encrypted = expected;
    REQUIRE(TVPActivateBuiltinXP3CxDecoder(0xe425cd85u));
    REQUIRE(TVPDecodeBuiltinXP3Cx(0xe425cd85u, 0,
                                encrypted.data(), encrypted.size()));
    TVPResetBuiltinXP3CxDecoder();

    Bytes archive{'X', 'P', '3', 13, 10, 32, 10, 26, 0x8b, 0x67, 1};
    archive.resize(19); // reserve the index offset
    Bytes files;
    const auto addFile = [&](const std::string &name, std::uint32_t hash,
                              const Bytes &payload, bool protectedFile) {
        Bytes info;
        appendLE(info, protectedFile ? 0x80000000u : 0u, 4);
        appendLE(info, payload.size(), 8);
        appendLE(info, payload.size(), 8);
        appendName(info, name);
        Bytes segment;
        appendLE(segment, 0, 4);
        appendLE(segment, archive.size(), 8);
        appendLE(segment, payload.size(), 8);
        appendLE(segment, payload.size(), 8);
        Bytes checksum;
        appendLE(checksum, hash, 4);
        Bytes item;
        appendChunk(item, "info", info);
        appendChunk(item, "segm", segment);
        appendChunk(item, "adlr", checksum);
        appendChunk(files, "File", item);
        archive.insert(archive.end(), payload.begin(), payload.end());
    };
    if(placeholderFirst)
        addFile("startup.tjs", 0x12345678u, {'d', 'e', 'c', 'o', 'y'}, true);
    addFile("opaque-entry", 0xe425cd85u, encrypted, true);
    if(!placeholderFirst)
        addFile("startup.tjs", 0x12345678u, {'d', 'e', 'c', 'o', 'y'}, true);
    const Bytes readme{'o', 'k'};
    addFile("readme.txt", 0x87654321u, readme, false);

    Bytes mapping;
    appendLE(mapping, 0xe425cd85u, 4);
    appendName(mapping, "startup.tjs");
    Bytes mappings;
    appendChunk(mappings, "hnfn", mapping);
    const auto indexOffset = archive.size();
    if(continued) {
        const auto first = makeIndex(mappings, compressed, true);
        archive.insert(archive.end(), first.begin(), first.end());
        appendLE(archive, archive.size() + 8, 8);
    } else {
        // hnfn may appear after File chunks in the same index.
        files.insert(files.end(), mappings.begin(), mappings.end());
    }
    const auto index = makeIndex(files, compressed, false);
    archive.insert(archive.end(), index.begin(), index.end());
    for(unsigned i = 0; i < 8; ++i)
        archive[11 + i] = static_cast<std::uint8_t>(indexOffset >> (i * 8));

    TemporaryXP3 fixture;
    {
        std::ofstream output(fixture.path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(archive.data()), archive.size());
        REQUIRE(output.good());
    }
    std::unique_ptr<tTVPXP3Archive> opened{
        new tTVPXP3Archive(ttstr(fixture.path.string()))};
    REQUIRE(opened->GetCount() == 2);
    CHECK_FALSE(opened->IsExistent(TJS_W("opaque-entry")));
    CHECK(TVPIsBuiltinXP3CxDecoderActive());
    std::unique_ptr<tTJSBinaryStream> startup{
        opened->CreateStream(TJS_W("startup.tjs"))};
    Bytes actual(startup->GetSize());
    startup->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == expected);
    std::unique_ptr<tTJSBinaryStream> other{
        opened->CreateStream(TJS_W("readme.txt"))};
    actual.resize(other->GetSize());
    other->ReadBuffer(actual.data(), actual.size());
    CHECK(actual == readme);
}

TEST_CASE("XP3 Hxv4 filter activation preserves the previous filter lifecycle") {
    struct RestoreFilter {
        tTVPXP3ArchiveExtractionFilter previous = TVPXP3ArchiveExtractionFilter;
        ~RestoreFilter() { TVPSetXP3ArchiveExtractionFilter(previous); }
    } restore;
    const tTVPXP3ArchiveExtractionFilter previous =
        [](tTVPXP3ExtractionFilterInfo *, tTJSVariant *) {};
    TVPSetXP3ArchiveExtractionFilter(previous);
    {
        Hxv4PluginScope hxv4;
        CHECK(TVPXP3ArchiveExtractionFilter == previous);

        Bytes archive{'X', 'P', '3', 13, 10, 32, 10, 26, 0x8b, 0x67, 1};
        appendLE(archive, 51, 8); // index follows a 32-byte Hxv4 payload
        archive.resize(51);
        Bytes metadata;
        appendLE(metadata, 19, 8);
        appendLE(metadata, 32, 4);
        appendLE(metadata, 0, 2);
        Bytes chunks;
        appendChunk(chunks, "Hxv4", metadata);
        const auto index = makeIndex(chunks, false, false);
        archive.insert(archive.end(), index.begin(), index.end());
        TemporaryXP3 fixture;
        {
            std::ofstream output(fixture.path, std::ios::binary);
            output.write(reinterpret_cast<const char *>(archive.data()), archive.size());
            REQUIRE(output.good());
        }
        std::unique_ptr<tTVPXP3Archive> opened{
            new tTVPXP3Archive(ttstr(fixture.path.string()))};
        CHECK(TVPXP3ArchiveExtractionFilter != nullptr);
        CHECK(TVPXP3ArchiveExtractionFilter != previous);
    }
    CHECK(TVPXP3ArchiveExtractionFilter == previous);
}
