/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
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

#ifndef RMLUI_SHELL_X11_INPUTX11_H
#define RMLUI_SHELL_X11_INPUTX11_H

#include <X11/Xlib.h>
#include <x11/X11MacroZapper.h>

#include "Input.h"

/**
 * Input Wrapper Code
 *
 * Feel free to take this class and integrate it with your project.
 *
 * @author Lloyd Weehuizen
 */

class InputX11 : public Input
{
public:
	static bool Initialise();
	static void Shutdown();

	/// Process the windows message
	static void ProcessXEvent(Display* display, const XEvent& event);

	// Initialises Xkb extension if available or reads keymap from X11
	// server otherwise.  This is internal to the X11 subsystem and
	// has nothing to do with RmlUi's mapping.
	static void InitialiseX11Keymap(Display *display);
};

#endif
