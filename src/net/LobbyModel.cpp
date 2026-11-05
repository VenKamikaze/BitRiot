#include "LobbyModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace netplay
{

// -- text ---------------------------------------------------------------------------------------

namespace
{
// Length of the well-formed UTF-8 sequence starting at s[i] (1..4), or 0 if there is none there.
size_t sequenceLength(const std::string& s, size_t i)
{
  const unsigned char c = (unsigned char) s[i];
  if (c < 0x80) return 1;
  size_t need = 0;
  unsigned char lo = 0x80, hi = 0xBF;
  if (c >= 0xC2 && c <= 0xDF) need = 1;
  else if (c >= 0xE0 && c <= 0xEF)
  {
    need = 2;
    if (c == 0xE0) lo = 0xA0;      // no overlong forms
    if (c == 0xED) hi = 0x9F;      // no surrogates
  }
  else if (c >= 0xF0 && c <= 0xF4)
  {
    need = 3;
    if (c == 0xF0) lo = 0x90;
    if (c == 0xF4) hi = 0x8F;      // nothing past U+10FFFF
  }
  else return 0;
  if (i + need >= s.size()) return 0;
  for (size_t k = 1; k <= need; k++)
  {
    const unsigned char d = (unsigned char) s[i + k];
    const unsigned char l = k == 1 ? lo : 0x80;
    const unsigned char h = k == 1 ? hi : 0xBF;
    if (d < l || d > h) return 0;
  }
  return need + 1;
}
} // namespace

std::string sanitizeUtf8(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();)
  {
    const unsigned char c = (unsigned char) text[i];
    if (c < 0x20 || c == 0x7F)
    {
      out += '?';
      i++;
      continue;
    }
    const size_t n = sequenceLength(text, i);
    if (n == 0)
    {
      out += '?';
      i++;
      continue;
    }
    out.append(text, i, n);
    i += n;
  }
  return out;
}

size_t utf8Length(const std::string& text)
{
  size_t chars = 0;
  for (unsigned char c : text)
  {
    if ((c & 0xC0) != 0x80) chars++;
  }
  return chars;
}

std::string clipUtf8(const std::string& text, size_t maxChars)
{
  const std::string clean = sanitizeUtf8(text);
  if (utf8Length(clean) <= maxChars)
  {
    return clean;
  }
  const size_t keep = maxChars > 3 ? maxChars - 3 : 0;
  size_t chars = 0;
  size_t i = 0;
  while (i < clean.size() && chars < keep)
  {
    i += sequenceLength(clean, i);
    chars++;
  }
  std::string out = clean.substr(0, i);
  out += std::string(std::min<size_t>(3, maxChars), '.');
  return out;
}

std::string clipUtf8Bytes(const std::string& text, size_t maxBytes)
{
  const std::string clean = sanitizeUtf8(text);
  if (clean.size() <= maxBytes)
  {
    return clean;
  }
  size_t end = 0;
  for (size_t i = 0; i < clean.size();)
  {
    const size_t n = sequenceLength(clean, i);
    if (i + n > maxBytes) break;
    i += n;
    end = i;
  }
  return clean.substr(0, end);
}

std::string hex64(uint64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%016llx", (unsigned long long) v);
  return buf;
}

// -- the lobby table ------------------------------------------------------------------------------

namespace
{
const LobbyEntry* findPeer(const LobbyView& v, uint8_t id)
{
  for (const LobbyEntry& e : v.peers)
  {
    if (e.id == id) return &e;
  }
  return nullptr;
}

int popcount8(unsigned v)
{
  int n = 0;
  for (; v; v >>= 1) n += v & 1u;
  return n;
}

std::string joinNames(const std::vector<std::string>& names)
{
  std::string out;
  for (size_t i = 0; i < names.size(); i++)
  {
    if (i > 0) out += i + 1 == names.size() ? " and " : ", ";
    out += names[i];
  }
  return out;
}
} // namespace

