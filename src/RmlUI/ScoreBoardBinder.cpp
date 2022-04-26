
#include "ScoreBoardBinder.h"
#include "MickLogger.h"
#include <cstddef>

namespace std
{

  struct ScoreBoardData 
  {
    Rml::String playerWin = "";
    int gameLengthSeconds = 0;
    int numberEggsPlaced = 0;
    int numberBombsPlaced = 0;
    int playerWinNumber = 0;
  } m_scores;

  ScoreBoardBinder::ScoreBoardBinder(Rml::Context* rmlContext, shared_ptr<PlayerCharacterEntity> winner, int gameTotalLength)
  {
    ScoreBoardBinder::mapScoreBoardData(winner, gameTotalLength);
    SetupDataBinding(rmlContext);
  }

  bool ScoreBoardBinder::SetupDataBinding(Rml::Context* context)
  { 
    Rml::DataModelConstructor constructor = context->CreateDataModel(m_modelName);

    if (!constructor)
    {
      return false;
    }

    constructor.Bind("playerWin", &m_scores.playerWin);
    constructor.Bind("numberEggsPlaced", &m_scores.numberEggsPlaced);
    constructor.Bind("numberBombsPlaced", &m_scores.numberBombsPlaced);
    constructor.Bind("gameLengthSeconds", &m_scores.gameLengthSeconds);
    constructor.Bind("playerWinNumber", &m_scores.playerWinNumber);

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

  void ScoreBoardBinder::mapScoreBoardData(shared_ptr<PlayerCharacterEntity> winner, int gameTotalLength)
  {
    std::ostringstream ss;
    m_scores.gameLengthSeconds = gameTotalLength;
    if(winner)
    {
      ss << "Player " << winner->getTeam() << " " << ScoreBoardBinder::randomWinText();
      m_scores.numberBombsPlaced = winner->getNumberBombsPlaced();
      m_scores.numberEggsPlaced = winner->getNumberEggsPlaced();
      m_scores.playerWinNumber = winner->getTeam();
    }
    else
    {
      ss << "It was a draw!";
    }
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
