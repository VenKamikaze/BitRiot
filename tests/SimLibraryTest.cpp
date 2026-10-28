// The simulation library (brief 03b): bitriot_sim has no SDL, no bitmaps, no UI.
//
// This file is compiled into both test executables. bitriot_sim_tests links the
// simulation and GoogleTest only, so every round any test plays there already
// proves the simulation runs with nothing else; the tests below pin down the rest
// of the claim from other directions:
//
//   * the text of the simulation names no SDL, RmlUi or OpenGL header and uses no
//     SDL type (a source scan over the bitriot_sim target's files and the headers
//     they include);
//   * the compiled archive holds no floating-point instruction (an object-level
//     check, which sees what the text scan cannot: an `auto` carrying a double, an
//     int converted at a float parameter);
//   * the sim-only executable really has no SDL or RmlUi library loaded;
//   * the seams that replaced the SDL calls behave: sound goes to a silent default
//     when nothing is installed, rumble goes through a sink, and the animation
//     frame counts come from a table.

#include "support/HeadlessGame.h"
#include "support/SimSources.h"
#include "support/SourceScan.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#if defined(__linux__)
#include <link.h>
#endif

#include "EntityAnim.h"
#include "GameSound.h"
#include "MatchConfig.h"
#include "MickBaseSound.h"
#include "RumbleSink.h"

using bitriot_test::HeadlessEnvironment;
using bitriot_test::HeadlessGameTest;
using bitriot_test::NullSound;

namespace bitriot_test
{
namespace
{

// ---------------------------------------------------------------------------
// No SDL, RmlUi or OpenGL in the simulation's source
// ---------------------------------------------------------------------------

// What in `source` ties it to SDL or the UI: an #include of an SDL / SDL_mixer /
// SDL_ttf / SDL_image / RmlUi / OpenGL header, or of one of the project's own
// render, device and UI headers; or use of an SDL type or function. Comments and
// strings are ignored. Each hit is the offending text.
std::vector<std::string> sdlOrUiUse(const std::string& source)
{
  static const std::regex forbiddenInclude(
    R"((SDL|Rml|OpenGL|GLES|^GL/|^gl[a-z]*\.h$|EntityRenderer|MapRenderer|WorldRenderer|InfoPanel|InputHandler|)"
    R"(GameEngine|FrameLoop|TickPacer|MenuSDL|MickSDL|GameRunningSDL))");
  static const std::regex forbiddenToken(
    R"((^|[^A-Za-z0-9_])(SDL_[A-Za-z0-9_]+|Mix_[A-Za-z0-9_]+|TTF_[A-Za-z0-9_]+|IMG_[A-Za-z0-9_]+|Rml::[A-Za-z0-9_]+|)"
    R"(Uint(8|16|32|64)|Sint(8|16|32|64))([^A-Za-z0-9_]|$))");

  std::vector<std::string> hits;
  // An include's path is a string literal, so read it from the text as written. A
  // line that is commented out does not start with #include and is not seen.
  for (const std::string& target : includeTargets(source))
  {
    if (std::regex_search(target, forbiddenInclude))
    {
      hits.push_back("#include " + target);
    }
  }
  std::istringstream lines(stripCommentsAndStrings(source));
  std::string line;
  while (std::getline(lines, line))
  {
    std::smatch m;
    if (trimmed(line).compare(0, 1, "#") == 0)
    {
      continue; // preprocessor lines: includes are judged above, as written
    }
    if (std::regex_search(line, m, forbiddenToken))
    {
      hits.push_back(trimmed(line));
    }
  }
  return hits;
}

TEST(SimLibraryTest, TheSimulationSourcesNameNoSdlRmlUiOrOpenGL)
{
  namespace fs = std::filesystem;
  const std::vector<fs::path> sources = simSourceFiles();
  const std::set<fs::path> files = simFileClosure();
  ASSERT_GT(sources.size(), 35u) << "the bitriot_sim source list is missing: " << BITRIOT_SIM_SOURCES_FILE;
  EXPECT_GT(files.size(), sources.size());

  const fs::path srcDir = fs::path(BITRIOT_SOURCE_DIR) / "src";
  for (const fs::path& file : files)
  {
    for (const std::string& hit : sdlOrUiUse(readFile(file)))
    {
      ADD_FAILURE() << fs::relative(file, srcDir).generic_string() << " ties the simulation to SDL or the UI: " << hit
                    << "\n  bitriot_sim has no SDL (brief 03b). Draw or read a device in bitriot_core, "
                       "and reach the simulation through a hook such as RumbleSink or MickBaseSound.";
    }
  }
}

TEST(SimLibraryTest, TheSourceScanCatchesSdlAndLeavesLookalikesAlone)
{
  EXPECT_EQ(sdlOrUiUse("#include <SDL2/SDL.h>").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"SDL_joystick.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include <SDL2/SDL_mixer.h>").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include <RmlUi/Core.h>").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include <GL/gl.h>").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"MickSDLUtil.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"EntityRendererFactory.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"MapRenderer.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"InfoPanel.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("#include \"FrameLoop.h\"").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("SDL_Rect r;").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("x = SDL_GetTicks();").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("Uint32 pixel;").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("Sint32 id;").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("Mix_Chunk* c;").size(), 1u);
  EXPECT_EQ(sdlOrUiUse("Rml::Context* c;").size(), 1u);

