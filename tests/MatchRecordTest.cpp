// MatchRecord: the byte format (brief 05). No round is played here: records are built
// by hand, so these tests are about the format alone. doco/netplay/match-record-format.md
// is the specification; the byte-level tests below are written against it, not against
// the implementation.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "MatchRecord.h"
#include "support/ScriptedInput.h"

namespace
{
using bitriot_test::ScriptRng;

MatchConfig smallConfig(int players = 3)
{
  MatchConfig c;
  c.seed = 0x1122334455667788ULL;
  c.mapWidth = 13;
  c.mapHeight = 11;
  c.blockPercentage = 55;
  c.npcSpawnerEnabled = true;
  c.slots.resize(players);
  for (int i = 0; i < players; i++)
  {
    c.slots[i].team = i;
    c.slots[i].ai = i != 0;
  }
  return c;
}

TickInputs tick(std::initializer_list<int> slotBytes, std::initializer_list<ControlEvent> events = {})
{
  TickInputs t;
  int i = 0;
  for (int b : slotBytes)
  {
    t.slot[i++] = PlayerInput((uint8_t) b);
  }
  for (const ControlEvent& e : events)
  {
    t.control.push_back(e);
  }
  return t;
}

MatchRecord sampleRecord(bool rle)
{
  MatchRecord r;
  r.rle = rle;
  r.roundOver = true;
  r.stateHashVersion = 2;
  r.checkpointInterval = 3;
  r.buildId = 0xAABBCCDDEEFF0011ULL;
  r.dataHash = 0x0102030405060708ULL;
  r.config = smallConfig();
  r.ticks.push_back(tick({}));
  r.ticks.push_back(tick({}));
  r.ticks.push_back(tick({}));
  r.ticks.push_back(tick({0x08, 0x00, 0x00}));
  r.ticks.push_back(tick({0x08, 0x00, 0x00}));
  r.ticks.push_back(tick({0x50, 0x05, 0x00}, { ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER) }));
  r.ticks.push_back(tick({0x40, 0x85, 0x00}, { ControlEvent(1, ControlEvent::RETURN_TO_AI), ControlEvent(2, ControlEvent::LEAVE) }));
  MatchRecord::Checkpoint c0, c3, c7;
  c0.tick = 0; c0.hash = 0xDEADBEEF00000001ULL;
  c3.tick = 3; c3.hash = 0xDEADBEEF00000002ULL;
  c7.tick = 7; c7.hash = 0xFFFFFFFFFFFFFFFFULL;
  r.checkpoints = { c0, c3, c7 };
  return r;
}

void expectParseFails(const std::vector<uint8_t>& bytes, const std::string& messagePart)
{
  try
  {
    MatchRecord::parse(bytes);
    ADD_FAILURE() << "parse accepted bytes that should be refused (" << messagePart << ")";
  }
  catch (const MatchRecordError& e)
  {
    EXPECT_NE(std::string(e.what()).find(messagePart), std::string::npos) << "message was: " << e.what();
  }
}
} // namespace

// ---------------------------------------------------------------------------
// The layout, byte by byte, written out by hand from the specification.
// ---------------------------------------------------------------------------

TEST(MatchRecordTest, TheBytesAreTheDocumentedLayout)
{
  MatchRecord r;
  r.rle = false;
  r.roundOver = true;
  r.stateHashVersion = 2;
  r.checkpointInterval = 30;
  r.buildId = 0x1112131415161718ULL;
  r.dataHash = 0x2122232425262728ULL;
  r.config.seed = 0x3132333435363738ULL;
  r.config.mapWidth = 0x0D0E;
  r.config.mapHeight = 0x0B0C;
  r.config.blockPercentage = -2; // i16
  r.config.npcSpawnerEnabled = true;
  r.config.slots.resize(2);
  r.config.slots[0].team = 5;
  r.config.slots[0].ai = false;
  r.config.slots[1].team = 7;
  r.config.slots[1].ai = true;
  r.ticks.push_back(tick({0x11, 0x22}));
  r.ticks.push_back(tick({0x33, 0x44}, { ControlEvent(1, ControlEvent::LEAVE) }));
  MatchRecord::Checkpoint cp;
  cp.tick = 2;
  cp.hash = 0x4142434445464748ULL;
  r.checkpoints = { cp };

  const std::vector<uint8_t> expected = {
    'B', 'R', 'M', 'R',                                  // magic
    0x01, 0x00,                                          // format version 1
    0x02, 0x00,                                          // flags: ROUND_OVER (bit 1), no RLE
    0x02, 0x00, 0x00, 0x00,                              // state hash version 2
    0x1E, 0x00, 0x00, 0x00,                              // checkpoint interval 30
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,      // build id, little-endian
    0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,      // data hash
    0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,      // seed
    0x0E, 0x0D,                                          // map width
    0x0C, 0x0B,                                          // map height
    0xFE, 0xFF,                                          // block percentage -2 (i16)
    0x01,                                                // spawner on
    0x02,                                                // two players
    0x05, 0x00,                                          // slot 0: team 5, human
    0x07, 0x01,                                          // slot 1: team 7, AI
    0x02, 0x00, 0x00, 0x00,                              // two ticks
    0x01, 0x00, 0x00, 0x00,                              // one checkpoint
    0x08, 0x00, 0x00, 0x00,                              // the body is 8 bytes
    0x11, 0x22, 0x00,                                    // tick 1: two input bytes, no events
    0x33, 0x44, 0x01, 0x01, 0x02,                        // tick 2: two bytes, one event (slot 1, kind LEAVE = 2)
    0x02, 0x00, 0x00, 0x00,                              // checkpoint: tick 2
    0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41       // checkpoint: hash
  };
  EXPECT_EQ(r.serialize(), expected);
  EXPECT_EQ(MatchRecord::parse(expected), r);
}

