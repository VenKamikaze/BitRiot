/*
 * MickRocketElementUtil.h
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#ifndef MICKROCKETELEMENTUTIL_H_
#define MICKROCKETELEMENTUTIL_H_


#include <string>
#include "RuntimeException.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>

#include <Rocket/Core.h>

class MickRocketElementUtil
{
  public:
    MickRocketElementUtil();

    static int getTabIndex(Rocket::Core::Element* element);
    static Rocket::Core::Element* getChildElementWithTabIndex(Rocket::Core::Element* parentNode, int tabIndex);
    static void appendTextToTextNode(Rocket::Core::Element* element, std::string toAppend);
    static void replaceEndStringInTextNode(Rocket::Core::Element* element, std::string toReplace);

    virtual ~MickRocketElementUtil();
};

#endif /* MICKROCKETELEMENTUTIL_H_ */