std::vector<LobbyRow> buildLobbyRows(const LobbyView& view, const LobbyContext& ctx)
{
  std::vector<LobbyRow> rows;
  for (int s = 0; s < MAX_SLOTS; s++)
  {
    LobbyRow r;
    r.slot = s;
    r.number = std::to_string(s + 1);
    if (s >= (int) view.slots.size())
    {
      r.kind = SlotKind::OPEN;
      r.name = "open";
      rows.push_back(r);
      continue;
    }
    const SlotInfo& si = view.slots[s];
    r.owner = si.owner;
    r.team = si.team;
    r.teamEditable = ctx.isHost && ctx.inLobby;
    if (si.owner == NO_PEER)
    {
      r.kind = SlotKind::AI;
      r.name = "Computer";
      rows.push_back(r);
      continue;
    }
    const LobbyEntry* e = findPeer(view, si.owner);
    r.kind = si.owner == HOST_ID ? SlotKind::HOST : SlotKind::REMOTE;
    r.mine = si.owner == ctx.myId;
    std::string name = e ? e->name : std::string("player");
    int index = 1;
    int count = 1;
    if (e)
    {
      count = std::max(1, popcount8(e->slotMask));
      index = popcount8(e->slotMask & ((1u << s) - 1u)) + 1;
    }
    std::string suffix;
    if (count > 1)
    {
      suffix = " " + std::to_string(index) + "/" + std::to_string(count);
    }
    if (r.mine)
    {
      suffix += " (you)";
    }
    const size_t room = ctx.nameChars > suffix.size() ? ctx.nameChars - suffix.size() : 1;
    r.name = clipUtf8(name, room) + suffix;
    if (si.owner != HOST_ID)
    {
      r.showReady = true;
      r.ready = e && e->ready;
      r.pingMs = e ? e->rttMs : -1;
      r.kickable = ctx.isHost && ctx.inLobby && index == 1;
      r.kickId = si.owner;
    }
    rows.push_back(r);
  }
  return rows;
}

std::string describeSettings(const LobbySettings& s, int slots)
{
  std::ostringstream t;
  t << "Map " << s.mapWidth << " x " << s.mapHeight << ", blocks " << s.blockPercentage << "%, enemies "
    << (s.npcSpawner ? "on" : "off") << ", " << slots << " players";
  return t.str();
}

LobbyScreen buildLobbyScreen(const LobbyView& view, const LobbyContext& ctx)
{
  LobbyScreen sc;
  sc.isHost = ctx.isHost;
  sc.rows = buildLobbyRows(view, ctx);
  sc.shownRows = (int) view.slots.size();
  sc.title = ctx.isHost ? "Hosting a network game" : "Network game";
  sc.settings = describeSettings(view.settings, (int) view.slots.size());
  sc.myReady = ctx.myReady;
  if (ctx.isHost)
  {
    const LobbySettings& st = view.settings;
    sc.settingButtons[0] = "Width " + std::to_string(st.mapWidth);
    sc.settingButtons[1] = "Height " + std::to_string(st.mapHeight);
    sc.settingButtons[2] = "Blocks " + std::to_string(st.blockPercentage) + "%";
    sc.settingButtons[3] = std::string("Enemies ") + (st.npcSpawner ? "on" : "off");
    sc.settingButtons[4] = "Players " + (st.totalSlots == 0 ? std::string("auto") : std::to_string(st.totalSlots));
  }

  std::vector<std::string> notReady;
  for (const LobbyEntry& e : view.peers)
  {
    if (e.id == HOST_ID || !e.connected) continue;
    sc.humanPeers++;
    if (e.ready) sc.readyPeers++;
    else notReady.push_back(clipUtf8(e.name, ctx.nameChars));
  }

  if (ctx.isHost)
  {
    sc.startLabel = "Start match";
    sc.canStart = ctx.canStart && ctx.inLobby;
    if (!ctx.inLobby)
    {
      sc.status = "Between rounds.";
    }
    else if (ctx.canStart)
    {
      if (sc.humanPeers == 0)
        sc.status = "No one else has joined yet. Start with computer players, or wait for your friends.";
      else if (notReady.empty())
        sc.status = "Everyone is ready.";
      else
        sc.status = joinNames(notReady) + (notReady.size() == 1 ? " has" : " have") + " not said ready, but you can start now.";
    }
    else if (!notReady.empty())
    {
      sc.status = "Waiting for " + joinNames(notReady) + " to be ready.";
    }
    else
    {
      sc.status = ctx.startBlocked.empty() ? std::string("Cannot start yet.") : ctx.startBlocked;
    }
  }
  else
  {
    if (!ctx.inLobby)
    {
      sc.startLabel = "Waiting for the host";
      sc.canToggleReady = false;
      sc.status = "A match is between rounds. You are dealt in when the host starts the next one.";
    }
    else
    {
      sc.canToggleReady = true;
      sc.startLabel = ctx.myReady ? "Not ready" : "Ready";
      sc.status = ctx.myReady ? "You are ready. Waiting for the host to start the match."
                              : "Press Ready when you are set.";
    }
  }
  return sc;
}

