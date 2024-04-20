/*
 * MickRmlUIElementUtil.cpp
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#include "MickRmlUIElementUtil.h"
#include "MickLogger.h"
#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include <RmlUi/Config/Config.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Types.h>
#include <cassert>
#include <sstream>
#include <string>


const char* MickRmlUIElementUtil::TAB_INDEX = "tabindex";
const char* MickRmlUIElementUtil::TAB_CLASS = "tabbed"; // all elements that have a tabindex should have this class
const char* MickRmlUIElementUtil::ANY_PLAYER_HIGHLIGHTED_CLASS = "playerhighlight"; // if an element is highlighted by any player it should have this class
const char* MickRmlUIElementUtil::ANY_PLAYER_SELECTED_CLASS = "playerselected"; // if a player has chosen a character then this class will be applied.
const char* MickRmlUIElementUtil::JOIN_PLAYER_CLASS = "joinplayer"; // an element that we can begin highlighted on a controller join

MickRmlUIElementUtil::MickRmlUIElementUtil()
{
  // TODO Auto-generated constructor stub

}

int MickRmlUIElementUtil::getTabIndex(Rml::Element* element)
{
  if (element->HasAttribute(TAB_INDEX))
  {
    Rml::String tabIndex = element->GetAttribute(TAB_INDEX)->Get<Rml::String>();
    return atoi(tabIndex.c_str());
  }
  return 0;
}

int MickRmlUIElementUtil::getAttributeValueAsInt(Rml::Element* element, const std::string attributeName)
{
  if (element->HasAttribute(attributeName))
  {
    Rml::String v = element->GetAttribute(attributeName)->Get<Rml::String>();
    return atoi(v.c_str());
  }
  return -1;
}

/**
 * When a controller has selected a player, this will return the element they have selected
 */ 
Rml::Element *MickRmlUIElementUtil::getElementChosenByControllerId(Rml::ElementDocument *document, int controllerId)
{
  Rml::ElementList eList;
  document->GetElementsByClassName(eList, getControllerHighlightClass(controllerId));
  bool found = false;
  Rml::Element *elementChosen = nullptr;
  for (auto el : eList)
  {
    assert(found == false); // make sure we don't have two elements chosen by the same controller.
    found = (el != nullptr);
    elementChosen = el;
  }
  return elementChosen;
}

/**
 * When a controller has selected a player and button 'B' is pressed, this will remove the player selected class to allow
 * them to switch to another player.
 */ 
void MickRmlUIElementUtil::setPlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId)
{
  togglePlayerChosenForControllerId(chosenElement, controllerId, true);
}

void MickRmlUIElementUtil::togglePlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId, bool setChosen)
{
  if(chosenElement)
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("togglePlayerChosenForControllerId. ID: ").append(chosenElement->GetId()).c_str());
    Rml::Element* portraitElement = nullptr;
    if(chosenElement->GetId().find("img-id") != std::string::npos )
    {
      std::MickLogger::getInstance()->debug(nullptr, std::string("togglePlayerChosenForControllerId img found").c_str());
      portraitElement = chosenElement;
    }
    else if (chosenElement->GetId().find("btn-ai") != std::string::npos) 
    {
      std::string chosenElPlayerId = chosenElement->GetAttribute("playerId")->Get<Rml::String>();
      std::MickLogger::getInstance()->debug(nullptr, std::string("togglePlayerChosenForControllerId btn found, trying to get img-id-").append(chosenElPlayerId).c_str());
      portraitElement = chosenElement->GetElementById(std::string("img-id-").append(chosenElPlayerId));
    }
    if(portraitElement)
    {
      std::MickLogger::getInstance()->debug(nullptr, std::string("togglePlayerChosenForControllerId setting selected").c_str());
      portraitElement->SetClass(ANY_PLAYER_SELECTED_CLASS, setChosen);
    }
  }
}

/**
 * When a controller has selected a player and button 'B' is pressed, this will remove the player selected class to allow
 * them to switch to another player.
 */ 
void MickRmlUIElementUtil::cancelPlayerChosenForControllerId(Rml::Element *chosenElement, int controllerId)
{
  togglePlayerChosenForControllerId(chosenElement, controllerId, false);
}


Rml::Element* MickRmlUIElementUtil::getChildElementWithClassName(Rml::Element* searchFromParent, const std::string className)
{
  std::string classNames = searchFromParent->GetClassNames();
  std::MickLogger::getInstance()->debug(nullptr, std::string(searchFromParent->GetId()).append(" classes: ").append(searchFromParent->GetClassNames()));
  if(classNames.find(className) != std::string::npos)
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("found class on el: ").append(searchFromParent->GetId()));
    return searchFromParent;
  }
  if(searchFromParent && searchFromParent->HasChildNodes())
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("getChildElementWithClassName: Element ID: ").append(searchFromParent->GetId()).append(" has # children: ").append(std::to_string(searchFromParent->GetNumChildren(false))));
    for (int i = 0; i < searchFromParent->GetNumChildren(false); i++)
    {
      if (searchFromParent->GetChild(i) != nullptr)
      {
        Rml::Element* foundEl = getChildElementWithClassName(searchFromParent->GetChild(i), className);
        if(foundEl)
        {
          return foundEl;
        }
      }
    }
  }
  return nullptr; // nothing found.
}

