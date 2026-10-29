#include "MatchRecord.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "BuildId.h"
#include "Simulation.h"

namespace
{
const uint8_t MAGIC[4] = { 'B', 'R', 'M', 'R' };
const uint16_t FLAG_RLE = 1 << 0;
const uint16_t FLAG_ROUND_OVER = 1 << 1;
const uint16_t KNOWN_FLAGS = FLAG_RLE | FLAG_ROUND_OVER;
const uint32_t MAX_RUN = 65535;
const size_t MAX_EVENTS_PER_TICK = 255;
const size_t CHECKPOINT_BYTES = 12;

std::string hexOffset(size_t offset)
{
  return "byte " + std::to_string(offset);
}

class Writer
{
  public:
    void u8(uint8_t v) { m_bytes.push_back(v); }
    void u16(uint16_t v)
    {
      m_bytes.push_back((uint8_t) (v & 0xff));
      m_bytes.push_back((uint8_t) (v >> 8));
    }
    void u32(uint32_t v)
    {
      for (int shift = 0; shift < 32; shift += 8)
      {
        m_bytes.push_back((uint8_t) ((v >> shift) & 0xff));
      }
    }
    void u64(uint64_t v)
    {
      for (int shift = 0; shift < 64; shift += 8)
      {
        m_bytes.push_back((uint8_t) ((v >> shift) & 0xff));
      }
    }
    void bytes(const std::vector<uint8_t>& b) { m_bytes.insert(m_bytes.end(), b.begin(), b.end()); }
    size_t size() const { return m_bytes.size(); }
    std::vector<uint8_t>& data() { return m_bytes; }

  private:
    std::vector<uint8_t> m_bytes;
};

class Reader
{
  public:
    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    size_t offset() const { return m_pos; }
    size_t remaining() const { return m_size - m_pos; }

    void need(size_t n, const char* what)
    {
      if (n > remaining())
      {
        throw MatchRecordError("record is truncated at " + hexOffset(m_pos) + ": " + what + " needs " + std::to_string(n)
                               + " more bytes, " + std::to_string(remaining()) + " are left");
      }
    }
    uint8_t u8(const char* what)
    {
      need(1, what);
      return m_data[m_pos++];
    }
    uint16_t u16(const char* what)
    {
      need(2, what);
      const uint16_t v = (uint16_t) (m_data[m_pos] | (m_data[m_pos + 1] << 8));
      m_pos += 2;
      return v;
    }
    uint32_t u32(const char* what)
    {
      need(4, what);
      uint32_t v = 0;
      for (int i = 3; i >= 0; i--)
      {
        v = (v << 8) | m_data[m_pos + i];
      }
      m_pos += 4;
      return v;
    }
    uint64_t u64(const char* what)
    {
      need(8, what);
      uint64_t v = 0;
      for (int i = 7; i >= 0; i--)
      {
        v = (v << 8) | m_data[m_pos + i];
      }
      m_pos += 8;
      return v;
    }