// -- why a session ended ------------------------------------------------------------------------------

EndMessage describeEnd(LockstepSession::EndReason reason, const RejectInfo& rj, const std::string& sessionText,
                       const Identity& mine, const std::string& address)
{
  EndMessage m;
  using L = LockstepSession;
  switch (reason)
  {
    case L::END_REJECTED:
    {
      switch (rj.reason)
      {
        case REJECT_VERSION:
          m.title = "Different game version";
          m.lines.push_back("The host speaks network protocol " + std::to_string(rj.hostProtocol) + "; this game speaks "
                            + std::to_string(mine.protocol) + ".");
          if (rj.hostProtocol > mine.protocol)
            m.lines.push_back("The host's game is newer than yours. Update this game to the host's version.");
          else if (rj.hostProtocol < mine.protocol)
            m.lines.push_back("The host's game is older than yours. Ask the host to update.");
          if (rj.hostBuildId != 0)
            m.lines.push_back("Host build " + hex64(rj.hostBuildId) + ", yours " + hex64(mine.buildId) + ".");
          break;
        case REJECT_BUILD:
          m.title = "Different game build";
          m.lines.push_back("Host build " + hex64(rj.hostBuildId) + ", yours " + hex64(mine.buildId) + ".");
          if (rj.hostProtocol > mine.protocol)
            m.lines.push_back("The host's game is newer than yours (protocol " + std::to_string(rj.hostProtocol) + " against "
                              + std::to_string(mine.protocol) + "). Update this game.");
          else if (rj.hostProtocol < mine.protocol)
            m.lines.push_back("The host's game is older than yours (protocol " + std::to_string(rj.hostProtocol) + " against "
                              + std::to_string(mine.protocol) + "). Ask the host to update.");
          else
            m.lines.push_back("Both machines speak the same protocol, but a build id is a hash of the game's rules, so which one is newer cannot be told from it.");
          m.lines.push_back("Both machines must run exactly the same version of Bit Riot, or the game would drift apart within seconds.");
          break;
        case REJECT_DATA:
          m.title = "Different game data";
          m.lines.push_back("Host data " + hex64(rj.hostDataHash) + ", yours " + hex64(mine.dataHash) + ".");
          m.lines.push_back("The files in the data folder (entity.txt, player.txt) differ from the host's. Use the same data folder as the host.");
          break;
        case REJECT_FULL:
          m.title = "The game is full";
          m.lines.push_back("The host has no free player slot for you (six players at most, counting everyone on every machine).");
          m.lines.push_back("Ask the host to make room, or bring fewer local players, or try again later.");
          break;
        case REJECT_IN_PROGRESS:
          m.title = "A match is already in progress";
          m.lines.push_back("You can join between rounds: try again when the host is on the scoreboard or back in the lobby.");
          break;
        case REJECT_BAD_REQUEST:
          m.title = "The host refused the request";
          m.lines.push_back(rj.text.empty() ? std::string("It did not make sense to the host.") : sanitizeUtf8(rj.text));
          break;
        default:
          m.title = "The host refused you";
          m.lines.push_back(rj.text.empty() ? sanitizeUtf8(sessionText) : sanitizeUtf8(rj.text));
          break;
      }
      break;
    }
    case L::END_CONNECT_TIMEOUT:
      m.title = "The host did not answer";
      m.lines.push_back("Nothing came back from " + (address.empty() ? std::string("the host") : address) + " in time.");
      m.lines.push_back("Check the address and the port, that the host is running, and that its UDP port is reachable (forwarded) if it is outside your network.");
      break;
    case L::END_HOST_LOST:
      m.title = "Lost the connection to the host";
      m.lines.push_back("No packet came from the host for too long. The match is over.");
      break;
    case L::END_HOST_CLOSED:
      if (sessionText.find("diverged") != std::string::npos)
      {
        m.title = "The host stopped the match";
        m.lines.push_back("The game state is no longer the same on every machine (a desync).");
      }
      else
      {
        m.title = "The host left";
        m.lines.push_back("The host ended the session.");
      }
      break;
    case L::END_KICKED:
      m.title = "You were dropped";
      m.lines.push_back("The host heard nothing from you for too long and dropped you.");
      break;
    case L::END_REMOVED:
      m.title = "You were removed";
      m.lines.push_back("The host removed you from the game. You can join again.");
      break;
    case L::END_LEFT:
      m.title = "You left the game";
      m.isError = false;
      break;
    case L::END_DESYNC:
      m.title = "Stopped after a desync";
      m.lines.push_back("The game state is no longer the same on every machine.");
      break;
    case L::END_NETWORK:
      m.title = "Network error";
      m.lines.push_back(sanitizeUtf8(sessionText));
      break;
    case L::END_BAD_HOST:
      m.title = "The host sent a match this game cannot play";
      m.lines.push_back(sanitizeUtf8(sessionText));
      break;
    case L::END_NONE:
    default:
      m.title = "Disconnected";
      if (!sessionText.empty()) m.lines.push_back(sanitizeUtf8(sessionText));
      break;
  }
  return m;
}

