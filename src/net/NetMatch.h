/*
 * NetMatch.h
 *
 * A LockstepSession driving a Simulation: the glue the game (GameEngine) and
 * bitriot_headless's host/join commands share, so a networked round steps through one
 * code path wherever it runs. It starts the Simulation from the round the session
 * announced, steps it with the host's confirmed inputs, hands the session the local
 * sample for the tick being stepped, reports checkpoint hashes, records the round the
 * host decided (the FINAL TickInputs, not the local sample), and keeps the desync
 * evidence: a ring of state dumps for the last few checkpoint ticks, because by the time
 * a DESYNC arrives the machines are past the tick in question and the state of that tick
 * can no longer be reproduced (undefined behaviour might not replay the same way).
 *
 * It does not sample devices or script inputs (the caller passes the sample), draw or
 * pump the network (the caller calls session.pump() every frame, in every state).
 */

#ifndef _NETMATCH_H
#define _NETMATCH_H

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "LockstepSession.h"
#include "MatchRecord.h"

class Simulation;

namespace netplay
{
  struct NetMatchOptions
  {
    // A state hash is reported (and a dump kept) every this many ticks, and on the
    // last tick of a round.
    uint32_t checkpointInterval = 30;
    // How many checkpoint dumps are kept. 16 is about 8 seconds at the default
    // interval, well past the time a DESYNC takes to arrive.
    size_t dumpRingSize = 16;
    // Where desync files and round records are written; empty = the working directory.
    std::string outputDir;
    // Write <outputDir>/round-<n>-peer<id>.brmr when a round finishes.
    bool recordRounds = false;
  };

  class NetMatch
  {
    public:
      typedef NetMatchOptions Options;

      NetMatch(LockstepSession& session, Simulation& sim, const Options& options = Options());

      // Starts the simulation for a round the session announced (LockstepSession::takeRoundStart).
      // Throws what Simulation::start() throws (the config is checked by the session first).
      void beginRound(const RoundStart& round);

      bool active() const { return m_active; }
      const RoundStart& round() const { return m_round; }
      uint8_t localSlotMask() const { return m_round.localSlotMask; }

      // The next tick can be stepped (the session holds the host's inputs for it).
      bool canStep() const { return m_active && m_session.canStep(); }
      // Steps one tick with the host's confirmed inputs and returns them. `localSample` is
      // what this machine's devices (or a script) did for it, `sampledMask` the slots the
      // sample covers. Call only when canStep(). On the last tick of a round the session is
      // told (roundFinished) and active() becomes false.
      const TickInputs& step(const TickInputs& localSample, uint8_t sampledMask);

      // The record of the round so far (final TickInputs, a checkpoint every interval).
      const MatchRecord& record();

      // Desync evidence. Valid once session.desynced().
      struct DesyncFiles
      {
        std::string dumpAtTick;     // state dump of the first disagreeing checkpoint ("" if it was no longer kept)
        std::string dumpNow;        // state dump of this machine's current tick
        std::string record;         // the round so far
      };
      // Writes the evidence (once; later calls return the same paths). False with `error`
      // if a file could not be written.
      bool writeDesyncFiles(DesyncFiles& out, std::string* error);

      // Writes the finished round's record to <outputDir>/round-<n>-peer<id>.brmr.
      bool writeRoundRecord(std::string* path, std::string* error);

      // Hash and dump of the latest checkpoint (for the headless output and tests).
      uint32_t lastCheckpointTick() const { return m_lastCheckpointTick; }
      uint64_t lastCheckpointHash() const { return m_lastCheckpointHash; }
      // Ticks whose dumps are still kept (oldest first).
      std::vector<uint32_t> keptDumpTicks() const;

    private:
      struct Dump
      {
        uint32_t tick;
        std::string text;
      };
      std::string path(const std::string& name) const;

      LockstepSession& m_session;
      Simulation& m_sim;
      Options m_opt;
      bool m_active = false;
      RoundStart m_round;
      MatchRecorder m_recorder;
      std::deque<Dump> m_dumps;
      uint32_t m_lastCheckpointTick = 0;
      uint64_t m_lastCheckpointHash = 0;
      bool m_filesWritten = false;
      DesyncFiles m_files;
      TickInputs m_lastInputs;
  };
}

#endif /* _NETMATCH_H */
