/*
 * DataHash.h
 *
 * The data hash (brief 05): one 64-bit number that says "these two builds run the
 * simulation on the same game data and the same tuning constants". A match record
 * carries it and netplay (07) compares it in HELLO; a peer with a different value
 * would play a different game from the first tick.
 *
 * WHAT IS HASHED. A list of "name=value" items, sorted by name, from two sources:
 *
 *   1. every constant of data/entity.txt and data/player.txt, AFTER parsing
 *      (DataReader): the name is "data/entity.txt:BOMB_FUSE", the value is the int in
 *      decimal ("045" and "45" are the same constant) or the string as it is. The raw
 *      bytes are never hashed, so comments, blank lines, key order, tabs against
 *      spaces and line endings (git autocrlf on Windows) do not matter. The other
 *      data files (renderer.txt, audio.txt, messages.txt) are not simulation data.
 *   2. the named tuning constants the simulation has in code, not in a data file:
 *      listed in DataHash.cpp (simConstantItems), named "sim:NAME". Any other number
 *      in the simulation's code is covered by the build id, not by this hash.
 *
 * ALGORITHM. Sort the items bytewise by name (unsigned byte order; names are unique, and
 * the sort is on the name alone, not on the finished "name=value" text). Feed FNV-1a 64 (offset basis 0xcbf29ce484222325, prime
 * 0x100000001b3) with, for each item in order, the bytes of "name=value" followed
 * by one 0x0A byte. The result is the data hash.
 *
 * The files are loaded eagerly (Simulation::start() and dataHash() both do it): a
 * file that cannot be read or parsed throws std::runtime_error here instead of
 * answering "1" for every constant, as DataReader's lazy lookup does.
 */

#ifndef _DATAHASH_H
#define _DATAHASH_H

#include <cstdint>
#include <string>
#include <vector>

namespace DataHash
{
  // The data files the simulation reads, in the form DataReader is given them.
  extern const char* const ENTITY_FILE;
  extern const char* const PLAYER_FILE;

  // Parses both files now (a no-op for a file already loaded). Throws
  // std::runtime_error naming the file and the DataReader's reason if one cannot
  // be read or parsed.
  void loadSimData();

  // The sorted "name=value" items the hash is computed over (loads the files).
  std::vector<std::string> items();

  // FNV-1a 64 over the items (see above).
  uint64_t hash();

  // The same algorithm over items already in sorted-by-name order (for tests and
  // for tools that re-implement the hash).
  uint64_t hashOfItems(const std::vector<std::string>& sortedItems);
}

#endif /* _DATAHASH_H */
