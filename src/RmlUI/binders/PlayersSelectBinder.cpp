#include "PlayersSelectBinder.h"
#include "MickLogger.h"
#include <string>

namespace std
{
  struct Controller {
    int index;
    std::string className;
    std::string imageSrc;
  };

  struct Controllers {
    std::vector<Controller> controllers = {};

    int getNumberOfAttachedControllers() {
      return controllers.size();
    }

    Controller* getControllerFromIndex(int index) {
      for(auto& controller : controllers)
      {
        if(controller.index == index)
        {
          return &controller;
        }
      }
      return nullptr;
    }

    // 1-based position of instanceId in the connected-controllers list (order
    // of controllersData.controllers), or -1 if not present. Used for stable
    // "Pad N" labels / image filenames instead of the raw SDL instance id,
    // which is not small/sequential and goes stale across a replug.
    int getControllerOrdinal(int instanceId) {
      for(size_t i = 0; i < controllers.size(); ++i)
      {
        if(controllers.at(i).index == instanceId)
        {
          return (int) i + 1;
        }
      }
      return -1;
    }

  } controllersData;

  // Map size shown on the player-select screen + the start-game gate. canStart is
  // false when the configured player count needs a taller map than is set; the
  // message explains how to fix it. Refreshed by refreshMapConfig().
  struct MapConfig {
    int mapWidth = 0;
    int mapHeight = 0;
    bool canStart = true;
    std::string playerLimitMsg = "";
  } mapConfigData;

  struct Players {
    // Bound directly to the canonical GameSettings model so the menu and the
    // game can't diverge. checkAssetAvailable() (reached via the GameSettings
    // ctor) is plain access()-based file I/O, so forcing construction at static
    // init here is safe. All mutations below go through GameSettings setters so
    // the derived genders/playerAIs mirrors stay in sync (single write path).
    std::vector<Player>& players = *std::GameSettings::getInstance()->getPlayers();

    Player* getPlayerForControllerId(int controllerId)
    {
      for(auto &player : players)
      {
        if(player.attachedControllerId == controllerId)
        {
          return &player;
        }
      }
      return nullptr;
    }
   
    int getPlayerIdAssociatedWithEvent(Rml::Event &ev)
    {
      Rml::Element *el = ev.GetTargetElement();
      assert(el);
      Rml::Variant *var = MickRmlUIElementUtil::getParentElementWithAttributeName(el, "playerId")->GetAttribute("playerId");
      assert(var);
      std::string playerIdString = var->Get<Rml::String>();
      assert(! playerIdString.empty());
      return atoi(playerIdString.c_str());
    }

    void toggleNumPlayers(Rml::DataModelHandle model_handle, Rml::Event& /*ev*/, const Rml::VariantList& /*arguments*/) {
      int newPlayers = (int) players.size() + 1;
      if (newPlayers > std::GameSettings::MAX_PLAYERS)
      {
        newPlayers = std::GameSettings::MIN_PLAYERS; // wrap it back to the minimum
      }
      // setNumberOfPlayers grows (appending defaults) or truncates the canonical
      // model in place, preserving existing entries and their device assignments.
      std::GameSettings::getInstance()->setNumberOfPlayers(newPlayers);
      MickLogger::getInstance()->debug(this, std::string("Players.toggleNumPlayers=").append(to_string(players.size())));
      model_handle.DirtyVariable("players");
      // The player count drives the map-size gate; recompute it now.
      PlayersSelectBinder::refreshMapConfig();
	  }