  EXPECT_EQ(sdlOrUiUse("#include \"Map.h\"").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("#include <string>").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("#include \"EntityAnim.h\"").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("#include \"RumbleSink.h\"").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("#include \"MickBaseSound.h\"").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("// SDL_Rect is gone").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("/* #include <SDL2/SDL.h> */").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("log(\"SDL_Init failed\");").size(), 0u);
  EXPECT_EQ(sdlOrUiUse("int MY_SDL_FLAG; int Uint32ish;").size(), 0u);
}

// ---------------------------------------------------------------------------
// No floating-point instruction in the compiled simulation
// ---------------------------------------------------------------------------

// The offending instructions in `disassembly` (objdump -d output): scalar and
// packed floating-point arithmetic, conversions and compares, scalar float moves,
// and x87. Each hit is "function: mnemonic". Plain 128-bit moves (movaps, movups,
// movdqa: used to copy structs) and pxor (zeroing) are integer-safe and ignored.
//
// Written for x86-64 and aarch64, the two architectures this project builds on;
// `arch` is CMAKE_SYSTEM_PROCESSOR. Anything else is refused rather than waved
// through, because a check that cannot read the machine code proves nothing.
std::vector<std::string> floatInstructions(const std::string& disassembly, const std::string& arch, bool& supported)
{
  supported = true;
  std::regex mnemonicOfInterest;
  if (arch == "x86_64" || arch == "AMD64" || arch == "amd64")
  {
    // SSE/AVX: addss, mulpd, divsd, sqrtss, ucomisd, cvtsi2sd, cvttss2si, movss,
    // roundsd, minps ...; and x87 (every x87 mnemonic starts with f).
    mnemonicOfInterest = std::regex(
      R"(^(v?(add|sub|mul|div|sqrt|min|max|cmp|ucomi|comi|round|rcp|rsqrt|hadd|hsub|dp|and|andn|or|xor|blend|shuf|unpck|mov)(s|p)(s|d)|)"
      R"(v?cvt[a-z0-9]*|v?f[a-z0-9]+)$)");
  }
  else if (arch == "aarch64" || arch == "arm64" || arch == "ARM64")
  {
    mnemonicOfInterest = std::regex(R"(^(f[a-z0-9]+|scvtf|ucvtf)$)");
  }
  else
  {
    supported = false;
    return {};
  }

  // Integer-safe lookalikes that the patterns above would otherwise catch.
  static const std::set<std::string> ignored =
  {
    "movaps", "movups", "movapd", "movupd", "andps", "andnps", "orps", "xorps", "andpd", "andnpd", "orpd", "xorpd",
    "unpcklps", "unpckhps", "unpcklpd", "unpckhpd", "shufps", "shufpd"
  };
  static const std::set<std::string> prefixes =
  {
    "rep", "repz", "repnz", "repe", "repne", "lock", "notrack", "bnd", "data16", "cs", "ds", "es", "ss",
    "xacquire", "xrelease"
  };

  std::vector<std::string> hits;
  std::string function = "?";
  std::istringstream lines(disassembly);
  std::string line;
  const std::regex functionHeader(R"(^[0-9a-f]+ <(.*)>:$)");
  const std::regex instruction(R"(^\s+[0-9a-f]+:\s+(.*)$)");
  while (std::getline(lines, line))
  {
    std::smatch m;
    if (std::regex_match(line, m, functionHeader))
    {
      function = m[1].str();
      continue;
    }
    if (!std::regex_match(line, m, instruction))
    {
      continue;
    }
    std::istringstream words(m[1].str());
    std::string mnemonic;
    words >> mnemonic;
    while (prefixes.count(mnemonic) && (words >> mnemonic))
    {
    }
    if (mnemonic.empty() || ignored.count(mnemonic))
    {
      continue;
    }
    if (std::regex_match(mnemonic, mnemonicOfInterest))
    {
      hits.push_back(function + ": " + mnemonic);
    }
  }
  return hits;
}

