/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// Note: This class no longer resembles the sample file from libRocket at all, sans the ProcessEvent method definition.
//       I've kept the MIT license as part of this anyway.

#include "EventHandlerPlayerSelect.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <string>
#include "EventManager.h"
#include "GameSettings.h"
#include "MickLogger.h"
#include "RmlUI/MickRmlUIElementUtil.h"


EventHandlerPlayerSelect::EventHandlerPlayerSelect()
{
}

EventHandlerPlayerSelect::~EventHandlerPlayerSelect()
{
}


void EventHandlerPlayerSelect::playerSelectPageInit(Rml::ElementDocument* bodyElement)
{
  if (! bodyElement)
    return;
}

void EventHandlerPlayerSelect::ProcessEvent(Rml::Event& event, const Rml::String& value)
{

}