Rml::Element* MickRmlUIElementUtil::getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex)
{
  for (int i = 0; i < parentNode->GetNumChildren(false); i++)
  {
    if (parentNode->GetChild(i) != nullptr && parentNode->GetChild(i)->GetAttribute(TAB_INDEX) != nullptr)
    {
      Rml::String siblingTabIndex = parentNode->GetChild(i)->GetAttribute(TAB_INDEX)->Get<Rml::String>();
      if (tabIndex == atoi(siblingTabIndex.c_str()))
      {
        return parentNode->GetChild(i);
      }
    }
  }
  return nullptr;
}

Rml::Element* MickRmlUIElementUtil::getElementWithTabIndex(Rml::ElementDocument* document, int tabIndex)
{
  Rml::ElementList eList;
  document->GetElementsByClassName(eList, TAB_CLASS);
  for (auto el : eList)
  {
    if (el->GetAttribute(TAB_INDEX) && atoi(el->GetAttribute(TAB_INDEX)->Get<Rml::String>().c_str()) == tabIndex)
    {
      return el;
    }
  }
  return nullptr;
}

// Recursively search up until we either find the element with the attribute, or we exhaust our search.
Rml::Element* MickRmlUIElementUtil::getParentElementWithAttributeName(Rml::Element* nodeToSearchFrom, const std::string attributeName)
{
  if (nodeToSearchFrom->GetParentNode() && nodeToSearchFrom->GetParentNode()->HasAttribute(attributeName))
  {
    return nodeToSearchFrom->GetParentNode();
  }
  else 
  {
    return getParentElementWithAttributeName(nodeToSearchFrom->GetParentNode(), attributeName);
  }
  return nullptr;
}

void MickRmlUIElementUtil::appendTextToTextNode(Rml::Element* element, std::string toAppend)
{
  Rml::ElementText* textNode = dynamic_cast<Rml::ElementText*>(element->GetFirstChild());
  //Rml::String utf8_string;
  //textNode->GetText().ToUTF8(utf8_string);
  //utf8_string.Append(Rml::String(toAppend.c_str()));
  std::string textInNode = textNode->GetText();
  textInNode.append(toAppend);
  textNode->SetText(textInNode.c_str());
}

void MickRmlUIElementUtil::replaceEndStringInTextNode(Rml::Element* element, std::string toReplace)
{
  Rml::ElementText* textNode = dynamic_cast<Rml::ElementText*>(element->GetFirstChild());
  Rml::String utf8_string;
  utf8_string = textNode->GetText().c_str(); //textNode->GetText().ToUTF8(utf8_string);
  utf8_string.resize(utf8_string.find(":") +2);
  //utf8_string.Resize(utf8_string.Find(":") +2); // replace one space after the colon
  utf8_string.append(toReplace.c_str());
  //utf8_string.Append(Rml::String(toReplace.c_str()));
  textNode->SetText(utf8_string);
}

Rml::Element* MickRmlUIElementUtil::getElementWithRowAndColValues(Rml::ElementDocument *search, int row, int col)
{
  Rml::ElementList eList;
  search->GetElementsByClassName(eList, TAB_CLASS);
  for(const auto& el : eList)
  {
    if(el->HasAttribute("row") && el->HasAttribute("col"))
    {
      int elRow = MickRmlUIElementUtil::getAttributeValueAsInt(el, "row");
      int elCol = MickRmlUIElementUtil::getAttributeValueAsInt(el, "col");
      if(elRow == row && elCol == col)
      {
        return el;
      }
    }
  }
  return nullptr;
}

Rml::Element* MickRmlUIElementUtil::getFirstElementWithAttribute(Rml::Element *searchFrom, const std::string attributeName)
{
  Rml::Element *foundElement = nullptr;
  if(searchFrom->HasAttribute(attributeName))
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("Found ! Element has ID: ").append(searchFrom->GetId()));
    return searchFrom; // return the first element we find in the graph that has this attributeName
  }
  if(searchFrom && searchFrom->HasChildNodes())
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("Element ID: ").append(searchFrom->GetId()).append(" has # children: ").append(std::to_string(searchFrom->GetNumChildren(false))));
    for (int i = 0; i < searchFrom->GetNumChildren(false); i++)
    {
      if (searchFrom->GetChild(i) != nullptr)
      {
        foundElement = getFirstElementWithAttribute(searchFrom->GetChild(i), attributeName);
        if(foundElement)
        {
          return foundElement;
        }
      }
    }
  }
  return nullptr; // nothing found.
}