// -- addresses and the saved settings ----------------------------------------------------------------

bool parsePort(const std::string& text, int* port, std::string* error)
{
  std::string t = text;
  while (!t.empty() && t.front() == ' ') t.erase(t.begin());
  while (!t.empty() && t.back() == ' ') t.pop_back();
  if (t.empty() || t.size() > 5 || !std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; }))
  {
    if (error) *error = "The port is a number from 1 to 65535.";
    return false;
  }
  const int v = std::atoi(t.c_str());
  if (v < 1 || v > 65535)
  {
    if (error) *error = "The port is a number from 1 to 65535.";
    return false;
  }
  if (port) *port = v;
  return true;
}

bool parseHostPort(const std::string& textIn, int defaultPort, std::string* host, int* port, std::string* error,
                   std::string* normalised)
{
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  std::string t = textIn;
  while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
  while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
  if (t.empty())
  {
    return fail("Type the host's address, for example 192.168.1.20:7777.");
  }
  std::string h;
  std::string p;
  bool v6 = false;
  if (t[0] == '[')
  {
    const size_t close = t.find(']');
    if (close == std::string::npos) return fail("The address has a '[' without its ']'.");
    h = t.substr(1, close - 1);
    v6 = true;
    const std::string rest = t.substr(close + 1);
    if (!rest.empty())
    {
      if (rest[0] != ':') return fail("After the ']' only :port may follow.");
      p = rest.substr(1);
      if (p.empty()) return fail("The port after ':' is missing.");
    }
  }
  else
  {
    const size_t first = t.find(':');
    if (first != std::string::npos)
    {
      if (t.find(':', first + 1) != std::string::npos)
      {
        return fail("Put an IPv6 address in square brackets, like [::1]:7777.");
      }
      h = t.substr(0, first);
      p = t.substr(first + 1);
      if (p.empty()) return fail("The port after ':' is missing.");
    }
    else
    {
      h = t;
    }
  }
  if (h.empty()) return fail("The host part of the address is empty.");
  if (h.size() > 253) return fail("That host name is too long.");
  for (unsigned char c : h)
  {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_'
                    || (v6 && (c == ':' || c == '%'));
    if (!ok) return fail("The host name has a character that cannot be in an address (spaces and symbols are not allowed).");
  }
  int portValue = defaultPort;
  if (!p.empty())
  {
    std::string why;
    if (!parsePort(p, &portValue, &why)) return fail(why);
  }
  if (host) *host = h;
  if (port) *port = portValue;
  if (normalised) *normalised = (v6 ? "[" + h + "]" : h) + ":" + std::to_string(portValue);
  return true;
}