TEST(SimLibraryTest, TheInstructionScanFlagsFloatingPointAndLeavesIntegerCodeAlone)
{
  const std::string x86 =
    "0000000000000000 <_ZN4Test3fooEi>:\n"
    "   0:\tmovss  0x0(%rip),%xmm0\n"
    "   8:\tcvtsi2sd %eax,%xmm0\n"
    "   c:\tdivss  %xmm1,%xmm0\n"
    "  10:\tpxor   %xmm0,%xmm0\n"
    "  14:\tmovaps %xmm0,0x0(%rip)\n"
    "  1b:\tmovdqu %xmm0,(%rax)\n"
    "  1f:\tfldl   -0x8(%rbp)\n"
    "  22:\tadd    %ecx,%eax\n"
    "  24:\trep movsl %ds:(%rsi),%es:(%rdi)\n"
    "  26:\tmovsd  %xmm0,(%rax)\n"
    "  2a:\tsqrtsd %xmm0,%xmm0\n"
    "  2e:\tucomisd %xmm1,%xmm0\n"
    "  32:\tcvttsd2si %xmm0,%eax\n"
    "  36:\tmovslq %eax,%rdx\n"
    "  38:\tmovzbl (%rax),%eax\n"
    "  3b:\tcs nopw 0x0(%rax,%rax,1)\n";
  bool supported = false;
  const std::vector<std::string> hits = floatInstructions(x86, "x86_64", supported);
  ASSERT_TRUE(supported);
  const std::set<std::string> got(hits.begin(), hits.end());
  const std::set<std::string> want =
  {
    "_ZN4Test3fooEi: movss", "_ZN4Test3fooEi: cvtsi2sd", "_ZN4Test3fooEi: divss", "_ZN4Test3fooEi: fldl",
    "_ZN4Test3fooEi: movsd", "_ZN4Test3fooEi: sqrtsd", "_ZN4Test3fooEi: ucomisd", "_ZN4Test3fooEi: cvttsd2si"
  };
  EXPECT_EQ(got, want);

  const std::string arm =
    "0000000000000000 <foo>:\n"
    "   0:\tldr\tw0, [x0]\n"
    "   4:\tfadd\ts0, s0, s0\n"
    "   8:\tscvtf\ts0, w1\n"
    "   c:\tadd\tw0, w0, w1\n";
  const std::vector<std::string> armHits = floatInstructions(arm, "aarch64", supported);
  ASSERT_TRUE(supported);
  EXPECT_EQ(armHits.size(), 2u);

  floatInstructions(x86, "riscv64", supported);
  EXPECT_FALSE(supported) << "an architecture the scan cannot read must be refused, not passed";
}