  private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

bool sameTick(const TickInputs& a, const TickInputs& b)
{
  return a.slot == b.slot && a.control == b.control;
}

void checkRange(long long value, long long lo, long long hi, const char* what)
{
  if (value < lo || value > hi)
  {
    throw MatchRecordError(std::string("cannot record ") + what + " " + std::to_string(value) + ": the format holds "
                           + std::to_string(lo) + " to " + std::to_string(hi));
  }
}

void writeTick(Writer& w, const TickInputs& t, int numPlayers)
{
  for (int slot = 0; slot < numPlayers; slot++)
  {
    w.u8(t.slot[slot].bits);
  }
  if (t.control.size() > MAX_EVENTS_PER_TICK)
  {
    throw MatchRecordError("cannot record " + std::to_string(t.control.size()) + " control events in one tick: the format holds "
                           + std::to_string(MAX_EVENTS_PER_TICK));
  }
  w.u8((uint8_t) t.control.size());
  for (const ControlEvent& e : t.control)
  {
    checkRange(e.slot, 0, 255, "a control event's slot");
    checkRange((long long) e.kind, 0, 255, "a control event's kind");
    w.u8((uint8_t) e.slot);
    w.u8((uint8_t) e.kind);
  }
}

TickInputs readTick(Reader& r, int numPlayers)
{
  TickInputs t;
  for (int slot = 0; slot < numPlayers; slot++)
  {
    t.slot[slot] = PlayerInput(r.u8("a player's input byte"));
  }
  const uint8_t events = r.u8("a control event count");
  for (int i = 0; i < events; i++)
  {
    const uint8_t slot = r.u8("a control event's slot");
    const uint8_t kind = r.u8("a control event's kind");
    t.control.push_back(ControlEvent((int) slot, (ControlEvent::Kind) kind));
  }
  return t;
}
} // namespace

std::vector<uint8_t> MatchRecord::serialize() const
{
  const int numPlayers = config.numPlayers();
  checkRange(numPlayers, 0, std::GameSettings::MAX_PLAYERS, "a player count of");
  checkRange((long long) ticks.size(), 0, MAX_TICKS, "a tick count of");
  checkRange(config.mapWidth, 0, 65535, "a map width of");
  checkRange(config.mapHeight, 0, 65535, "a map height of");
  checkRange(config.blockPercentage, -32768, 32767, "a block percentage of");

  for (size_t i = 0; i < ticks.size(); i++)
  {
    for (int slot = numPlayers; slot < std::GameSettings::MAX_PLAYERS; slot++)
    {
      if (ticks[i].slot[slot].bits != 0)
      {
        throw MatchRecordError("tick " + std::to_string(i + 1) + " has an input for slot " + std::to_string(slot)
                               + ", but the config has only " + std::to_string(numPlayers) + " players");
      }
    }
  }
  uint32_t previous = 0;
  for (size_t i = 0; i < checkpoints.size(); i++)
  {
    if (checkpoints[i].tick > ticks.size() || (i > 0 && checkpoints[i].tick <= previous))
    {
      throw MatchRecordError("checkpoint " + std::to_string(i) + " (tick " + std::to_string(checkpoints[i].tick)
                             + ") is out of order or beyond the last tick");
    }
    previous = checkpoints[i].tick;
  }

  // The body first: its length goes in the header.
  Writer body;
  if (rle)
  {
    size_t i = 0;
    while (i < ticks.size())
    {
      size_t run = 1;
      while (i + run < ticks.size() && run < MAX_RUN && sameTick(ticks[i], ticks[i + run]))
      {
        run++;
      }
      body.u16((uint16_t) run);
      writeTick(body, ticks[i], numPlayers);
      i += run;
    }
  }
  else
  {
    for (const TickInputs& t : ticks)
    {
      writeTick(body, t, numPlayers);
    }
  }

  Writer w;
  for (uint8_t m : MAGIC)
  {
    w.u8(m);
  }
  w.u16(formatVersion);
  w.u16((uint16_t) ((rle ? FLAG_RLE : 0) | (roundOver ? FLAG_ROUND_OVER : 0)));
  w.u32(stateHashVersion);
  w.u32(checkpointInterval);
  w.u64(buildId);
  w.u64(dataHash);
  w.u64(config.seed);
  w.u16((uint16_t) config.mapWidth);
  w.u16((uint16_t) config.mapHeight);
  w.u16((uint16_t) (int16_t) config.blockPercentage);
  w.u8(config.npcSpawnerEnabled ? 1 : 0);
  w.u8((uint8_t) numPlayers);
  for (const MatchConfig::Slot& s : config.slots)
  {
    checkRange(s.team, 0, 255, "a team number of");
    w.u8((uint8_t) s.team);
    w.u8(s.ai ? 1 : 0);
  }
  w.u32((uint32_t) ticks.size());
  w.u32((uint32_t) checkpoints.size());
  w.u32((uint32_t) body.size());
  w.bytes(body.data());
  for (const Checkpoint& c : checkpoints)
  {
    w.u32(c.tick);
    w.u64(c.hash);
  }
  return w.data();
}

MatchRecord MatchRecord::parse(const uint8_t* data, size_t size)
{
  Reader r(data, size);
  MatchRecord rec;

  uint8_t magic[4];
  for (int i = 0; i < 4; i++)
  {
    magic[i] = r.u8("the magic");
  }
  if (std::memcmp(magic, MAGIC, 4) != 0)
  {
    throw MatchRecordError("not a match record: the first four bytes are not \"BRMR\"");
  }
  rec.formatVersion = r.u16("the format version");
  if (rec.formatVersion != FORMAT_VERSION)
  {
    throw MatchRecordError("unsupported record format version " + std::to_string(rec.formatVersion)
                           + " (this build reads version " + std::to_string(FORMAT_VERSION) + ")");
  }
  const uint16_t flags = r.u16("the flags");
  if ((flags & ~KNOWN_FLAGS) != 0)
  {
    throw MatchRecordError("unknown flag bits 0x" + std::to_string(flags & ~KNOWN_FLAGS) + " in a version-1 record");
  }
  rec.rle = (flags & FLAG_RLE) != 0;
  rec.roundOver = (flags & FLAG_ROUND_OVER) != 0;
  rec.stateHashVersion = r.u32("the state hash version");
  rec.checkpointInterval = r.u32("the checkpoint interval");
  rec.buildId = r.u64("the build id");
  rec.dataHash = r.u64("the data hash");
  rec.config.seed = r.u64("the seed");
  rec.config.mapWidth = r.u16("the map width");
  rec.config.mapHeight = r.u16("the map height");
  rec.config.blockPercentage = (int16_t) r.u16("the block percentage");
  const uint8_t spawner = r.u8("the spawner flag");
  if (spawner > 1)
  {
    throw MatchRecordError("the spawner flag is " + std::to_string(spawner) + ", not 0 or 1");
  }
  rec.config.npcSpawnerEnabled = spawner == 1;
  const int numPlayers = r.u8("the player count");
  if (numPlayers > std::GameSettings::MAX_PLAYERS)
  {
    throw MatchRecordError("the record has " + std::to_string(numPlayers) + " players; at most "
                           + std::to_string(std::GameSettings::MAX_PLAYERS) + " are supported");
  }
  for (int i = 0; i < numPlayers; i++)
  {
    MatchConfig::Slot slot;
    slot.team = r.u8("a slot's team");
    const uint8_t ai = r.u8("a slot's AI flag");
    if (ai > 1)
    {
      throw MatchRecordError("slot " + std::to_string(i) + "'s AI flag is " + std::to_string(ai) + ", not 0 or 1");
    }
    slot.ai = ai == 1;
    rec.config.slots.push_back(slot);
  }

  const uint32_t numTicks = r.u32("the tick count");
  const uint32_t numCheckpoints = r.u32("the checkpoint count");
  const uint32_t bodyBytes = r.u32("the body length");
  if (numTicks > MAX_TICKS)
  {
    throw MatchRecordError("the record claims " + std::to_string(numTicks) + " ticks; at most "
                           + std::to_string(MAX_TICKS) + " are supported");
  }
  if (numCheckpoints > (uint64_t) numTicks + 1)
  {
    throw MatchRecordError("the record claims " + std::to_string(numCheckpoints) + " checkpoints for "
                           + std::to_string(numTicks) + " ticks");
  }
  const uint64_t expectedRest = (uint64_t) bodyBytes + (uint64_t) numCheckpoints * CHECKPOINT_BYTES;
  if (expectedRest > r.remaining())
  {
    throw MatchRecordError("record is truncated at " + hexOffset(r.offset()) + ": the body and checkpoints need "
                           + std::to_string(expectedRest) + " bytes, " + std::to_string(r.remaining()) + " are left");
  }
  if (expectedRest < r.remaining())
  {
    throw MatchRecordError("record has " + std::to_string(r.remaining() - expectedRest) + " unexpected bytes after its last checkpoint");
  }

  // The body, which must use exactly bodyBytes.
  const size_t bodyEnd = r.offset() + bodyBytes;
  rec.ticks.reserve(numTicks);
  if (rec.rle)
  {
    uint32_t total = 0;
    uint32_t lastRun = 0;
    while (total < numTicks)
    {
      const size_t at = r.offset();
      const uint32_t run = r.u16("a run length");
      if (run == 0 || run > numTicks - total)
      {
        throw MatchRecordError("bad run length " + std::to_string(run) + " at " + hexOffset(at) + " (" + std::to_string(numTicks - total)
                               + " ticks are still to come)");
      }
      TickInputs tick = readTick(r, numPlayers);
      if (!rec.ticks.empty() && sameTick(rec.ticks.back(), tick) && lastRun != MAX_RUN)
      {
        throw MatchRecordError("run at " + hexOffset(at) + " repeats the tick before it: runs must be maximal (a run is split only at "
                               + std::to_string(MAX_RUN) + ")");
      }
      rec.ticks.insert(rec.ticks.end(), run, tick);
      total += run;
      lastRun = run;
    }
  }
  else
  {
    for (uint32_t i = 0; i < numTicks; i++)
    {
      rec.ticks.push_back(readTick(r, numPlayers));
    }
  }
  if (r.offset() != bodyEnd)
  {
    throw MatchRecordError("the body is " + std::to_string(bodyBytes) + " bytes long but its ticks end at " + hexOffset(r.offset())
                           + ", not " + hexOffset(bodyEnd));
  }

  for (uint32_t i = 0; i < numCheckpoints; i++)
  {
    Checkpoint c;
    c.tick = r.u32("a checkpoint's tick");
    c.hash = r.u64("a checkpoint's hash");
    if (c.tick > numTicks || (i > 0 && c.tick <= rec.checkpoints.back().tick))
    {
      throw MatchRecordError("checkpoint " + std::to_string(i) + " (tick " + std::to_string(c.tick)
                             + ") is out of order or beyond the last tick (" + std::to_string(numTicks) + ")");
    }
    rec.checkpoints.push_back(c);
  }
  return rec;
}

MatchRecord MatchRecord::readFile(const std::string& path)
{
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in.is_open())
  {
    throw MatchRecordError("cannot open " + path);
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try
  {
    return parse(bytes);
  }
  catch (const MatchRecordError& e)
  {
    throw MatchRecordError(path + ": " + e.what());
  }
}

void MatchRecord::writeFile(const std::string& path) const
{
  const std::vector<uint8_t> bytes = serialize();
  std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!out.is_open())
  {
    throw MatchRecordError("cannot open " + path + " for writing");
  }
  out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize) bytes.size());
  out.flush();
  if (!out)
  {
    throw MatchRecordError("could not write all of " + path);
  }
}