std::string cleanPlayerName(const std::string& text)
{
  std::string s;
  for (unsigned char c : text)
  {
    if (c >= 0x20 && c != 0x7F) s += (char) c;
  }
  s = sanitizeUtf8(s);
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return clipUtf8Bytes(s, MAX_NAME);
}

namespace
{
std::string trim(const std::string& s)
{
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
  return s.substr(a, b - a);
}
} // namespace

bool parseNetPrefs(const std::string& text, NetPrefs& out)
{
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
  {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line.size() > 400) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "name")
    {
      out.name = cleanPlayerName(value);
    }
    else if (key == "join")
    {
      std::string normalised;
      if (value.empty())
      {
        out.joinAddress.clear();
      }
      else if (parseHostPort(value, 7777, nullptr, nullptr, nullptr, &normalised))
      {
        out.joinAddress = value.size() <= 260 ? value : normalised;
      }
    }
    else if (key == "port")
    {
      int p = 0;
      if (parsePort(value, &p, nullptr)) out.hostPort = p;
    }
    else if (key == "players")
    {
      if (value.size() == 1 && value[0] >= '1' && value[0] <= '4') out.localPlayers = value[0] - '0';
    }
  }
  return true;
}

bool loadNetPrefs(const std::string& path, NetPrefs& out)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
  {
    return false;
  }
  std::stringstream buf;
  buf << f.rdbuf();
  std::string text = buf.str();
  if (text.size() > 8192) text.resize(8192);
  return parseNetPrefs(text, out);
}

std::string formatNetPrefs(const NetPrefs& p)
{
  std::ostringstream t;
  t << "name=" << cleanPlayerName(p.name) << "\n"
    << "join=" << sanitizeUtf8(p.joinAddress) << "\n"
    << "port=" << p.hostPort << "\n"
    << "players=" << p.localPlayers << "\n";
  return t.str();
}

bool saveNetPrefs(const std::string& path, const NetPrefs& p)
{
  // A name of this process's own: two game windows sharing a preferences folder must not write the same temporary file.
  const std::string tmp = path + "." + std::to_string((long) getpid()) + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
    {
      return false;
    }
    f << formatNetPrefs(p);
    f.flush();
    if (!f)
    {
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0)
  {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

// -- during a round ---------------------------------------------------------------------------------------

std::string eventToast(const ControlEvent& e, const std::string& who)
{
  if (e.kind != ControlEvent::RETURN_TO_AI && e.kind != ControlEvent::LEAVE)
  {
    return "";
  }
  const std::string name = who.empty() ? std::string("A player") : clipUtf8(who, 20);
  return name + " left - now AI";
}

std::string stallSentence(const std::string& waitingText)
{
  if (waitingText.empty())
  {
    return "";
  }
  std::string t = sanitizeUtf8(waitingText);
  if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char) (t[0] - 'a' + 'A');
  return t + "...";
}

const std::string& StallTimer::update(const std::string& wanted, uint32_t nowMs)
{
  if (wanted.empty())
  {
    m_wanted.clear();
    m_shown.clear();
    m_since = 0;
    return m_shown;
  }
  if (m_wanted.empty())
  {
    m_since = nowMs;
  }
  m_wanted = wanted;
  if ((uint32_t) (nowMs - m_since) >= m_delay)
  {
    m_shown = wanted;
  }
  else
  {
    m_shown.clear();
  }
  return m_shown;
}

} // namespace netplay