    // Replaces the old CPU/Human toggle. Cycles a slot's Control button
    // through CPU -> free-or-own keyboard sets (slots 0-3 only; there are
    // only 4 fixed sets, so slots 4/5 never see a keyboard candidate) ->
    // free-or-own connected pads -> back to CPU. assignPlayerControl is the
    // single write path, so this function only decides which choice is next.
    void cyclePlayerControl(Rml::DataModelHandle model_handle, Rml::Event& ev, const Rml::VariantList& /*arguments*/) {
      int playerId = getPlayerIdAssociatedWithEvent(ev);
      Player &player = players.at(playerId);

      struct Candidate { std::PlayersSelectBinder::ControlChoiceType type; int value; };
      std::vector<Candidate> candidates;
      candidates.push_back({std::PlayersSelectBinder::ControlChoiceType::CPU, -1});
      if(playerId < std::GameSettings::MAX_KEYBOARD_PLAYERS)
      {
        for(int set = 0; set < std::GameSettings::MAX_KEYBOARD_PLAYERS; ++set)
        {
          if(! std::PlayersSelectBinder::isKeyboardSetTaken(set, playerId))
          {
            candidates.push_back({std::PlayersSelectBinder::ControlChoiceType::KEYBOARD, set});
          }
        }
      }
      for(auto &c : controllersData.controllers)
      {
        if(! std::PlayersSelectBinder::isControllerTaken(c.index, playerId))
        {
          candidates.push_back({std::PlayersSelectBinder::ControlChoiceType::CONTROLLER, c.index});
        }
      }

      // Locate the player's current assignment in that list (falls through to
      // -1, i.e. "advance to CPU", if the current device isn't a candidate -
      // e.g. it was just disconnected).
      int currentIndex = -1;
      for(size_t i = 0; i < candidates.size(); ++i)
      {
        const Candidate &c = candidates.at(i);
        bool isCurrent = (c.type == std::PlayersSelectBinder::ControlChoiceType::CPU && ! player.human)
          || (c.type == std::PlayersSelectBinder::ControlChoiceType::KEYBOARD && player.attachedKeyboardId == c.value)
          || (c.type == std::PlayersSelectBinder::ControlChoiceType::CONTROLLER && player.attachedControllerId == c.value);
        if(isCurrent)
        {
          currentIndex = (int) i;
          break;
        }
      }

      const Candidate &chosen = candidates.at((currentIndex + 1) % (int) candidates.size());
      std::PlayersSelectBinder::assignPlayerControl(playerId, chosen.type, chosen.value);
      MickLogger::getInstance()->debug(this, std::string("Players.cyclePlayerControl[").append(to_string(playerId)).append("]=").append(player.deviceLabel));
      model_handle.DirtyVariable("players");
	  }

    void togglePlayerPortrait(Rml::DataModelHandle model_handle, Rml::Event& ev, const Rml::VariantList& /*arguments*/) {
      int playerId = getPlayerIdAssociatedWithEvent(ev);

      MickAssets::PlayerType newType = MickAssets::getNextPlayerType(players.at(playerId).type);
      string portaitFile = MickAssets::getPortraitBitmapFilename(playerId, newType);
      // setPlayerGender owns the type field (+ gender mirror); portrait is a
      // non-mirrored display field set directly on the canonical record.
      std::GameSettings::getInstance()->setPlayerGender(playerId, newType == MickAssets::PlayerType::MALE);
      players.at(playerId).portrait = portaitFile.c_str();

      MickLogger::getInstance()->debug(this, std::string("Players.togglePlayerPortrait[").append(to_string(playerId)).append("]=").append(portaitFile));
      model_handle.DirtyVariable("players");
	  }
  } playersData;

  PlayersSelectBinder::PlayersSelectBinder(Rml::Context* rmlContext)
  {
    PlayersSelectBinder::mapPlayersData();
    PlayersSelectBinder::mapControllersData();
    setupDataBinding(rmlContext);
  }

