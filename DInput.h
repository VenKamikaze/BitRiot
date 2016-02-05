// DInput.h
// a wrapper for Direct Input version 8

#ifndef _DINPUTCLASS
#define _DINPUTCLASS

#define INITGUID
#define DIRECTINPUT_VERSION 0x0800

#include <dinput.h>

class DInput
{
public:
	static void initDInput(HINSTANCE hInstance, HWND mainWindowHandle); // initialises direct input objects
	static void shutdownDInput(); // used to release Direct Input objects

	static bool updateKeyboardState(); // refreshes keyboard state data
	static bool keyDown(unsigned char DIKey); // returns true if key is down

	static bool updateMouseState();
	static DIMOUSESTATE * getMouseState() { return &mouseState; }


private:
	static LPDIRECTINPUT8 lpdi; // pointer to main direct input object
	static LPDIRECTINPUTDEVICE8 lpdikey; // pointer to keyboard
	static LPDIRECTINPUTDEVICE8 lpdimouse; // pointer to mouse

	static unsigned char keystate[256];
	static DIMOUSESTATE mouseState;

};

#endif