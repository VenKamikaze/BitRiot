/*
 * MickRocketElementUtil.cpp
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#include "MickRocketElementUtil.h"

static const char* TAB_INDEX = "tabindex";

MickRocketElementUtil::MickRocketElementUtil()
{
  // TODO Auto-generated constructor stub

}

int MickRocketElementUtil::getTabIndex(Rocket::Core::Element* element)
{
  if (element->HasAttribute(TAB_INDEX))
  {
    Rocket::Core::String tabIndex = element->GetAttribute(TAB_INDEX)->Get<Rocket::Core::String>();
    return atoi(tabIndex.CString());
  }
  return 0;
}

Rocket::Core::Element* MickRocketElementUtil::getChildElementWithTabIndex(Rocket::Core::Element* parentNode, int tabIndex)
{
  for (int i = 0; i < parentNode->GetNumChildren(false); i++)
  {
    if (parentNode->GetChild(i) != NULL && parentNode->GetChild(i)->GetAttribute(TAB_INDEX) != NULL)
    {
      Rocket::Core::String siblingTabIndex = parentNode->GetChild(i)->GetAttribute(TAB_INDEX)->Get<Rocket::Core::String>();
      if (tabIndex == atoi(siblingTabIndex.CString()))
      {
        return parentNode->GetChild(i);
      }
    }
  }
  return NULL;
}

void MickRocketElementUtil::appendTextToTextNode(Rocket::Core::Element* element, std::string toAppend)
{
  Rocket::Core::ElementText* textNode = dynamic_cast<Rocket::Core::ElementText*>(element->GetFirstChild());
  Rocket::Core::String utf8_string;
  textNode->GetText().ToUTF8(utf8_string);
  utf8_string.Append(Rocket::Core::String(toAppend.c_str()));
  textNode->SetText(utf8_string);
}

void MickRocketElementUtil::replaceEndStringInTextNode(Rocket::Core::Element* element, std::string toReplace)
{
  Rocket::Core::ElementText* textNode = dynamic_cast<Rocket::Core::ElementText*>(element->GetFirstChild());
  Rocket::Core::String utf8_string;
  textNode->GetText().ToUTF8(utf8_string);
  utf8_string.Resize(utf8_string.Find(":") +2); // replace one space after the colon
  utf8_string.Append(Rocket::Core::String(toReplace.c_str()));
  textNode->SetText(utf8_string);
}


MickRocketElementUtil::~MickRocketElementUtil()
{
  // TODO Auto-generated destructor stub
}