  bool PlayersSelectBinder::setupDataBinding(Rml::Context* context)
  { 
    Rml::DataModelConstructor constructor = context->CreateDataModel(m_modelName);

    if (!constructor)
    {
      return false;
    }

    if(auto playerHandle = constructor.RegisterStruct<Player>())
    {
      playerHandle.RegisterMember("id", &Player::id);
      playerHandle.RegisterMember("team", &Player::team);
      playerHandle.RegisterMember("portrait", &Player::portrait);
      playerHandle.RegisterMember("human", &Player::human);
      playerHandle.RegisterMember("deviceLabel", &Player::deviceLabel);
    }

    constructor.RegisterArray<std::vector<Player>>();

    constructor.Bind("players", std::GameSettings::getInstance()->getPlayers());
    constructor.BindEventCallback("toggleNumPlayers", &Players::toggleNumPlayers, &playersData);
    constructor.BindEventCallback("cyclePlayerControl", &Players::cyclePlayerControl, &playersData);
    constructor.BindEventCallback("togglePlayerPortrait", &Players::togglePlayerPortrait, &playersData);

    if(auto controllerHandle = constructor.RegisterStruct<Controller>())
    {
      controllerHandle.RegisterMember("index", &Controller::index);
      controllerHandle.RegisterMember("className", &Controller::className);
      controllerHandle.RegisterMember("imageSrc", &Controller::imageSrc);
    }
    constructor.RegisterArray<std::vector<Controller>>();

    constructor.Bind("controllers", &controllersData.controllers);

    // Map size display + start gate (see MapConfig / refreshMapConfig).
    constructor.Bind("mapWidth", &mapConfigData.mapWidth);
    constructor.Bind("mapHeight", &mapConfigData.mapHeight);
    constructor.Bind("canStart", &mapConfigData.canStart);
    constructor.Bind("playerLimitMsg", &mapConfigData.playerLimitMsg);

    m_dataModelHandle = constructor.GetModelHandle();

    // Seed the bound map-config values before the page first renders.
    refreshMapConfig();

    return true;
  }

  void PlayersSelectBinder::refreshMapConfig()
  {
    GameSettings *settings = std::GameSettings::getInstance();
    mapConfigData.mapWidth = settings->getMapWidth();
    mapConfigData.mapHeight = settings->getMapHeight();
    mapConfigData.canStart = settings->canStartGame();

    if (mapConfigData.canStart)
    {
      mapConfigData.playerLimitMsg = "";
    }
    else if (settings->hasUndrivableHuman())
    {
      mapConfigData.playerLimitMsg = "A player 5/6 slot is set to Human with no controller attached. Assign a pad or set it back to CPU.";
    }
    else
    {
      int numberOfPlayers = (int) playersData.players.size();
      int required = settings->requiredMapHeightFor(numberOfPlayers);
      std::stringstream ss;
      ss << numberOfPlayers << " players need a map at least " << required
         << " tiles tall (currently " << mapConfigData.mapHeight
         << "). Raise Map Height in Options.";
      mapConfigData.playerLimitMsg = ss.str();
    }

    Rml::DataModelHandle handle = Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle();
    handle.DirtyVariable("mapWidth");
    handle.DirtyVariable("mapHeight");
    handle.DirtyVariable("canStart");
    handle.DirtyVariable("playerLimitMsg");
  }

  void PlayersSelectBinder::removeDataBinding(Rml::Context *context)
  {
    if(context->GetDataModel(m_modelName))
    {
      MickLogger::getInstance()->debug(this, string("Removing data binding for ").append(m_modelName) );
      context->RemoveDataModel(m_modelName);
    }
  }

  void PlayersSelectBinder::mapPlayersData()
  {
    // No-op: the canonical GameSettings model (which playersData.players now
    // references) is already seeded with default Player records by the
    // GameSettings constructor, and persists menu intent across games. Seeding
    // here would duplicate those entries.
  }