bool MatchRecord::operator==(const MatchRecord& o) const
{
  if (formatVersion != o.formatVersion || rle != o.rle || roundOver != o.roundOver
      || stateHashVersion != o.stateHashVersion || checkpointInterval != o.checkpointInterval
      || buildId != o.buildId || dataHash != o.dataHash || !(config == o.config)
      || ticks.size() != o.ticks.size() || checkpoints != o.checkpoints)
  {
    return false;
  }
  for (size_t i = 0; i < ticks.size(); i++)
  {
    if (!sameTick(ticks[i], o.ticks[i]))
    {
      return false;
    }
  }
  return true;
}

// -- MatchRecorder -------------------------------------------------------------

MatchRecorder::MatchRecorder(uint32_t checkpointInterval, bool rle)
{
  m_record.checkpointInterval = checkpointInterval;
  m_record.rle = rle;
}

void MatchRecorder::checkpoint(const Simulation& sim)
{
  if (!m_record.checkpoints.empty() && m_record.checkpoints.back().tick == sim.tick())
  {
    return;
  }
  MatchRecord::Checkpoint c;
  c.tick = (uint32_t) sim.tick();
  c.hash = sim.stateHash();
  m_record.checkpoints.push_back(c);
}

void MatchRecorder::begin(const Simulation& sim)
{
  if (!sim.started() || sim.tick() != 0)
  {
    throw std::logic_error("MatchRecorder::begin: call it right after Simulation::start()");
  }
  const uint32_t interval = m_record.checkpointInterval;
  const bool rle = m_record.rle;
  m_record = MatchRecord();
  m_record.checkpointInterval = interval;
  m_record.rle = rle;
  m_record.stateHashVersion = (uint32_t) Simulation::STATE_HASH_VERSION;
  m_record.buildId = bitriotBuildId();
  m_record.dataHash = Simulation::dataHash();
  m_record.config = sim.config();
  m_active = true;
  checkpoint(sim);
}

