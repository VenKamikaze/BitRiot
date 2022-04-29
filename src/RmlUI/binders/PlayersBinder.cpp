
#include "PlayersBinder.h"
#include "GameSettings.h"
#include "MickAssets.h"
#include "MickLogger.h"
#include <cstddef>

namespace std
{

  struct Player {
    int id;
    int team;
    std::string portrait;
    int human = 0; // 1 for true
  };

  struct Players {
    std::vector<Player> players = {};
   
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
        p.portrait = MickAssets::getPortraitBitmapFilename(i, (i % 2 == 0) ? MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE);
        players.push_back(p);
      }

      std::GameSettings::getInstance()->setNumberOfPlayers(players.size());
      MickLogger::getInstance()->debug(this, std::string("Players.toggleNumPlayers=").append(to_string(players.size())));
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
      p.portrait = MickAssets::getPortraitBitmapFilename(i, (i % 2 == 0) ? MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE);
      playersData.players.push_back(p);
    }
  }
}