  void PlayersSelectBinder::mapControllersData()
  {
    controllersData.controllers.clear();
    for(int i = 0; i < SDL_NumJoysticks(); i++)
    {
      if(SDL_IsGameController(i))
      {
        Controller c;
        // Store the instance id, matching controllerDeviceAttached and the
        // instance ids button events carry - the loop device index above is
        // not stable and must not be stored (was Bug 3).
        c.index = SDL_JoystickGetDeviceInstanceID(i);
        c.className = "";
        c.imageSrc = (MickSDLUtil::getPrimaryJoystickID() == c.index) ? "gamecontroller1-ready.png" : "gamecontroller-join.png";
        controllersData.controllers.push_back(c);
      }
    }
  }

  void PlayersSelectBinder::controllerDeviceAttached(int deviceIndex)
  {
    if(SDL_IsGameController(deviceIndex))
    {
      SDL_JoystickID jId = SDL_JoystickGetDeviceInstanceID(deviceIndex);
      MickLogger::getInstance()->debug(nullptr, string("PSB: # controllers: ").append(to_string(controllersData.controllers.size())).append(", attaching controller device index: ").append(to_string(deviceIndex)));
      if (controllersData.getControllerFromIndex(jId) == nullptr)
      {
        Controller c;
        c.index = jId;
        c.className = "";
        c.imageSrc = (MickSDLUtil::getPrimaryJoystickID() == jId) ? "gamecontroller1-ready.png" : "gamecontroller-join.png";
        controllersData.controllers.push_back(c);
        MickLogger::getInstance()->debug(nullptr, string("PSB: # controllers: ").append(to_string(controllersData.controllers.size())));
      }
    }
    refreshDeviceLabels(); // ordinals ("Pad N") shift when the connected list changes
    Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle().DirtyVariable("controllers");
  }

  void PlayersSelectBinder::controllerDeviceRemoved(int joystickInstanceId)
  {
    detachPlayerFromController(joystickInstanceId);
    const Controller *c = controllersData.getControllerFromIndex(joystickInstanceId);
    if(c)
    {
      MickLogger::getInstance()->debug(nullptr, string("PSB: # controllers: ").append(to_string(controllersData.controllers.size())).append(", removing controller: ").append(to_string(joystickInstanceId)));
      std::vector<Controller>::iterator it;
      it = std::find_if(controllersData.controllers.begin(), controllersData.controllers.end(), [&c](const Controller &itc) -> bool { return itc.index == c->index; } );
      if(it != controllersData.controllers.end())
      {
        controllersData.controllers.erase(it);
      }
      MickLogger::getInstance()->debug(nullptr, string("PSB: # controllers: ").append(to_string(controllersData.controllers.size())));
    }
    refreshDeviceLabels(); // ordinals ("Pad N") shift when the connected list changes
    Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle().DirtyVariable("controllers");
  }

  Player* PlayersSelectBinder::getPlayerAttachedToController(int controllerId)
  {
    return playersData.getPlayerForControllerId(controllerId);
  }

  bool PlayersSelectBinder::isKeyboardSetTaken(int setId, int exceptPlayerId)
  {
    for(auto &p : playersData.players)
    {
      if(p.id != exceptPlayerId && p.attachedKeyboardId == setId)
      {
        return true;
      }
    }
    return false;
  }

  bool PlayersSelectBinder::isControllerTaken(int controllerId, int exceptPlayerId)
  {
    for(auto &p : playersData.players)
    {
      if(p.id != exceptPlayerId && p.attachedControllerId == controllerId)
      {
        return true;
      }
    }
    return false;
  }

  void PlayersSelectBinder::refreshDeviceLabels()
  {
    for(auto &p : playersData.players)
    {
      if(! p.human)
      {
        p.deviceLabel = "CPU";
      }
      else if(p.attachedKeyboardId >= 0)
      {
        p.deviceLabel = string("Keyboard ").append(to_string(p.attachedKeyboardId + 1));
      }
      else if(p.attachedControllerId >= 0)
      {
        int ordinal = controllersData.getControllerOrdinal(p.attachedControllerId);
        p.deviceLabel = (ordinal > 0) ? string("Pad ").append(to_string(ordinal)) : "Pad";
      }
      else
      {
        // Human with neither id set: reachable transiently before
        // normalizeAssignments runs, or persistently for slots 5/6 with no
        // free pad. Distinct from "CPU" so the UI doesn't imply AI control.
        p.deviceLabel = "Unassigned";
      }
    }
    Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle().DirtyVariable("players");
  }

