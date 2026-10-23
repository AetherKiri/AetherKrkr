#include <catch2/catch_test_macros.hpp>

#include "BinaryStream.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;

void integer(Bytes &bytes, std::uint64_t value, unsigned width) {
    for(unsigned i = 0; i < width; ++i)
        bytes.push_back(static_cast<unsigned char>(value >> (i * 8)));
}

void chunk(Bytes &bytes, const char *tag, const Bytes &body) {
    bytes.insert(bytes.end(), tag, tag + 4);
    integer(bytes, body.size(), 8);
    bytes.insert(bytes.end(), body.begin(), body.end());
}

class ArchiveProject {
public:
    ArchiveProject()
        : previousProject_(TVPProjectDir),
          previousDirectory_(TVPNormalizeStorageName(TJS_W("./"))) {
        path_ = std::filesystem::temp_directory_path() /
                ("aether-archives-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
        root_ = TVPNormalizeStorageName(ttstr(path_.string() + "/"));
    }

    ~ArchiveProject() {
        TVPBoostAutoMountPaths();
        for(const auto &name : archives_) {
            TVPRemoveAutoPath(root_ + ttstr(name) + TJS_W(">"));
            TVPRemoveAutoPath(root_ + ttstr(name) + TJS_W(">system/"));
        }
        TVPProjectDir = previousProject_;
        TVPSetCurrentDirectory(previousDirectory_);
        TVPClearStorageCaches();
        TVPClearArchiveCache();
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    void archive(const char *name,
                 const std::vector<std::pair<std::string, std::string>> &files) {
        Bytes output{'X', 'P', '3', '\r', '\n', ' ', '\n', 0x1a, 0x8b, 0x67, 1};
        integer(output, 0, 8);
        Bytes index;
        for(const auto &file : files) {
            const auto offset = output.size();
            output.insert(output.end(), file.second.begin(), file.second.end());
            Bytes info, segments, checksum, record;
            integer(info, 0, 4);
            integer(info, file.second.size(), 8);
            integer(info, file.second.size(), 8);
            integer(info, file.first.size(), 2);
            for(unsigned char ch : file.first)
                integer(info, ch, 2);
            integer(segments, 0, 4);
            integer(segments, offset, 8);
            integer(segments, file.second.size(), 8);
            integer(segments, file.second.size(), 8);
            integer(checksum, 0, 4);
            chunk(record, "info", info);
            chunk(record, "segm", segments);
            chunk(record, "adlr", checksum);
            chunk(index, "File", record);
        }
        const std::uint64_t indexOffset = output.size();
        for(unsigned i = 0; i < 8; ++i)
            output[11 + i] = static_cast<unsigned char>(indexOffset >> (i * 8));
        output.push_back(0);
        integer(output, index.size(), 8);
        output.insert(output.end(), index.begin(), index.end());
        std::ofstream stream(path_ / name, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(output.data()), output.size());
        REQUIRE(stream.good());
        archives_.emplace_back(name);
    }

    void select(const char *name) {
        TVPProjectDir = root_;
        if(*name)
            TVPProjectDir += ttstr(name) + TJS_W(">");
        TVPSetCurrentDirectory(TVPProjectDir);
        TVPClearStorageCaches();
        TVPAutoMountSiblingXP3Archives();
    }

    std::string read(const tjs_char *name) {
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name));
        std::string value(stream->GetSize(), '\0');
        stream->ReadBuffer(value.data(), value.size());
        return value;
    }

private:
    ttstr previousProject_, previousDirectory_, root_;
    std::filesystem::path path_;
    std::vector<std::string> archives_;
};
} // namespace

TEST_CASE("renamed XP3 siblings expose startup and preserve patch overlays",
          "[storage][startup]") {
    ArchiveProject project;
    project.archive("data.bin", {{"startup.tjs", "startup"},
                                 {"system/initialize.tjs", "framework"},
                                 {"translated.txt", "base"}});
    project.archive("assets.DAT", {{"asset.txt", "renamed asset"}});
    project.archive("patch.xp3", {{"translated.txt", "patch"}});
    project.archive("patch2.xp3", {{"translated.txt", "patch2"}});

    // An explicit project archive remains the current directory, whose
    // direct files take precedence over auto paths. Directory launches use
    // the normal numbered-patch order instead.
    std::string expected;
    SECTION("directory entry") {
        project.select("");
        expected = "patch2";
    }
    SECTION("selected patch entry") {
        project.select("patch.xp3");
        expected = "patch";
    }
    SECTION("selected renamed base entry") {
        project.select("data.bin");
        expected = "base";
    }

    CHECK(project.read(TJS_W("startup.tjs")) == "startup");
    CHECK(project.read(TJS_W("system/initialize.tjs")) == "framework");
    CHECK(project.read(TJS_W("asset.txt")) == "renamed asset");
    CHECK(project.read(TJS_W("translated.txt")) == expected);
    TVPBoostAutoMountPaths();
    CHECK(project.read(TJS_W("translated.txt")) == expected);
}
