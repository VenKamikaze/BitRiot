
#include "ScoreBoardBinder.h"
#include "MickLogger.h"
#include <cstddef>
#include <algorithm>

namespace std
{

  // One row per player on the end-game scoreboard, grouped by team.
  struct PlayerScoreRow
  {
    int playerNumber = 0;
    int teamNumber = 0;
    int eggs = 0;
    int bombs = 0;
    int survived = 0; // 1 if still alive at game end
    int winner = 0;   // 1 if on the winning team
  };

  struct ScoreBoardData
  {
    Rml::String playerWin = "";
    int gameLengthSeconds = 0;
    int winningTeamNumber = 0;
    std::vector<PlayerScoreRow> playerScores;
  } m_scores;

  ScoreBoardBinder::ScoreBoardBinder(Rml::Context* rmlContext, shared_ptr<PlayerCharacterEntity> winner, vector<shared_ptr<PlayerCharacterEntity>> allPlayers, int gameTotalLength)
  {
    ScoreBoardBinder::mapScoreBoardData(winner, allPlayers, gameTotalLength);
    SetupDataBinding(rmlContext);
  }

  bool ScoreBoardBinder::SetupDataBinding(Rml::Context* context)
  {
    Rml::DataModelConstructor constructor = context->CreateDataModel(m_modelName);

    if (!constructor)
    {
      return false;
    }

    if(auto rowHandle = constructor.RegisterStruct<PlayerScoreRow>())
    {
      rowHandle.RegisterMember("playerNumber", &PlayerScoreRow::playerNumber);
      rowHandle.RegisterMember("teamNumber", &PlayerScoreRow::teamNumber);
      rowHandle.RegisterMember("eggs", &PlayerScoreRow::eggs);
      rowHandle.RegisterMember("bombs", &PlayerScoreRow::bombs);
      rowHandle.RegisterMember("survived", &PlayerScoreRow::survived);
      rowHandle.RegisterMember("winner", &PlayerScoreRow::winner);
    }
    constructor.RegisterArray<std::vector<PlayerScoreRow>>();

    constructor.Bind("playerWin", &m_scores.playerWin);
    constructor.Bind("gameLengthSeconds", &m_scores.gameLengthSeconds);
    constructor.Bind("winningTeamNumber", &m_scores.winningTeamNumber);
    constructor.Bind("playerScores", &m_scores.playerScores);

    m_dataModelHandle = constructor.GetModelHandle();

    return true;
  }

  void ScoreBoardBinder::RemoveDataBinding(Rml::Context *context)
  {
    if(context->GetDataModel(m_modelName))
    {
      MickLogger::getInstance()->debug(this, string("Removing data binding for ").append(m_modelName) );
      context->RemoveDataModel(m_modelName);
    }
  }

  void ScoreBoardBinder::mapScoreBoardData(shared_ptr<PlayerCharacterEntity> winner, vector<shared_ptr<PlayerCharacterEntity>> allPlayers, int gameTotalLength)
  {
    std::ostringstream ss;
    m_scores.gameLengthSeconds = gameTotalLength;
    m_scores.winningTeamNumber = winner ? winner->getTeamNumber() : 0;

    if(winner)
    {
      ss << "Team " << m_scores.winningTeamNumber << " " << ScoreBoardBinder::randomWinText();
    }
    else
    {
      ss << "It was a draw!";
    }

    // Build one row per player, grouped by team (sorted by team, then player).
    m_scores.playerScores.clear();
    for(const auto& player : allPlayers)
    {
      PlayerScoreRow row;
      row.playerNumber = player->getPlayerNumber();
      row.teamNumber = player->getTeamNumber();
      row.eggs = player->getNumberEggsPlaced();
      row.bombs = player->getNumberBombsPlaced();
      row.survived = player->isAlive() ? 1 : 0;
      row.winner = (m_scores.winningTeamNumber != 0 && row.teamNumber == m_scores.winningTeamNumber) ? 1 : 0;
      m_scores.playerScores.push_back(row);
    }
    std::sort(m_scores.playerScores.begin(), m_scores.playerScores.end(),
      [](const PlayerScoreRow& a, const PlayerScoreRow& b) {
        if(a.teamNumber != b.teamNumber) return a.teamNumber < b.teamNumber;
        return a.playerNumber < b.playerNumber;
      });

    MickLogger::getInstance()->debug(nullptr, ss.str().c_str());
    m_scores.playerWin = ss.str();
  }

  const string ScoreBoardBinder::randomWinText()
  {
    int val = rand() % 5;
    switch(val)
    {
      case 0:
        return DataReader::getInstance()->getStringFromFile("WIN_TEXT_1", "data/messages.txt").c_str();
        break;

      case 1:
        return DataReader::getInstance()->getStringFromFile("WIN_TEXT_2", "data/messages.txt").c_str();
        break;

      case 2:
        return DataReader::getInstance()->getStringFromFile("WIN_TEXT_3", "data/messages.txt").c_str();
        break;

      case 3:
        return DataReader::getInstance()->getStringFromFile("WIN_TEXT_4", "data/messages.txt").c_str();
        break;
      
      default:
        return DataReader::getInstance()->getStringFromFile("WIN_TEXT_5", "data/messages.txt").c_str();
        break;
    }
    return "won";
  }
}