TEST(SimLibraryTest, TheCompiledSimulationHoldsNoFloatingPointInstruction)
{
  // Disassemble every object in the bitriot_sim archive. Needs binutils' objdump,
  // which is always present where a compiler is; if it is not, this fails rather
  // than passes unchecked.
  const std::string command = std::string("objdump -d --no-show-raw-insn \"") + BITRIOT_SIM_LIBRARY + "\" 2>&1";
  FILE* pipe = popen(command.c_str(), "r");
  ASSERT_NE(pipe, nullptr) << command;
  std::string output;
  char buffer[65536];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
  {
    output.append(buffer, n);
  }
  const int status = pclose(pipe);
  ASSERT_EQ(status, 0) << "objdump failed (is binutils installed?): " << command << "\n" << output.substr(0, 500);
  ASSERT_GT(output.size(), 100000u) << "suspiciously little machine code to check in " << BITRIOT_SIM_LIBRARY;

  bool supported = false;
  const std::vector<std::string> hits = floatInstructions(output, BITRIOT_COMPILER_TARGET_ARCH, supported);
  ASSERT_TRUE(supported) << "no floating-point instruction patterns for the architecture '"
                         << BITRIOT_COMPILER_TARGET_ARCH << "'; add them to floatInstructions() in tests/SimLibraryTest.cpp";

  std::map<std::string, int> perFunction;
  for (const std::string& hit : hits)
  {
    perFunction[hit]++;
  }
  int shown = 0;
  for (const auto& entry : perFunction)
  {
    if (shown++ == 20)
    {
      break;
    }
    ADD_FAILURE() << "floating-point instruction in bitriot_sim, " << entry.first << " (x" << entry.second << ")"
                  << "\n  The simulation must use fixed_t (src/Fixed.h). If the source shows no float, look for an "
                     "`auto` carrying a double or an int passed to a float parameter.";
  }
  EXPECT_TRUE(hits.empty()) << hits.size() << " floating-point instructions in the compiled simulation";
}

// ---------------------------------------------------------------------------
// The simulation-only executable really has no SDL behind it
// ---------------------------------------------------------------------------

TEST(SimLibraryTest, TheSimulationOnlyExecutableHasNoSdlOrUiLibraryLoaded)
{
#if defined(__linux__)
  std::vector<std::string> loaded;
  dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* data) -> int
  {
    static_cast<std::vector<std::string>*>(data)->push_back(info->dlpi_name ? info->dlpi_name : "");
    return 0;
  }, &loaded);
  ASSERT_GT(loaded.size(), 2u);

  std::vector<std::string> sdlOrUi;
  for (const std::string& name : loaded)
  {
    if (name.find("SDL") != std::string::npos || name.find("libRml") != std::string::npos
        || name.find("libGL") != std::string::npos || name.find("freetype") != std::string::npos)
    {
      sdlOrUi.push_back(name);
    }
  }

  if (linkedWithCore())
  {
    // bitriot_tests links the whole game; this just shows the check can see SDL.
    EXPECT_FALSE(sdlOrUi.empty()) << "the full executable should have SDL loaded; the check is blind";
  }
  else
  {
    for (const std::string& name : sdlOrUi)
    {
      ADD_FAILURE() << "bitriot_sim_tests has a library loaded that the simulation must not need: " << name;
    }
  }
#else
  GTEST_SUCCEED() << "loaded-library check is Linux only; the sim-only link and the source scans still apply";
#endif
}

class SimLibraryRoundTest : public HeadlessGameTest
{
};

TEST_F(SimLibraryRoundTest, AFullRoundWithBlocksRunsToItsEndWithNoBitmapsAndNoSurface)
{
  configurePlayers(2);
  setMapSize(13, 11);
  setBlockPercentage(55);
  startRound();

  ASSERT_TRUE(sim().started());
  int blocks = 0;
  for (int x = 0; x < Map::MAP_WIDTH; x++)
  {
    for (int y = 0; y < Map::MAP_HEIGHT; y++)
    {
      blocks += Map::getInstance()->staticTileAt(x, y) == Map::BLOCK ? 1 : 0;
    }
  }
  EXPECT_GT(blocks, 5) << "a real round has blocks (about 25 on this map at 55%; the bound is loose on purpose)";

  int ticks = 0;
  while (!sim().roundOver() && ticks < 60000)
  {
    sim().step();
    ticks++;
  }
  EXPECT_TRUE(sim().roundOver()) << "the round did not end in " << ticks << " ticks";
  EXPECT_GT(sim().roundEndedAtTick(), 60u);
}

