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

#include <stdio.h>
#include <stdlib.h>
#include <RmlUi/Core.h>
#include <cstddef>

class MickRmlUIElementUtil
{
  public:
    static const char* TAB_INDEX;
    static const char* TAB_CLASS; // all elements that have a tabindex should have this class
    static const char* ANY_PLAYER_HIGHLIGHTED_CLASS; // if an element is highlighted by any player it should have this class
    static const char* ANY_PLAYER_SELECTED_CLASS; // if a player has chosen a character then this class will be applied.
    static const char* JOIN_PLAYER_CLASS; // an element that we can begin highlighted on a controller join

    MickRmlUIElementUtil();
    static int getTabIndex(Rml::Element* element);
    static int getAttributeValueAsInt(Rml::Element* element, const std::string attributeName);
    
    static Rml::Element* getElementChosenByControllerId(Rml::ElementDocument *document, int controllerId); // When a controller has selected a player, this will return the element they have selected
    static void setPlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId); 
    static void cancelPlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId); 
    static void togglePlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId, bool setChosen);

    static Rml::Element* getChildElementWithClassName(Rml::Element* searchFromParent, const std::string className); // 
    static Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    static Rml::Element* getElementWithTabIndex(Rml::ElementDocument* document, int tabIndex);
    static Rml::Element* getParentElementWithAttributeName(Rml::Element* nodeToSearchFrom, const std::string attributeName);
    static void appendTextToTextNode(Rml::Element* element, std::string toAppend);
    static void replaceEndStringInTextNode(Rml::Element* element, std::string toReplace);

    static Rml::Element* getElementWithRowAndColValues(Rml::ElementDocument *search, int row, int col);
    static Rml::Element* getFirstElementWithAttribute(Rml::Element *searchFrom, std::string attributeName);
    static const std::string getControllerHighlightClass(int controllerId);
    static Rml::Element* getElementHighlightedByControllerId(Rml::Context *context, int controllerId); // checks for class playerXhighlight on an element and returns it
    
    static void focusNextElement(Rml::Context *context, int havingPlayerId = -1); // optionally specify a playerId that the element must have
    static void focusPrevElement(Rml::Context *context, int havingPlayerId = -1); // optionally specify a playerId that the element must have
    static void focusElementWithTabIndex(Rml::Context *context, int tabIndex, int havingPlayerId = -1); // optionally specify a playerId attr value that the element must have
    static void setFocusElementForController(Rml::Element *element, int controllerId, bool activate); // adds playerXhighlight and selected classes to the specified element.
    
    static void navigateLeftForController(Rml::Element *leftFromElement, int controllerId);
    static void navigateRightForController(Rml::Element *rightFromElement, int controllerId);

    static void menuBackButtonTriggered(Rml::Context *context);

    virtual ~MickRmlUIElementUtil();
};

#endif /* MICKROCKETELEMENTUTIL_H_ */
