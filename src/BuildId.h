/*
 * BuildId.h
 *
 * The build id (brief 05): a 64-bit hash of the simulation's SOURCE, generated at
 * build time by cmake/GenerateBuildId.cmake. Two builds have the same id exactly
 * when the simulation code is the same text, whatever the compiler, flags,
 * optimisation level or operating system. A match record carries it and netplay
 * (07) compares it in HELLO.
 *
 * WHAT IS HASHED. The files of the bitriot_sim target and every project header they
 * include, directly or not (an include resolves next to the including file, then
 * under src/, then under the repository root, as the compiler does), in order of
 * path; for each: its path relative to the repository root, a newline, its text with
 * every carriage return removed (so git autocrlf cannot change the id), and a
 * trailer line. MD5 of that stream; the id is the first 8 bytes, read as a
 * big-endian number (so its 16 hex digits are the first 16 digits of the MD5).
 * The data files are not part of it (they have the data hash, DataHash.h).
 *
 * The id is regenerated on every build (a custom target), so editing a simulation
 * file always changes it, and it is written only when it changed, so an unchanged
 * simulation does not rebuild. A core-only file (rendering, input, menus) does not
 * move it.
 *
 * It says "same code", not "same behaviour on this machine": proving that is the
 * cross-platform gate (brief 05b).
 */

#ifndef _BUILDID_H
#define _BUILDID_H

#include <cstdint>
#include <string>

// Defined in the generated BuildId.cpp.
uint64_t bitriotBuildId();

// "0123456789abcdef": 16 lowercase hex digits.
std::string bitriotHex64(uint64_t value);

inline std::string bitriotBuildIdString()
{
  return bitriotHex64(bitriotBuildId());
}

#endif /* _BUILDID_H */
