/*
 * MickRmlUIElementUtil.h
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#ifndef MICKROCKETELEMENTUTIL_H_
#define MICKROCKETELEMENTUTIL_H_


#include <RmlUi/Core/ElementDocument.h>
#include <string>
#include "RuntimeException.h"

#include <SDL2/SDL.h>
//#include <GL/glew.h>

#include <stdio.h>
#include <stdlib.h>
#include <RmlUi/Core.h>

class MickRmlUIElementUtil
{
  public:
    MickRmlUIElementUtil();

    static int getTabIndex(Rml::Element* element);
    static Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    static void appendTextToTextNode(Rml::Element* element, std::string toAppend);
    static void replaceEndStringInTextNode(Rml::Element* element, std::string toReplace);
    static Rml::Element* getFirstElementWithAttribute(Rml::Element *searchFrom, std::string attributeName);

    virtual ~MickRmlUIElementUtil();
};

#endif /* MICKROCKETELEMENTUTIL_H_ */
