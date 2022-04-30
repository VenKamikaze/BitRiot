
#include "PlayersBinder.h"
#include "GameSettings.h"
#include "MickAssets.h"
#include "MickLogger.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include <RmlUi/Core/Element.h>
#include <cassert>
#include <cstddef>

namespace std
{

  struct Player {
    int id;
    int team;
    std::string portrait;
    MickAssets::PlayerType type;
    int human = 0; // 1 for true
  };

  struct Players {
    std::vector<Player> players = {};
   
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

  PlayersBinder::PlayersBinder(Rml::Context* rmlContext)
  {
    PlayersBinder::mapPlayersData();
    setupDataBinding(rmlContext);
  }

  bool PlayersBinder::setupDataBinding(Rml::Context* context)
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

    m_dataModelHandle = constructor.GetModelHandle();

    return true;
  }

  void PlayersBinder::removeDataBinding(Rml::Context *context)
  {
    if(context->GetDataModel(m_modelName))
    {
      MickLogger::getInstance()->debug(this, string("Removing data binding for ").append(m_modelName) );
      context->RemoveDataModel(m_modelName);
    }
  }

  void PlayersBinder::mapPlayersData()
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
}
