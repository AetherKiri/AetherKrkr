#ifndef XP3ArchiveHashDecoderH
#define XP3ArchiveHashDecoderH

#include <cstddef>
#include <cstdint>

// The rotating-hash XP3 format uses a 31-byte per-file XOR key and a
// 61-byte additive key. Detection is deliberately limited to media headers;
// callers must also verify the decoded entry's Adler-32 before enabling it.
bool TVPIsXP3HashDecoderCandidate(std::uint32_t hash, const void *header,
                                std::size_t size);
void TVPDecodeXP3Hash(std::uint32_t hash, std::uint64_t offset, void *buffer,
                     std::size_t size);

#endif
