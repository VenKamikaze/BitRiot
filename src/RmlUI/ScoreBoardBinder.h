/*
 * MickRmlUIElementUtil.h
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#ifndef SCOREBOARDBINDER_H_
#define SCOREBOARDBINDER_H_


#include <RmlUi/Core/DataModelHandle.h>
#include <string>
#include "PlayerCharacterEntity.h"
#include "RuntimeException.h"
#include "MickLogger.h"
#include "DataReader.h"

#include <SDL2/SDL.h>
//#include <GL/glew.h>

#include <RmlUi/Core.h>

namespace std
{
  class ScoreBoardBinder
  {
    public:
      ScoreBoardBinder(Rml::Context* rmlContext, PlayerCharacterEntity* winner, int gameTotalLength);
      static void mapScoreBoardData(PlayerCharacterEntity* winner, int gameTotalLength);
      void RemoveDataBinding(Rml::Context *context);
    private:
      bool SetupDataBinding(Rml::Context* context);
      static const string randomWinText();
      std::string m_modelName = "scoreBoard";
      Rml::DataModelHandle m_dataModelHandle = nullptr;
  };
}; /* namespace std */

#endif /* SCOREBOARDBINDER_H_ */