// ---------------------------------------------------------------------------
// The seams that replaced the SDL calls
// ---------------------------------------------------------------------------

// Puts the global recording backend back, whatever a test did to the registration.
struct SoundBackendRestorer
{
  ~SoundBackendRestorer() { HeadlessEnvironment::sound()->makeCurrent(); }
};

class SoundSeamTest : public HeadlessGameTest
{
};

TEST_F(SoundSeamTest, WithNoSoundBackendInstalledSoundGoesToASilentDefault)
{
  SoundBackendRestorer restore;

  uintptr_t gone = 0;
  {
    NullSound temporary; // registers itself, replacing the harness's backend
    ASSERT_EQ(MickBaseSound::getInstance(), &temporary);
    gone = reinterpret_cast<uintptr_t>(&temporary);
  }

  // The backend was destroyed: it must no longer be the registered one, and the
  // simulation must still have something to talk to.
  MickBaseSound* now = MickBaseSound::getInstance();
  ASSERT_NE(now, nullptr) << "with no backend the simulation would dereference null";
  ASSERT_NE(reinterpret_cast<uintptr_t>(now), gone) << "a destroyed backend is still registered";
  EXPECT_NO_THROW(now->playSound("assets/audio/nothing.wav"));
  EXPECT_NO_THROW(std::GameSound::playSound(std::GameSound::BOOM));

  // And installing one again works as before.
  HeadlessEnvironment::sound()->makeCurrent();
  EXPECT_EQ(MickBaseSound::getInstance(), HeadlessEnvironment::sound());
}

class RecordingRumbleSink : public RumbleSink
{
  public:
    struct Call { int controller; int strength; int ms; };
    void rumble(int controllerId, int strengthPermille, int durationMs) override
    {
      calls.push_back({controllerId, strengthPermille, durationMs});
    }
    std::vector<Call> calls;
};

struct RumbleSinkRestorer
{
  ~RumbleSinkRestorer() { RumbleSink::setInstance(nullptr); }
};

class RumbleSinkTest : public HeadlessGameTest
{
  protected:
    void SetUp() override
    {
      HeadlessGameTest::SetUp();
      configurePlayers(2);
      startRound();
    }
};

TEST_F(RumbleSinkTest, ARumbleFromAHitGoesThroughTheInstalledSink)
{
  RumbleSinkRestorer restore;
  RecordingRumbleSink sink;
  RumbleSink::setInstance(&sink);

  auto p = player(1);
  p->m_controlledByAI = false;
  p->attachedController = 3;
  p->onCollision(EXPLOSION, EXPLOSION_GROUP, 0);

  ASSERT_EQ(sink.calls.size(), 1u);
  EXPECT_EQ(sink.calls[0].controller, 3);
  EXPECT_EQ(sink.calls[0].strength, 500);
  EXPECT_EQ(sink.calls[0].ms, 200);
}

TEST_F(RumbleSinkTest, NoRumbleForABotOrAPlayerWithNoController)
{
  RumbleSinkRestorer restore;
  RecordingRumbleSink sink;
  RumbleSink::setInstance(&sink);

  auto bot = player(1);
  bot->m_controlledByAI = true;
  bot->attachedController = 3;
  bot->onCollision(EXPLOSION, EXPLOSION_GROUP, 0);

  auto human = player(2);
  human->m_controlledByAI = false;
  human->attachedController = -1;
  human->onCollision(EXPLOSION, EXPLOSION_GROUP, 0);

  EXPECT_TRUE(sink.calls.empty());
}

TEST_F(RumbleSinkTest, WithNoSinkInstalledAHitIsDroppedNotACrash)
{
  RumbleSink::setInstance(nullptr);
  ASSERT_NE(RumbleSink::getInstance(), nullptr);

  auto p = player(1);
  p->m_controlledByAI = false;
  p->attachedController = 3;
  EXPECT_NO_THROW(p->onCollision(EXPLOSION, EXPLOSION_GROUP, 0));
}

