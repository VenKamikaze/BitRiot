/*
 * MickRmlUIElementUtil.cpp
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#include "MickRmlUIElementUtil.h"

static const char* TAB_INDEX = "tabindex";

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


MickRmlUIElementUtil::~MickRmlUIElementUtil()
{
  // TODO Auto-generated destructor stub
}

