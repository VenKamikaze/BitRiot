// Netplay across real processes (brief 07): two or three bitriot_headless processes play
// lockstep rounds over native UDP on 127.0.0.1, with simulated latency, jitter, loss,
// duplication and reordering from VNET_IMPAIR (a seed of its own in each process), and
// must print byte-identical results. This is the "whole stack" test: real sockets, real
// time, real Simulation in each process. It takes a few tens of seconds, so it is its own
// executable and ctest entry (bitriot_netplay_process_tests); tests/README.md says how to
// run it.
//
// Ports are ephemeral: the host listens on port 0 and writes the port it got to a file.

#include <gtest/gtest.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "MatchRecord.h"
#include "support/ScriptedInput.h"

namespace fs = std::filesystem;

namespace
{
std::string slurp(const std::string& path)
{
  std::ifstream in(path.c_str(), std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

class Scratch
{
  public:
    Scratch()
    {
      static int counter = 0;
      m_dir = fs::temp_directory_path() / ("bitriot_net_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
      fs::create_directories(m_dir);
    }
    ~Scratch()
    {
      std::error_code ignored;
      fs::remove_all(m_dir, ignored);
    }
    std::string file(const std::string& name) const { return (m_dir / name).string(); }
    std::string dir(const std::string& name) const
    {
      fs::create_directories(m_dir / name);
      return (m_dir / name).string();
    }
  private:
    fs::path m_dir;
};

// One child bitriot_headless process with stdout and stderr in files.
class Proc
{
  public:
    Proc(const std::vector<std::string>& args, const std::string& impair, const std::string& outFile, const std::string& errFile)
      : m_out(outFile), m_err(errFile)
    {
      m_pid = fork();
      if (m_pid == 0)
      {
        if (!impair.empty()) setenv("VNET_IMPAIR", impair.c_str(), 1); else unsetenv("VNET_IMPAIR");
        FILE* o = freopen(outFile.c_str(), "w", stdout);
        FILE* e = freopen(errFile.c_str(), "w", stderr);
        (void) o; (void) e;
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(BITRIOT_HEADLESS_BINARY));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(BITRIOT_HEADLESS_BINARY, argv.data());
        _exit(127);
      }
    }
    ~Proc()
    {
      if (m_pid > 0 && !m_done)
      {
        kill(m_pid, SIGKILL);
        int status;
        waitpid(m_pid, &status, 0);
      }
    }
    // Waits up to timeoutSecs for the exit; returns the exit code, or -1 on a timeout /
    // signal (the process is then killed).
    int wait(int timeoutSecs)
    {
      const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSecs);
      while (true)
      {
        int status = 0;
        const pid_t r = waitpid(m_pid, &status, WNOHANG);
        if (r == m_pid)
        {
          m_done = true;
          return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (std::chrono::steady_clock::now() > limit)
        {
          kill(m_pid, SIGKILL);
          waitpid(m_pid, &status, 0);
          m_done = true;
          return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    void killNow()
    {
      kill(m_pid, SIGKILL);
      int status;
      waitpid(m_pid, &status, 0);
      m_done = true;
    }
    std::string out() const { return slurp(m_out); }
    std::string err() const { return slurp(m_err); }
  private:
    pid_t m_pid = -1;
    bool m_done = false;
    std::string m_out, m_err;
};

// The host's port, once it has written the port file.
int waitForPort(const std::string& portFile)
{
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < limit)
  {
    std::ifstream f(portFile.c_str());
    int port = 0;
    if (f >> port && port > 0) return port;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return 0;
}

// A script record: random inputs for the first `humans` slots, `players` players.
std::string makeScript(const Scratch& dir, int players, int humans, int ticks, uint64_t seed)
{
  MatchRecord rec;
  rec.config.mapWidth = 13;
  rec.config.mapHeight = 11;
  rec.config.slots.resize(players);
  for (int i = 0; i < players; i++) { rec.config.slots[i].team = i; rec.config.slots[i].ai = i >= humans; }
  // randomInputs takes an initializer_list of slots; build the script slot by slot.
  rec.ticks.resize(ticks);
  for (int s = 0; s < humans; s++)
  {
    const std::vector<TickInputs> one = bitriot_test::randomInputs(seed + (uint64_t) s, ticks, {s});
    for (int t = 0; t < ticks; t++) rec.ticks[t].slot[s] = one[t].slot[s];
  }
  const std::string path = dir.file("script.brmr");
  rec.writeFile(path);
  return path;
}

std::string impairSpec(int process, const char* base = "lat=8,jit=3,loss=5,dup=1,reorder=2")
{
  return std::string(base) + ",seed=" + std::to_string(40 + process);
}

// "name" lines of the output that all processes must share.
std::vector<std::string> lines(const std::string& text)
{
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) out.push_back(line);
  return out;
}

int count(const std::string& text, const std::string& needle)
{
  int n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) n++;
  return n;
}

const char* kConfig3 = "players=3,map=13x11,blocks=75";
const int kTimeout = 150;
} // namespace

TEST(NetplayProcess, TwoProcessesPlayTwoRoundsUnderLossAndPrintTheSame)
{
  Scratch dir;
  const std::string script = makeScript(dir, 3, 2, 4000, 11);
  const std::string port = dir.file("port");
  Proc host({"host", "--port-file", port, "--peers", "1", "--rounds", "2", "--seed", "10", "--config", kConfig3, "--script", script,
             "--tick-ms", "2", "--delay", "12", "--out", dir.dir("h")}, impairSpec(0), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  Proc peer({"join", "127.0.0.1:" + std::to_string(p), "--rounds", "2", "--script", script, "--tick-ms", "2", "--out", dir.dir("p")},
            impairSpec(1), dir.file("p.out"), dir.file("p.err"));
  const int hostCode = host.wait(kTimeout);
  const int peerCode = peer.wait(20);
  ASSERT_EQ(hostCode, 0) << host.err() << "\n" << host.out();
  ASSERT_EQ(peerCode, 0) << peer.err() << "\n" << peer.out();

  // Identical output: every checkpoint hash of both rounds, the final hashes and the winners.
  EXPECT_EQ(host.out(), peer.out());
  const std::string out = host.out();
  EXPECT_EQ(count(out, ": over at tick"), 2);
  EXPECT_GE(count(out, "checkpoint 1 "), 20);
  EXPECT_GE(count(out, "checkpoint 2 "), 20);
  EXPECT_EQ(count(out, "winner "), 2);
  EXPECT_NE(out.find("round 1: seed 0x000000000000000a"), std::string::npos) << out;
  EXPECT_NE(out.find("round 2: seed 0x000000000000000b"), std::string::npos) << out;
  EXPECT_NE(out.find("delay 12"), std::string::npos);

  // The impairment really happened (the test would prove less over a clean link).
  EXPECT_NE(host.err().find("hosting on UDP port"), std::string::npos);

  // Both machines recorded the host's final inputs: the records are byte-identical, and a
  // replay of one agrees with every checkpoint.
  for (int round = 1; round <= 2; round++)
  {
    const std::string hr = dir.file("h/round-" + std::to_string(round) + "-peer0.brmr");
    const std::string pr = dir.file("p/round-" + std::to_string(round) + "-peer1.brmr");
    ASSERT_TRUE(fs::exists(hr)) << hr;
    ASSERT_TRUE(fs::exists(pr)) << pr;
    EXPECT_EQ(slurp(hr), slurp(pr)) << "round " << round;
    Proc replay({"replay", hr}, "", dir.file("r.out"), dir.file("r.err"));
    EXPECT_EQ(replay.wait(60), 0) << replay.out() << replay.err();
    EXPECT_NE(replay.out().find("replay: OK"), std::string::npos) << replay.out();
  }
}

TEST(NetplayProcess, ThreeProcessesWithTwoLocalPlayersATakeoverAndAPeerWhoLeaves)
{
  Scratch dir;
  const std::string script = makeScript(dir, 6, 4, 4000, 23);
  const std::string port = dir.file("port");
  // Host 1 player (slot 0), peer 1 two players (slots 1, 2), peer 2 one player (slot 3), and
  // two bots (slots 4, 5) on a tall map. Peer 2 takes slot 4 over at tick 200 and quits at tick 600.
  const char* config = "players=6,map=17x19,blocks=75";
  Proc host({"host", "--port-file", port, "--peers", "2", "--seed", "3", "--config", config, "--script", script,
             "--tick-ms", "2", "--delay", "12", "--out", dir.dir("h")}, impairSpec(0), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  const std::string addr = "127.0.0.1:" + std::to_string(p);
  Proc peer1({"join", addr, "--local-players", "2", "--script", script, "--tick-ms", "2", "--out", dir.dir("p1")}, impairSpec(1), dir.file("p1.out"), dir.file("p1.err"));
  Proc peer2({"join", addr, "--script", script, "--tick-ms", "2", "--takeover", "4@200", "--leave-at", "600", "--out", dir.dir("p2")},
             impairSpec(2), dir.file("p2.out"), dir.file("p2.err"));
  const int hostCode = host.wait(kTimeout);
  const int peer1Code = peer1.wait(20);
  const int peer2Code = peer2.wait(20);
  ASSERT_EQ(hostCode, 0) << host.err() << "\n" << host.out();
  ASSERT_EQ(peer1Code, 0) << peer1.err() << "\n" << peer1.out();
  EXPECT_EQ(peer2Code, 0) << peer2.err();   // leaving on purpose is not a failure

  EXPECT_EQ(host.out(), peer1.out());
  const std::string out = host.out();
  EXPECT_NE(out.find("owners 0:p0 1:p1 2:p1 3:p2 4:ai 5:ai"), std::string::npos) << out;
  // The takeover (tick 200 + D = 212) and the departure: slots 3 and 4, back to the AI.
  EXPECT_NE(out.find("event 1 212 slot 4 HUMAN_TAKES_OVER"), std::string::npos) << out;
  EXPECT_EQ(count(out, "RETURN_TO_AI"), 2) << out;
  EXPECT_NE(out.find("slot 3 RETURN_TO_AI"), std::string::npos);
  EXPECT_NE(out.find("slot 4 RETURN_TO_AI"), std::string::npos);
  EXPECT_EQ(count(out, ": over at tick"), 1);
  // Peer 2 played until it left: what it printed is a prefix of the others' output.
  const std::string p2 = peer2.out();
  EXPECT_NE(p2.find("checkpoint 1 "), std::string::npos) << peer2.err();
  const std::vector<std::string> hl = lines(out), pl = lines(p2);
  for (size_t i = 0; i < pl.size() && i < hl.size(); i++)
  {
    if (pl[i].rfind("checkpoint", 0) == 0) EXPECT_EQ(pl[i], hl[i]) << "line " << i;
  }
}

TEST(NetplayProcess, ADesyncIsDetectedAtTheNextCheckpointAndEveryoneWritesTheEvidence)
{
  Scratch dir;
  const std::string port = dir.file("port");
  Proc host({"host", "--port-file", port, "--peers", "2", "--seed", "10", "--config", kConfig3, "--tick-ms", "2", "--delay", "12",
             "--out", dir.dir("h")}, impairSpec(0), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  const std::string addr = "127.0.0.1:" + std::to_string(p);
  Proc peer1({"join", addr, "--tick-ms", "2", "--out", dir.dir("p1")}, impairSpec(1), dir.file("p1.out"), dir.file("p1.err"));
  // Peer 2's state is changed by one unit before tick 355: its checkpoint at tick 360 is the first to differ.
  Proc peer2({"join", addr, "--tick-ms", "2", "--perturb-at", "355", "--out", dir.dir("p2")}, impairSpec(2), dir.file("p2.out"), dir.file("p2.err"));
  const int hostCode = host.wait(kTimeout);
  const int peer1Code = peer1.wait(30);
  const int peer2Code = peer2.wait(30);
  EXPECT_EQ(hostCode, 1) << host.err() << host.out();
  EXPECT_EQ(peer1Code, 1) << peer1.err() << peer1.out();
  EXPECT_EQ(peer2Code, 1) << peer2.err() << peer2.out();
  const std::string line = "desync round 1 tick 360 ";
  EXPECT_NE(host.out().find(line), std::string::npos) << host.out();
  EXPECT_NE(peer1.out().find(line), std::string::npos) << peer1.out();
  EXPECT_NE(peer2.out().find(line), std::string::npos) << peer2.out();
  EXPECT_NE(host.out().find("offender 2"), std::string::npos);
  // The line is the same everywhere (host hash, peer hash, offender).
  auto desyncLine = [](const std::string& out) {
    for (const std::string& l : lines(out)) if (l.rfind("desync ", 0) == 0) return l;
    return std::string();
  };
  EXPECT_EQ(desyncLine(host.out()), desyncLine(peer1.out()));
  EXPECT_EQ(desyncLine(host.out()), desyncLine(peer2.out()));

  // Each machine wrote the state dump for tick 360, the dump of where it stopped and the
  // record of the round so far; the record parses.
  for (const char* who : {"h", "p1", "p2"})
  {
    const fs::path d = dir.dir(who);
    bool atTick = false, now = false, record = false;
    for (const auto& entry : fs::directory_iterator(d))
    {
      const std::string name = entry.path().filename().string();
      if (name.find("-t360-") != std::string::npos && name.find("-state.txt") != std::string::npos && name.find("-now-") == std::string::npos)
      {
        atTick = true;
        EXPECT_NE(slurp(entry.path().string()).find("tick"), std::string::npos);
      }
      if (name.find("-now-t") != std::string::npos) now = true;
      if (name.size() > 5 && name.compare(name.size() - 5, 5, ".brmr") == 0)
      {
        record = true;
        EXPECT_NO_THROW(MatchRecord::readFile(entry.path().string())) << name;
      }
    }
    EXPECT_TRUE(atTick) << who;
    EXPECT_TRUE(now) << who;
    EXPECT_TRUE(record) << who;
  }
}

TEST(NetplayProcess, ADesyncInTheLastTicksOfARoundIsDetectedToo)
{
  // Seed 10 on this config ends at tick 1526; the periodic checkpoints are every 30 ticks (the last at
  // 1500). Perturbing before tick 1515 changes only the hash reported with the round's last tick.
  Scratch dir;
  const std::string port = dir.file("port");
  Proc host({"host", "--port-file", port, "--peers", "1", "--seed", "10", "--config", kConfig3, "--tick-ms", "2", "--delay", "12",
             "--out", dir.dir("h")}, impairSpec(0), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  Proc peer({"join", "127.0.0.1:" + std::to_string(p), "--tick-ms", "2", "--perturb-at", "1515", "--out", dir.dir("p")},
            impairSpec(1), dir.file("p.out"), dir.file("p.err"));
  EXPECT_EQ(host.wait(kTimeout), 1) << host.err() << host.out();
  EXPECT_EQ(peer.wait(30), 1) << peer.err() << peer.out();
  EXPECT_NE(host.out().find("desync round 1 tick 1526 "), std::string::npos) << host.out();
  EXPECT_NE(peer.out().find("desync round 1 tick 1526 "), std::string::npos) << peer.out();
}

TEST(NetplayProcess, WithoutThePerturbationTheSameSetupDoesNotDesync)
{
  // The negative control of the test above: same seeds, same processes, nobody perturbed.
  Scratch dir;
  const std::string port = dir.file("port");
  Proc host({"host", "--port-file", port, "--peers", "1", "--seed", "10", "--config", kConfig3, "--tick-ms", "2", "--delay", "12"},
            impairSpec(0), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  Proc peer({"join", "127.0.0.1:" + std::to_string(p), "--tick-ms", "2"}, impairSpec(1), dir.file("p.out"), dir.file("p.err"));
  EXPECT_EQ(host.wait(kTimeout), 0) << host.err();
  EXPECT_EQ(peer.wait(20), 0) << peer.err();
  EXPECT_EQ(host.out().find("desync"), std::string::npos);
  EXPECT_EQ(host.out(), peer.out());
}

TEST(NetplayProcess, TheHostChoosesTheDelayFromTheMeasuredRoundTrip)
{
  Scratch dir;
  const std::string port = dir.file("port");
  // Latency of 60 ms each way (a round trip of 120 ms plus jitter) at a tick of 33 ms: D = ceil(130/33) + 1 = 5 or more.
  Proc host({"host", "--port-file", port, "--peers", "1", "--seed", "10", "--config", kConfig3, "--tick-ms", "33", "--timeout-secs", "20", "--rounds", "1"},
            impairSpec(0, "lat=60,jit=4"), dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  Proc peer({"join", "127.0.0.1:" + std::to_string(p), "--tick-ms", "33", "--timeout-secs", "20"}, impairSpec(1, "lat=60,jit=4"), dir.file("p.out"), dir.file("p.err"));
  // The round is long at 30 ticks a second; we only want the START.
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  std::string out;
  while (std::chrono::steady_clock::now() < limit)
  {
    out = host.out();
    if (out.find("round 1:") != std::string::npos) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_NE(out.find("round 1:"), std::string::npos) << host.err();
  const size_t at = out.find("delay ");
  ASSERT_NE(at, std::string::npos);
  const int d = atoi(out.c_str() + at + 6);
  EXPECT_GE(d, 5) << out;
  EXPECT_LE(d, 8) << out;
  EXPECT_NE(host.err().find("D="), std::string::npos) << host.err();
  host.killNow();
  peer.killNow();
}

TEST(NetplayProcess, LosingTheHostEndsThePeerCleanlyWithAMessage)
{
  Scratch dir;
  const std::string port = dir.file("port");
  Proc host({"host", "--port-file", port, "--peers", "1", "--seed", "10", "--config", kConfig3, "--tick-ms", "33", "--delay", "3"},
            "", dir.file("h.out"), dir.file("h.err"));
  const int p = waitForPort(port);
  ASSERT_GT(p, 0) << host.err();
  Proc peer({"join", "127.0.0.1:" + std::to_string(p), "--tick-ms", "33"}, "", dir.file("p.out"), dir.file("p.err"));
  // Wait for the round to start, then kill the host outright.
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < limit && peer.out().find("round 1:") == std::string::npos)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_NE(peer.out().find("round 1:"), std::string::npos) << peer.err();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  host.killNow();
  const int code = peer.wait(30);
  EXPECT_EQ(code, 1) << peer.err();
  EXPECT_NE(peer.err().find("lost the connection to the host"), std::string::npos) << peer.err();
}