  Player* PlayersSelectBinder::assignPlayerControl(int playerIndex, ControlChoiceType type, int value)
  {
    Player &player = playersData.players.at(playerIndex);
    int previousControllerId = player.attachedControllerId;

    switch(type)
    {
      case ControlChoiceType::CPU:
        std::GameSettings::getInstance()->setPlayerController(playerIndex, -1);
        std::GameSettings::getInstance()->setPlayerKeyboard(playerIndex, -1);
        std::GameSettings::getInstance()->setPlayerAI(playerIndex, true); // -> AI
        break;
      case ControlChoiceType::KEYBOARD:
        std::GameSettings::getInstance()->setPlayerController(playerIndex, -1);
        std::GameSettings::getInstance()->setPlayerKeyboard(playerIndex, value);
        std::GameSettings::getInstance()->setPlayerAI(playerIndex, false); // -> human
        break;
      case ControlChoiceType::CONTROLLER:
        std::GameSettings::getInstance()->setPlayerKeyboard(playerIndex, -1);
        std::GameSettings::getInstance()->setPlayerController(playerIndex, value);
        std::GameSettings::getInstance()->setPlayerAI(playerIndex, false); // -> human
        break;
    }

    // The pad this player previously held (if any, and if different from the
    // newly assigned one) reverts to the join icon.
    if(previousControllerId != -1 && previousControllerId != value)
    {
      Controller *prev = controllersData.getControllerFromIndex(previousControllerId);
      if(prev)
      {
        prev->imageSrc = "gamecontroller-join.png";
      }
    }
    if(type == ControlChoiceType::CONTROLLER)
    {
      Controller *cur = controllersData.getControllerFromIndex(value);
      if(cur)
      {
        int ordinal = controllersData.getControllerOrdinal(value);
        cur->imageSrc = string("gamecontroller").append(to_string(ordinal)).append("-ready.png");
      }
      else
      {
        MickLogger::getInstance()->warn(nullptr, string("assignPlayerControl: no controller entry found for id ").append(to_string(value)));
      }
    }

    refreshDeviceLabels();
    refreshMapConfig();
    Rml::DataModelHandle handle = Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle();
    handle.DirtyVariable("players");
    handle.DirtyVariable("controllers");
    return &player;
  }

  Player* PlayersSelectBinder::attachPlayerToController(int playerIndex, int controllerId)
  {
    Player &player = playersData.players.at(playerIndex);
    if(! player.human && player.attachedControllerId == -1 && player.attachedKeyboardId == -1
       && ! isControllerTaken(controllerId, playerIndex))
    {
      return assignPlayerControl(playerIndex, ControlChoiceType::CONTROLLER, controllerId);
    }
    MickLogger::getInstance()->debug(nullptr, string("attachPlayerToController: playerIndex ").append(to_string(playerIndex)).append(" already attached to controllerId: ").append(to_string(player.attachedControllerId)));
    return nullptr; // no free slots ?!
  }