class EntityAnimTest : public HeadlessGameTest
{
};

TEST_F(EntityAnimTest, EveryTypeHasAtLeastOneFrameAndTheTableCoversTheEnum)
{
  for (int t = 0; t < NUM_ENTITY_TYPES; t++)
  {
    EXPECT_GE(EntityAnim::frameCount(static_cast<EntityType>(t)), 1) << "type " << t;
  }
  EXPECT_EQ(EntityAnim::frameCount(static_cast<EntityType>(-1)), 1);
  EXPECT_EQ(EntityAnim::frameCount(static_cast<EntityType>(NUM_ENTITY_TYPES)), 1);
}

TEST_F(EntityAnimTest, TheFrameCountsAreTheShippedSpriteSheets)
{
  // Widths of assets/bitmaps/*.bmp divided by 32. (AnimFrameTableTest, in the full
  // executable, checks the same against the loaded bitmaps.)
  const std::map<EntityType, int> expected =
  {
    {BLOCK, 1}, {EGG1, 2}, {EGG2, 2}, {EGG3, 2}, {EGG4, 2}, {EGG5, 2}, {ENPLUS_POWERUP, 1}, {IQPLUS_POWERUP, 1},
    {BOMB, 4}, {MINE, 2}, {ROCKET_SPIN, 8}, {ROCKET_FIRED, 1}, {ENERGY_DROP, 4}, {EXPLOSION, 8},
    {PLAYER_CHARACTER, 4}, {EGG_THIEF, 4}, {WALL_EATER, 8}, {DRONE, 16}, {ROBOT1, 4}, {MUNCH_EFFECT, 1},
    {DAMAGE_EFFECT, 5}
  };
  ASSERT_EQ((int) expected.size(), NUM_ENTITY_TYPES);
  for (const auto& e : expected)
  {
    EXPECT_EQ(EntityAnim::frameCount(e.first), e.second) << "type " << e.first;
  }
}

TEST_F(EntityAnimTest, AnEntitysFrameWrapsAtItsTypesFrameCount)
{
  configurePlayers(2);
  useTestMap();
  startRound();

  for (int t = 0; t < NUM_ENTITY_TYPES; t++)
  {
    const EntityType type = static_cast<EntityType>(t);
    if (type == PLAYER_CHARACTER)
    {
      continue; // players are built by the round itself (their sheet has 4 frames)
    }
    clearBlocks();
    spawnEntity(type, 0, 3, 3);
    DynamicMap* map = em().getDynamicMap();
    ASSERT_FALSE(map->entityList.empty());
    std::shared_ptr<GameEntity> entity = map->entityList.rbegin()->second;
    ASSERT_EQ(entity->getType(), type) << "type " << t << " was not the newest entity";

    const int frames = EntityAnim::frameCount(type);
    std::set<int> seen;
    for (int i = 0; i < frames; i++)
    {
      entity->GameEntity::incAnimFrame(); // the base class: players add a delay on top
      EXPECT_GE(entity->animFrame(), 0);
      EXPECT_LT(entity->animFrame(), frames) << "type " << t;
      seen.insert(entity->animFrame());
    }
    EXPECT_EQ((int) seen.size(), frames) << "type " << t << " did not show each of its frames once in " << frames << " steps";
  }
}

TEST_F(EntityAnimTest, APlayersFrameWrapsAtTheCharacterSheet)
{
  configurePlayers(2);
  startRound();
  auto p = player(1);
  std::set<int> seen;
  for (int i = 0; i < 4 * 40; i++)
  {
    p->incAnimFrame(); // the player override: one frame per PLAYER_ANIM_DELAY calls
    EXPECT_LT(p->animFrame(), EntityAnim::frameCount(PLAYER_CHARACTER));
    seen.insert(p->animFrame());
  }
  EXPECT_EQ((int) seen.size(), EntityAnim::frameCount(PLAYER_CHARACTER));
}

} // namespace
} // namespace bitriot_test