void MickRmlUIElementUtil::focusNextElement(Rml::Context *context, int havingPlayerId)
{
  int nextTabIndex = MickRmlUIElementUtil::getTabIndex(context->GetFocusElement()) + 1;
  focusElementWithTabIndex(context, nextTabIndex);
}

void MickRmlUIElementUtil::focusPrevElement(Rml::Context *context, int havingPlayerId)
{
  int previousTabIndex = MickRmlUIElementUtil::getTabIndex(context->GetFocusElement()) - 1;
  focusElementWithTabIndex(context, previousTabIndex);
}

void MickRmlUIElementUtil::focusElementWithTabIndex(Rml::Context *context, int tabIndex, int havingPlayerId)
{
  Rml::Element *currentElement = context->GetFocusElement();
  Rml::Element *nextElement = MickRmlUIElementUtil::getElementWithTabIndex(currentElement->GetOwnerDocument(), tabIndex);
  if(nextElement)
  {
    if(havingPlayerId == -1) // normal navigation
    {
      nextElement->Focus(); 
    }
    else // "fake" focus by applying classes to elements so other players can see what elements they are "highlighting"
    {
      if( nextElement->HasAttribute("playerId") && atoi(nextElement->GetAttribute("playerId")->Get<Rml::String>().c_str()) == havingPlayerId)
      {
        setFocusElementForController(currentElement, havingPlayerId, false);
        setFocusElementForController(nextElement, havingPlayerId, true);
      }
    }
  }
}

void MickRmlUIElementUtil::setFocusElementForController(Rml::Element *element, int controllerId, bool activate)
{
  if(element)
  {
    element->SetClass(getControllerHighlightClass(controllerId), activate);
    element->SetClass(ANY_PLAYER_HIGHLIGHTED_CLASS, activate);
  }
}

void MickRmlUIElementUtil::navigateLeftForController(Rml::Element *leftFromElement, int controllerId)
{
  if (leftFromElement && leftFromElement->HasAttribute("row") && leftFromElement->HasAttribute("col"))
  {
    int currentCol = getAttributeValueAsInt(leftFromElement, "col");
    int currentRow = getAttributeValueAsInt(leftFromElement, "row");
    Rml::Element *element = getElementWithRowAndColValues(leftFromElement->GetOwnerDocument(), currentRow, --currentCol);

    if(element && ( element->GetClassNames().find(ANY_PLAYER_SELECTED_CLASS) == std::string::npos))
    {
      setFocusElementForController(leftFromElement, controllerId, false);
      setFocusElementForController(element, controllerId, true);
    }
  }
}

void MickRmlUIElementUtil::navigateRightForController(Rml::Element *rightFromElement, int controllerId)
{
  if (rightFromElement && rightFromElement->HasAttribute("row") && rightFromElement->HasAttribute("col"))
  {
    int currentCol = getAttributeValueAsInt(rightFromElement, "col");
    int currentRow = getAttributeValueAsInt(rightFromElement, "row");
    Rml::Element *element = getElementWithRowAndColValues(rightFromElement->GetOwnerDocument(), currentRow, ++currentCol);

    if(element && ( element->GetClassNames().find(ANY_PLAYER_SELECTED_CLASS) == std::string::npos))
    {
      setFocusElementForController(rightFromElement, controllerId, false);
      setFocusElementForController(element, controllerId, true);
    }
  }
}

const std::string MickRmlUIElementUtil::getControllerHighlightClass(int controllerId)
{
  std::stringstream ss;
  ss << "controller" << controllerId << "highlight";
  return ss.str().c_str();
}

Rml::Element* MickRmlUIElementUtil::getElementHighlightedByControllerId(Rml::Context *context, int controllerId) // checks for class controllerXhighlight on an element and returns it
{
  Rml::ElementList eList;
  context->GetFocusElement()->GetOwnerDocument()->GetElementsByClassName(eList, getControllerHighlightClass(controllerId));
  for (auto el : eList)
  {
    return el; // return the first.
  }
  return nullptr;
}

void MickRmlUIElementUtil::menuBackButtonTriggered(Rml::Context *context)
{
  RmlUiSDL2SystemInterface* systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();
  Rml::ElementDocument *document = context->GetFocusElement()->GetOwnerDocument();
  if(document)
  {
    std::MickLogger::getInstance()->debug(nullptr, std::string("current document id: ").append(document ? document->GetId() : "null") );
    Rml::Element* escapeActionElement = MickRmlUIElementUtil::getFirstElementWithAttribute(document, "escapeAction");
    if(escapeActionElement)
    {
      std::MickLogger::getInstance()->debug(nullptr, std::string("escapeActionElement element id: ").append(escapeActionElement->GetId()) );
      // process the element with the attribute 'escapeAction'
      escapeActionElement->Focus();
      context->ProcessKeyDown(systemInterface->TranslateKey(SDLK_RETURN), 0);
    }
  }
}



MickRmlUIElementUtil::~MickRmlUIElementUtil()
{
  // TODO Auto-generated destructor stub
}

