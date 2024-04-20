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

  } controllersData;

  struct Players {
    std::vector<Player> players = {};

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
      int origNumPlayers = players.size();
      int newPlayers = players.size();
      if (++newPlayers > std::GameSettings::MAX_PLAYERS)
      {
        newPlayers = std::GameSettings::MIN_PLAYERS; // wrap it back to the minimum
        players.clear();
        origNumPlayers = 0;
      }

      for(int i = origNumPlayers; i < newPlayers; i++)
      {
        Player p;
        p.human = (i == 0);
        p.id = i;
        p.team = i;
        p.type = (i % 2 == 0) ?  MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE;
        p.portrait = MickAssets::getPortraitBitmapFilename(i, p.type);
        players.push_back(p);
      }

      std::GameSettings::getInstance()->setNumberOfPlayers(players.size());
      MickLogger::getInstance()->debug(this, std::string("Players.toggleNumPlayers=").append(to_string(players.size())));
      model_handle.DirtyVariable("players");
	  }

    void togglePlayerAI(Rml::DataModelHandle model_handle, Rml::Event& ev, const Rml::VariantList& /*arguments*/) {
      int playerId = getPlayerIdAssociatedWithEvent(ev);

      bool playerIdIsCurrentlyAI = players.at(playerId).human == 0;
      players.at(playerId).human = (playerIdIsCurrentlyAI) ? 1 : 0; // if they are AI change to human and vice-versa

      std::GameSettings::getInstance()->setPlayerAI(playerId, players.at(playerId).human == 0);
      MickLogger::getInstance()->debug(this, std::string("Players.togglePlayerAI[").append(to_string(playerId)).append("]=").append(to_string(players.at(playerId).human)));
      model_handle.DirtyVariable("players");
	  }

    void togglePlayerPortrait(Rml::DataModelHandle model_handle, Rml::Event& ev, const Rml::VariantList& /*arguments*/) {
      int playerId = getPlayerIdAssociatedWithEvent(ev);

      MickAssets::PlayerType newType = MickAssets::getNextPlayerType(players.at(playerId).type);
      string portaitFile = MickAssets::getPortraitBitmapFilename(playerId, newType);
      players.at(playerId).type = newType;
      players.at(playerId).portrait = portaitFile.c_str();

      std::GameSettings::getInstance()->setPlayerGender(playerId, newType == MickAssets::PlayerType::MALE);
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
    }

    constructor.RegisterArray<std::vector<Player>>();

    constructor.Bind("players", &playersData.players);
    constructor.BindEventCallback("toggleNumPlayers", &Players::toggleNumPlayers, &playersData);
    constructor.BindEventCallback("togglePlayerAI", &Players::togglePlayerAI, &playersData);
    constructor.BindEventCallback("togglePlayerPortrait", &Players::togglePlayerPortrait, &playersData);

    if(auto controllerHandle = constructor.RegisterStruct<Controller>())
    {
      controllerHandle.RegisterMember("index", &Controller::index);
      controllerHandle.RegisterMember("className", &Controller::className);
      controllerHandle.RegisterMember("imageSrc", &Controller::imageSrc);
    }
    constructor.RegisterArray<std::vector<Controller>>();

    constructor.Bind("controllers", &controllersData.controllers);

    m_dataModelHandle = constructor.GetModelHandle();

    return true;
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
    for(int i = 0; i < GameSettings::getInstance()->getNumberOfPlayers(); i++)
    {
      Player p;
      p.human = (i == 0);
      p.id = i;
      p.team = i;
      p.type = (i % 2 == 0) ? MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE;
      p.portrait = MickAssets::getPortraitBitmapFilename(i, p.type);
      playersData.players.push_back(p);
    }
  }

  void PlayersSelectBinder::mapControllersData()
  {
    controllersData.controllers.clear();
    for(int i = 0; i < SDL_NumJoysticks(); i++)
    {
      if(SDL_IsGameController(i))
      {
        Controller c;
        c.index = i;
        c.className = "";
        c.imageSrc = (i == 0) ? "gamecontroller1-ready.png" : "gamecontroller-join.png";
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
    Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle().DirtyVariable("controllers");
  }

  Player* PlayersSelectBinder::getPlayerAttachedToController(int controllerId)
  {
    return playersData.getPlayerForControllerId(controllerId);
  }

  Player* PlayersSelectBinder::attachPlayerToController(int playerIndex, int controllerId)
  {
    Player &player = playersData.players[playerIndex];
    if(! player.human && player.attachedControllerId == -1 && player.attachedKeyboardId == -1)
    {
      player.attachedControllerId = controllerId;
      player.human = true;
      string imageFilename = string("gamecontroller").append(to_string(controllerId+1)).append("-ready.png");
      controllersData.getControllerFromIndex(controllerId)->imageSrc = imageFilename.c_str();
      MickLogger::getInstance()->debug(nullptr, string("attachPlayerToController: playerIndex ").append(to_string(playerIndex)).append(" setting to image: ").append(imageFilename));
      Rml::GetContext("default")->GetDataModel("playersSelect").GetModelHandle().DirtyVariable("controllers");
      return &player;
    }
    else 
    {
      MickLogger::getInstance()->debug(nullptr, string("attachPlayerToController: playerIndex ").append(to_string(playerIndex)).append(" already attached to controllerId: ").append(to_string(player.attachedControllerId)));
    }
    return nullptr; // no free slots ?!
  }

  void PlayersSelectBinder::detachPlayerFromController(int controllerId)
  {
    Player *p = getPlayerAttachedToController(controllerId);
    if(p)
    {
      p->attachedControllerId = -1;
      p->human = false;
      controllersData.getControllerFromIndex(controllerId)->imageSrc = "gamecontroller-join.png";
    }
  }
}
