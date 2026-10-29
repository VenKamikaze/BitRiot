/*
 * MatchRecord.h
 *
 * A record of one round (brief 05): everything needed to play it again and prove it
 * played the same. A round is fully described by four things: the MatchConfig, a
 * hash of the game data (DataHash.h), a build id (BuildId.h), and the sequence of
 * TickInputs; the record adds checkpoints, a state hash every K ticks, so a replay
 * can say where it stopped agreeing.
 *
 * The byte format is specified in doco/netplay/match-record-format.md: versioned,
 * little-endian, fixed-width integers, so a JS or Go tool can read it. This header is
 * the C++ side: the value type, serialise/parse (which round-trip byte for byte), and
 * MatchRecorder, which builds a record as a round is stepped.
 *
 * Netplay (07) and hot-join (13) reuse it: the host keeps a MatchRecord of the round
 * so far, and a joiner replays it. Scripted inputs reuse it too: bitriot_headless
 * takes a record as its script.
 */

#ifndef _MATCHRECORD_H
#define _MATCHRECORD_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "MatchConfig.h"
#include "TickInputs.h"

class Simulation;

// A record that cannot be written (a field the format cannot hold) or read (bad
// magic, an unknown version, a truncated file, a count that does not add up). The
// message says what and, for a read, at which byte.
class MatchRecordError : public std::runtime_error
{
  public:
    explicit MatchRecordError(const std::string& what) : std::runtime_error(what) {}
};

struct MatchRecord
{
  static const uint16_t FORMAT_VERSION = 1;

  // The most ticks a record may hold (4,194,304: about 38 hours at 30 ticks a second).
  // It bounds what a hostile file can ask a reader to allocate.
  static const uint32_t MAX_TICKS = 1u << 22;

  struct Checkpoint
  {
    uint32_t tick = 0;  // the state hash after this many steps (0 = straight after start())
    uint64_t hash = 0;
    bool operator==(const Checkpoint& o) const { return tick == o.tick && hash == o.hash; }
    bool operator!=(const Checkpoint& o) const { return !(*this == o); }
  };

  uint16_t formatVersion = FORMAT_VERSION;
  // Run-length encode identical consecutive ticks when serialising (and: the record
  // that was parsed used it). Changes the bytes, never the meaning.
  bool rle = true;
  // The round had ended (Simulation::roundOver()) after the last tick.
  bool roundOver = false;
  // Simulation::STATE_HASH_VERSION of the build that made the checkpoints.
  uint32_t stateHashVersion = 0;
  // The interval the recorder was asked for; informational (the checkpoints list is
  // what counts). 0 = no periodic checkpoints.
  uint32_t checkpointInterval = 0;
  uint64_t buildId = 0;
  uint64_t dataHash = 0;
  MatchConfig config;
  // ticks[i] is the input of tick i + 1 (the (i+1)-th step after start()).
  std::vector<TickInputs> ticks;
  // Strictly increasing ticks, each at most ticks.size().
  std::vector<Checkpoint> checkpoints;

  // The bytes. Throws MatchRecordError if a field cannot be represented (a team above
  // 255, a map above 65535 tiles, more than 6 players, more than 255 control events in
  // one tick, a non-zero input byte for a slot beyond the config's players, ...).
  std::vector<uint8_t> serialize() const;

  // Parses a record. Throws MatchRecordError on anything wrong, naming the byte offset.
  // The result serialises back to exactly the bytes given. It checks the file is
  // well-formed, not that it replays: an event for a slot the round does not have is
  // for the replayer to refuse.
  static MatchRecord parse(const uint8_t* data, size_t size);
  static MatchRecord parse(const std::vector<uint8_t>& bytes) { return parse(bytes.data(), bytes.size()); }

  // File helpers. Throw MatchRecordError (also for an unreadable or unwritable file).
  static MatchRecord readFile(const std::string& path);
  void writeFile(const std::string& path) const;

  bool operator==(const MatchRecord& o) const;
  bool operator!=(const MatchRecord& o) const { return !(*this == o); }
};

// Builds a record while a round is stepped:
//
//   MatchRecorder rec(30);
//   sim.start(config);
//   rec.begin(sim);                       // after start(): header and the tick-0 checkpoint
//   ... TickInputs in = ...; sim.step(in); rec.onStep(in, sim);
//   const MatchRecord& r = rec.finish(sim);
//
// A checkpoint is taken at tick 0, at every multiple of the interval, and at the last
// tick. Reading stateHash() changes nothing about the round (a test pins that).
class MatchRecorder
{
  public:
    // interval 0: no periodic checkpoints (the tick-0 and final ones are still taken).
    explicit MatchRecorder(uint32_t checkpointInterval = 30, bool rle = true);

    // Call right after Simulation::start(). Throws std::logic_error if the simulation
    // has not just started (tick() != 0).
    void begin(const Simulation& sim);

    // Call right after each Simulation::step(inputs), with the inputs given to it.
    // Throws std::logic_error if the recorder and the simulation are out of step.
    void onStep(const TickInputs& inputs, const Simulation& sim);

    // Takes the last checkpoint and the round-over flag. The recorder may keep being
    // stepped afterwards (finish() can be called again).
    const MatchRecord& finish(const Simulation& sim);

    bool active() const { return m_active; }
    const MatchRecord& record() const { return m_record; }

  private:
    void checkpoint(const Simulation& sim);

    MatchRecord m_record;
    bool m_active = false;
};

#endif /* _MATCHRECORD_H */