void MatchRecorder::onStep(const TickInputs& inputs, const Simulation& sim)
{
  if (!m_active)
  {
    throw std::logic_error("MatchRecorder::onStep: call begin() first");
  }
  if (sim.tick() != m_record.ticks.size() + 1)
  {
    throw std::logic_error("MatchRecorder::onStep: the simulation is at tick " + std::to_string(sim.tick())
                           + " but the recorder holds " + std::to_string(m_record.ticks.size()) + " ticks");
  }
  if (m_record.ticks.size() >= MatchRecord::MAX_TICKS)
  {
    throw std::logic_error("MatchRecorder::onStep: a record holds at most " + std::to_string(MatchRecord::MAX_TICKS) + " ticks");
  }
  m_record.ticks.push_back(inputs);
  const uint32_t interval = m_record.checkpointInterval;
  if (interval != 0 && sim.tick() % interval == 0)
  {
    checkpoint(sim);
  }
}

const MatchRecord& MatchRecorder::finish(const Simulation& sim)
{
  if (!m_active)
  {
    throw std::logic_error("MatchRecorder::finish: call begin() first");
  }
  if (sim.tick() != m_record.ticks.size())
  {
    throw std::logic_error("MatchRecorder::finish: the simulation is at tick " + std::to_string(sim.tick())
                           + " but the recorder holds " + std::to_string(m_record.ticks.size()) + " ticks");
  }
  checkpoint(sim);
  m_record.roundOver = sim.roundOver();
  return m_record;
}
