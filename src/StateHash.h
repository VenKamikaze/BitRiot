/*
 * StateHash.h
 *
 * The accumulator behind Simulation::stateHash(): the simulation's desync
 * detector. The full list of what is hashed, in what order, is the "State hash
 * specification" comment in Simulation.h; this file defines only how bytes are
 * fed in.
 *
 * ALGORITHM: FNV-1a, 64 bit.
 *
 *     hash  = 0xcbf29ce484222325                 (offset basis)
 *     for each byte b, in order:
 *         hash = (hash XOR b) * 0x100000001b3    (mod 2^64)
 *
 * Every value is fed as a fixed number of bytes, least significant byte first,
 * whatever the host's endianness or the width of its `int`/`long`:
 *
 *     u8   1 byte    bool (0 or 1) and small flags
 *     i32  4 bytes   int, fixed_t, enum values and unsigned ints (two's complement)
 *     u64  8 bytes   RNG state, tick counters
 *
 * Nothing goes through float or double, and no padding or memory layout is ever
 * hashed. Variable-length lists are written as an i32 count followed by the
 * items, so two different layouts cannot produce the same byte stream by
 * accident.
 *
 * ENTITY REFERENCES. Absolute entity ids are not hashed. Where one entity refers
 * to another (an AI goal's target, a tile list's members, a queued message's
 * sender), the reference is written as ref(id): the position of that entity in
 * the entity list's walk order (0 = first, in ascending id order), or -1 if no
 * live entity has that id. Two peers that built the same entities in the same
 * order therefore agree whatever their id counters started at.
 *
 * FNV-1a is chosen for being trivial to specify and to reimplement in any
 * language (a replay tool, a browser build). It is a desync detector, not a
 * cryptographic hash: it only has to tell honest runs apart.
 */

#ifndef _STATEHASH_H
#define _STATEHASH_H

#include <algorithm>
#include <cstdint>
#include <vector>

class StateHasher
{
  public:
    StateHasher() : m_hash(OFFSET_BASIS) {}

    void u8(uint8_t v)
    {
      m_hash ^= v;
      m_hash *= PRIME;
    }

    void flag(bool v)
    {
      u8(v ? 1 : 0);
    }

    // Every int-like value (int, fixed_t, enum, unsigned) goes in as 32 bits.
    void i32(int32_t v)
    {
      const uint32_t u = (uint32_t) v;
      u8((uint8_t) (u & 0xff));
      u8((uint8_t) ((u >> 8) & 0xff));
      u8((uint8_t) ((u >> 16) & 0xff));
      u8((uint8_t) ((u >> 24) & 0xff));
    }

    void u64(uint64_t v)
    {
      for (int shift = 0; shift < 64; shift += 8)
      {
        u8((uint8_t) ((v >> shift) & 0xff));
      }
    }

    // A list length. Always call before the items.
    void count(size_t n)
    {
      i32((int32_t) n);
    }

    // The sorted (ascending) ids of the live entities, in walk order. Must be set
    // before any ref() is written.
    void setEntityIds(const std::vector<int>& sortedIds)
    {
      m_ids = sortedIds;
    }

    // Walk position of the entity with this id, or -1 if there is none.
    int32_t refOf(int id) const
    {
      std::vector<int>::const_iterator it = std::lower_bound(m_ids.begin(), m_ids.end(), id);
      if (it == m_ids.end() || *it != id)
      {
        return -1;
      }
      return (int32_t) (it - m_ids.begin());
    }

    void ref(int id)
    {
      i32(refOf(id));
    }

    uint64_t value() const
    {
      return m_hash;
    }

    static constexpr uint64_t OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static constexpr uint64_t PRIME = 0x100000001b3ULL;

  private:
    uint64_t m_hash;
    std::vector<int> m_ids;
};

#endif /* _STATEHASH_H */
