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
#include <string>
#include <vector>

class StateHasher
{
  public:
    StateHasher() : m_hash(OFFSET_BASIS), m_trace(nullptr), m_prefix("") {}

    // Every write names its field ("entity.health", "bomb.fuse"). The name does
    // not enter the hash; it only feeds the optional trace below.
    void u8(uint8_t v, const char* name)
    {
      note(name, "u8");
      raw(v);
    }

    void flag(bool v, const char* name)
    {
      note(name, "u8");
      raw(v ? 1 : 0);
    }

    // Every int-like value (int, fixed_t, enum, unsigned) goes in as 32 bits.
    void i32(int32_t v, const char* name)
    {
      note(name, "i32");
      raw32(v);
    }

    void u64(uint64_t v, const char* name)
    {
      note(name, "u64");
      for (int shift = 0; shift < 64; shift += 8)
      {
        raw((uint8_t) ((v >> shift) & 0xff));
      }
    }

    // A list length. Always call before the items.
    void count(size_t n, const char* name)
    {
      note(name, "count");
      raw32((int32_t) n);
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

    void ref(int id, const char* name)
    {
      note(name, "ref");
      raw32(refOf(id));
    }

    uint64_t value() const
    {
      return m_hash;
    }

    // SCHEMA TRACE. With a trace attached, every write also appends
    // "[TYPE/]name:kind" (kind is u8, i32, u64, count or ref) to it, in the order the
    // fields are hashed. The trace holds field names and widths, never values, so
    // a test can pin the hash's layout without depending on how the simulation
    // happens to play: dropping, adding, renaming or re-typing a field changes the
    // trace, a legitimate AI, rules or data change does not. See
    // SimulationTest.TheHashSchemaIsPinned.
    void setTrace(std::vector<std::string>* trace)
    {
      m_trace = trace;
    }

    // Trace entries written after this are prefixed "PREFIX/" (an entity walk sets
    // the entity's type name, so BOMB/entity.health and ROBOT1/mover.decayCounter
    // are different entries). Empty for no prefix. Only the trace sees it.
    void setTracePrefix(const char* prefix)
    {
      m_prefix = prefix;
    }

    static constexpr uint64_t OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static constexpr uint64_t PRIME = 0x100000001b3ULL;

  private:
    void raw(uint8_t v)
    {
      m_hash ^= v;
      m_hash *= PRIME;
    }

    void raw32(int32_t v)
    {
      const uint32_t u = (uint32_t) v;
      raw((uint8_t) (u & 0xff));
      raw((uint8_t) ((u >> 8) & 0xff));
      raw((uint8_t) ((u >> 16) & 0xff));
      raw((uint8_t) ((u >> 24) & 0xff));
    }

    void note(const char* name, const char* kind)
    {
      if (m_trace != nullptr)
      {
        m_trace->push_back((m_prefix[0] == 0 ? std::string() : std::string(m_prefix) + "/") + name + ":" + kind);
      }
    }

    uint64_t m_hash;
    std::vector<int> m_ids;
    std::vector<std::string>* m_trace;
    const char* m_prefix;
};

#endif /* _STATEHASH_H */