  Player* PlayersSelectBinder::attachPlayerToKeyboard(int keyboardSetId)
  {
    // Phase 2.5: keyboard-join. Bind keyboard set N directly to player slot N
    // (not first-free): this matches the InputHandler default-seeding convention
    // (set index == slot index) so the menu and the game can't pick different
    // slots for the same set, and gives the traditional WASD=P1 / arrows=P2 /
    // numpad=P3 / IJKL=P4 mapping. A slot already owning a controller or
    // another keyboard set is left untouched; refused if some other player
    // has since explicitly claimed this set via the Control-button cycle.
    if(keyboardSetId < 0 || keyboardSetId >= (int) playersData.players.size())
    {
      return nullptr;
    }
    Player &player = playersData.players.at(keyboardSetId);
    if(player.attachedControllerId == -1 && player.attachedKeyboardId == -1
       && ! isKeyboardSetTaken(keyboardSetId, keyboardSetId))
    {
      MickLogger::getInstance()->debug(nullptr, string("attachPlayerToKeyboard: bound keyboard set ").append(to_string(keyboardSetId)).append(" to player slot ").append(to_string(keyboardSetId)));
      return assignPlayerControl(keyboardSetId, ControlChoiceType::KEYBOARD, keyboardSetId);
    }
    MickLogger::getInstance()->debug(nullptr, string("attachPlayerToKeyboard: player slot ").append(to_string(keyboardSetId)).append(" already has a device, or the set is taken; ignoring keyboard-join"));
    return nullptr;
  }

  void PlayersSelectBinder::detachPlayerFromController(int controllerId)
  {
    Player *p = getPlayerAttachedToController(controllerId);
    if(! p)
    {
      return;
    }
    // Slot 0 never goes fully undriven if a free keyboard set exists (the
    // primary player-select column, always visible/keyboard-drivable).
    if(p->id == 0 && ! isKeyboardSetTaken(0, 0))
    {
      assignPlayerControl(0, ControlChoiceType::KEYBOARD, 0);
    }
    else
    {
      assignPlayerControl(p->id, ControlChoiceType::CPU);
    }
  }

  void PlayersSelectBinder::normalizeAssignments()
  {
    auto &players = playersData.players;

    // (c) Drop stored controller ids for devices no longer tracked in
    // controllersData (e.g. a replug that changed the connected-list state).
    // detachPlayerFromController handles the slot-0 keyboard fallback.
    for(auto &p : players)
    {
      if(p.attachedControllerId != -1 && controllersData.getControllerFromIndex(p.attachedControllerId) == nullptr)
      {
        detachPlayerFromController(p.attachedControllerId);
      }
    }

    // (a) Bug 1 fix: auto-attach the primary pad to slot 0 when it's human,
    // device-less, and the pad itself is unclaimed. Still cyclable back to
    // keyboard afterwards via cyclePlayerControl.
    Player &slot0 = players.at(0);
    if(slot0.human && slot0.attachedControllerId == -1 && slot0.attachedKeyboardId == -1)
    {
      SDL_JoystickID primary = MickSDLUtil::getPrimaryJoystickID();
      if(primary != -1 && ! isControllerTaken(primary, 0))
      {
        assignPlayerControl(0, ControlChoiceType::CONTROLLER, primary);
      }
    }

    // (b) Any device-less human: first free keyboard set for slots 0-3
    // (prefer set == slot index), CPU otherwise (slots 4-5, or no sets left).
    // Includes slot 0: (a) above only assigns it a controller when a free
    // primary pad exists, so a device-less slot 0 (the common no-pad case)
    // still needs to fall through to a keyboard set here, same as any other
    // slot - otherwise it's left "Unassigned" instead of defaulting to
    // Keyboard 1.
    for(auto &p : players)
    {
      if(! p.human || p.attachedControllerId != -1 || p.attachedKeyboardId != -1)
      {
        continue;
      }
      int chosen = -1;
      if(p.id < std::GameSettings::MAX_KEYBOARD_PLAYERS)
      {
        if(! isKeyboardSetTaken(p.id, p.id))
        {
          chosen = p.id;
        }
        else
        {
          for(int set = 0; set < std::GameSettings::MAX_KEYBOARD_PLAYERS; ++set)
          {
            if(! isKeyboardSetTaken(set, p.id))
            {
              chosen = set;
              break;
            }
          }
        }
      }
      assignPlayerControl(p.id, chosen != -1 ? ControlChoiceType::KEYBOARD : ControlChoiceType::CPU, chosen);
    }

    refreshDeviceLabels();
  }
}