TEST(MatchRecordTest, RunLengthEncodingIsALengthAndOneTickPerRun)
{
  MatchRecord r;
  r.rle = true;
  r.config = smallConfig(2);
  for (int i = 0; i < 5; i++) { r.ticks.push_back(tick({0x08, 0x00})); }
  r.ticks.push_back(tick({0x00, 0x00}));
  const std::vector<uint8_t> bytes = r.serialize();
  const size_t headerBytes = 4 + 2 + 2 + 4 + 4 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 2 * 2 + 4 + 4 + 4;
  const std::vector<uint8_t> body(bytes.begin() + headerBytes, bytes.end());
  const std::vector<uint8_t> expected = {
    0x05, 0x00, 0x08, 0x00, 0x00, // run of 5 ticks: u16 count, then the tick
    0x01, 0x00, 0x00, 0x00, 0x00  // run of 1
  };
  EXPECT_EQ(body, expected);
  EXPECT_EQ(bytes[6] & 1, 1) << "the RLE flag";
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST(MatchRecordTest, SerialisationIsByteIdenticalAfterARoundTrip)
{
  for (bool rle : { false, true })
  {
    const MatchRecord r = sampleRecord(rle);
    const std::vector<uint8_t> bytes = r.serialize();
    const MatchRecord back = MatchRecord::parse(bytes);
    EXPECT_EQ(back, r) << "rle=" << rle;
    EXPECT_EQ(back.serialize(), bytes) << "rle=" << rle;
    EXPECT_EQ(back.rle, rle);
  }
}

TEST(MatchRecordTest, RleAndPlainDescribeTheSameRound)
{
  MatchRecord plain = sampleRecord(false);
  MatchRecord packed = plain;
  packed.rle = true;
  const MatchRecord a = MatchRecord::parse(plain.serialize());
  const MatchRecord b = MatchRecord::parse(packed.serialize());
  EXPECT_EQ(a.ticks.size(), b.ticks.size());
  for (size_t i = 0; i < a.ticks.size(); i++)
  {
    EXPECT_EQ(a.ticks[i].slot, b.ticks[i].slot) << "tick " << i + 1;
    EXPECT_EQ(a.ticks[i].control, b.ticks[i].control) << "tick " << i + 1;
  }
  EXPECT_LT(packed.serialize().size(), plain.serialize().size()) << "the repeated ticks should shrink";
}

TEST(MatchRecordTest, ARandomRecordRoundTripsInBothEncodings)
{
  ScriptRng rng(99);
  for (int trial = 0; trial < 40; trial++)
  {
    MatchRecord r;
    r.config = smallConfig(2 + (int) rng.below(5));
    r.buildId = rng.next();
    r.dataHash = ((uint64_t) rng.next() << 32) | rng.next();
    r.stateHashVersion = rng.below(5);
    r.checkpointInterval = rng.below(100);
    r.roundOver = rng.below(2) == 0;
    const int ticks = (int) rng.below(300);
    TickInputs current;
    for (int t = 0; t < ticks; t++)
    {
      if (rng.below(4) == 0) // runs of identical ticks, and changes
      {
        current = TickInputs();
        for (int s = 0; s < r.config.numPlayers(); s++)
        {
          current.slot[s] = PlayerInput((uint8_t) (rng.below(3) == 0 ? rng.below(256) : 0));
        }
        for (uint32_t e = rng.below(3) == 0 ? rng.below(4) : 0; e > 0; e--)
        {
          current.control.push_back(ControlEvent((int) rng.below(r.config.numPlayers()), (ControlEvent::Kind) rng.below(3)));
        }
      }
      r.ticks.push_back(current);
    }
    for (int t = 0; t <= ticks; t++)
    {
      if (rng.below(7) == 0)
      {
        MatchRecord::Checkpoint c;
        c.tick = (uint32_t) t;
        c.hash = ((uint64_t) rng.next() << 32) | rng.next();
        r.checkpoints.push_back(c);
      }
    }
    for (bool rle : { false, true })
    {
      r.rle = rle;
      const std::vector<uint8_t> bytes = r.serialize();
      const MatchRecord back = MatchRecord::parse(bytes);
      ASSERT_EQ(back, r) << "trial " << trial << " rle=" << rle;
      ASSERT_EQ(back.serialize(), bytes) << "trial " << trial << " rle=" << rle;
    }
  }
}

TEST(MatchRecordTest, AnEmptyRecordIsValid)
{
  MatchRecord r;
  r.config = smallConfig(2);
  for (bool rle : { false, true })
  {
    r.rle = rle;
    const MatchRecord back = MatchRecord::parse(r.serialize());
    EXPECT_EQ(back, r);
    EXPECT_TRUE(back.ticks.empty());
  }
}

TEST(MatchRecordTest, ARunLongerThanTheLengthFieldIsSplitAndStillRoundTrips)
{
  MatchRecord r;
  r.config = smallConfig(2);
  r.rle = true;
  r.ticks.assign(65535 + 65535 + 7, tick({0x08, 0x00}));
  const std::vector<uint8_t> bytes = r.serialize();
  EXPECT_LT(bytes.size(), 200u) << "three runs, not one tick each";
  const MatchRecord back = MatchRecord::parse(bytes);
  EXPECT_EQ(back, r);
  EXPECT_EQ(back.serialize(), bytes);
}

TEST(MatchRecordTest, FilesRoundTripToo)
{
  const MatchRecord r = sampleRecord(true);
  const std::string path = ::testing::TempDir() + "bitriot_matchrecord_roundtrip.brmr";
  r.writeFile(path);
  EXPECT_EQ(MatchRecord::readFile(path), r);
  EXPECT_THROW(MatchRecord::readFile(path + ".missing"), MatchRecordError);
  EXPECT_THROW(r.writeFile(::testing::TempDir() + "no_such_dir/x.brmr"), MatchRecordError);
  std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// What a reader refuses
// ---------------------------------------------------------------------------

TEST(MatchRecordTest, EveryTruncationIsRefusedAndNeverCrashes)
{
  for (bool rle : { false, true })
  {
    const std::vector<uint8_t> bytes = sampleRecord(rle).serialize();
    for (size_t keep = 0; keep < bytes.size(); keep++)
    {
      const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + keep);
      EXPECT_THROW(MatchRecord::parse(cut), MatchRecordError) << "kept " << keep << " of " << bytes.size() << " bytes, rle=" << rle;
    }
  }
}

TEST(MatchRecordTest, TrailingBytesAreRefused)
{
  std::vector<uint8_t> bytes = sampleRecord(false).serialize();
  bytes.push_back(0);
  expectParseFails(bytes, "unexpected bytes");
}

TEST(MatchRecordTest, TheHeaderIsChecked)
{
  const std::vector<uint8_t> good = sampleRecord(false).serialize();
  {
    std::vector<uint8_t> b = good;
    b[0] = 'X';
    expectParseFails(b, "not a match record");
  }
  {
    std::vector<uint8_t> b = good;
    b[4] = 2; // version 2
    expectParseFails(b, "unsupported record format version 2");
  }
  {
    std::vector<uint8_t> b = good;
    b[6] |= 0x04; // an unknown flag
    expectParseFails(b, "unknown flag");
  }
  {
    std::vector<uint8_t> b = good;
    b[46] = 2; // spawner byte
    expectParseFails(b, "spawner flag");
  }
  {
    std::vector<uint8_t> b = good;
    b[47] = 7; // seven players
    expectParseFails(b, "7 players");
  }
  {
    std::vector<uint8_t> b = good;
    b[49] = 2; // slot 0's AI flag
    expectParseFails(b, "AI flag");
  }
}

TEST(MatchRecordTest, CountsThatCannotBeTrueAreRefusedBeforeAnythingIsAllocated)
{
  MatchRecord r;
  r.config = smallConfig(2);
  r.rle = true;
  r.ticks.assign(10, tick({0, 0}));
  std::vector<uint8_t> bytes = r.serialize();
  const size_t ticksAt = 4 + 2 + 2 + 4 + 4 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 2 * 2; // the tick count field
  {
    std::vector<uint8_t> b = bytes;
    b[ticksAt + 3] = 0x7F; // two billion ticks
    expectParseFails(b, "ticks; at most");
  }
  {
    std::vector<uint8_t> b = bytes;
    b[ticksAt + 4] = 0xFF; // far more checkpoints than ticks
    expectParseFails(b, "checkpoints for");
  }
  {
    std::vector<uint8_t> b = bytes;
    b[ticksAt + 8] = 0xFF; // body length far past the end
    b[ticksAt + 9] = 0xFF;
    expectParseFails(b, "truncated");
  }
}

TEST(MatchRecordTest, RunsMustBeWellFormedAndMaximal)
{
  MatchRecord r;
  r.config = smallConfig(2);
  r.rle = true;
  r.ticks.assign(4, tick({0x08, 0x00}));
  r.ticks.push_back(tick({0x00, 0x00}));
  std::vector<uint8_t> bytes = r.serialize();
  const size_t body = 4 + 2 + 2 + 4 + 4 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 2 * 2 + 12;
  ASSERT_EQ(bytes[body], 4); // the first run's length
  {
    std::vector<uint8_t> b = bytes;
    b[body] = 0; // a run of zero
    expectParseFails(b, "bad run length");
  }
  {
    std::vector<uint8_t> b = bytes;
    b[body] = 40; // longer than the ticks left
    expectParseFails(b, "bad run length");
  }
  {
    // 3 + 1 + 1: the first two runs hold the same tick, so they should have been one.
    std::vector<uint8_t> b = bytes;
    b[body] = 3;
    const std::vector<uint8_t> extra = { 0x01, 0x00, 0x08, 0x00, 0x00 };
    b.insert(b.begin() + body + 5, extra.begin(), extra.end());
    // the body is five bytes longer now
    b[body - 4] = (uint8_t) (b[body - 4] + 5);
    expectParseFails(b, "must be maximal");
  }
}

TEST(MatchRecordTest, CheckpointsMustBeInOrderAndInsideTheRecord)
{
  for (int variant = 0; variant < 3; variant++)
  {
    MatchRecord r = sampleRecord(false);
    MatchRecord::Checkpoint c;
    if (variant == 0) { c.tick = 3; }        // equal to the one before it
    if (variant == 1) { c.tick = 2; }        // before it
    if (variant == 2) { c.tick = 8; }        // beyond the last tick (7)
    r.checkpoints.push_back(c);
    EXPECT_THROW(r.serialize(), MatchRecordError) << "variant " << variant;
  }
  // and on the read side: patch a valid file's last checkpoint tick to 99
  std::vector<uint8_t> bytes = sampleRecord(false).serialize();
  bytes[bytes.size() - 12] = 99;
  expectParseFails(bytes, "out of order or beyond");
  // ... or to a tick the checkpoint before it already has, or an earlier one
  // (the checkpoints are at ticks 0, 3 and 7)
  bytes = sampleRecord(false).serialize();
  bytes[bytes.size() - 12] = 3;
  expectParseFails(bytes, "out of order or beyond");
  bytes[bytes.size() - 12] = 2;
  expectParseFails(bytes, "out of order or beyond");
}

TEST(MatchRecordTest, WhatTheFormatCannotHoldIsRefusedOnWrite)
{
  {
    MatchRecord r = sampleRecord(false);
    r.config.slots[1].team = 256;
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.config.slots[1].team = -1;
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.config.mapWidth = 70000;
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.config.blockPercentage = 40000;
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.config.slots.resize(7);
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.ticks[0].slot[5] = PlayerInput(1); // a slot the 3-player config does not have
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    for (int i = 0; i < 256; i++) { r.ticks[0].control.push_back(ControlEvent(0, ControlEvent::LEAVE)); }
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.ticks[0].control.push_back(ControlEvent(300, ControlEvent::LEAVE));
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
  {
    MatchRecord r = sampleRecord(false);
    r.checkpoints.back().tick = 99; // beyond the last tick
    EXPECT_THROW(r.serialize(), MatchRecordError);
  }
}

TEST(MatchRecordTest, DamagedFilesNeverCrashAndNeverChangeMeaningSilently)
{
  // Flip every bit of a valid record, one at a time. A reader either refuses the file
  // or reads something that writes back to exactly the damaged bytes (the format has no
  // redundant encodings), and never crashes.
  for (bool rle : { false, true })
  {
    const std::vector<uint8_t> good = sampleRecord(rle).serialize();
    int refused = 0;
    int accepted = 0;
    for (size_t byte = 0; byte < good.size(); byte++)
    {
      for (int bit = 0; bit < 8; bit++)
      {
        std::vector<uint8_t> damaged = good;
        damaged[byte] = (uint8_t) (damaged[byte] ^ (1 << bit));
        try
        {
          const MatchRecord r = MatchRecord::parse(damaged);
          accepted++;
          EXPECT_EQ(r.serialize(), damaged) << "byte " << byte << " bit " << bit << " rle=" << rle;
        }
        catch (const MatchRecordError&)
        {
          refused++;
        }
      }
    }
    EXPECT_GT(refused, 100) << "rle=" << rle;
    EXPECT_GT(accepted, 100) << "most bit flips in the payload are still a well-formed record (rle=" << rle << ")";
  }
}
