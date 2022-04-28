/*
 * MickRmlUIElementUtil.h
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#ifndef PLAYERSBINDER_H_
#define PLAYERSBINDER_H_


#include <RmlUi/Core/DataModelHandle.h>
#include <string>
#include "MickLogger.h"

#include <RmlUi/Core.h>

namespace std
{
  class PlayersBinder
  {
    public:
      PlayersBinder(Rml::Context* rmlContext);
      static void mapPlayersData();
      void removeDataBinding(Rml::Context *context);
    private:
      bool setupDataBinding(Rml::Context* context);
      static const string randomWinText();
      std::string m_modelName = "playersSelect";
      Rml::DataModelHandle m_dataModelHandle = nullptr;
  };
}; /* namespace std */

#endif /* PLAYERSBINDER_H_ */
