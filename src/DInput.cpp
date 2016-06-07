
#include "DInput.h"

LPDIRECTINPUT8 DInput::lpdi = NULL;
LPDIRECTINPUTDEVICE8 DInput::lpdikey = NULL;
LPDIRECTINPUTDEVICE8 DInput::lpdimouse = NULL;

unsigned char DInput::keystate[] = {};
DIMOUSESTATE DInput::mouseState = {};

void DInput::initDInput(HINSTANCE hInstance, HWND mainWindowHandle)
{
	lpdi = NULL;
	lpdikey = NULL;
	lpdimouse = NULL;

	if (FAILED(DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void **)&lpdi, NULL)))
	{
		// failed to set direct input 8 COM
		MessageBox(mainWindowHandle, 
			"Failed to initialise Direct Input 8 Interface!", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdi->CreateDevice(GUID_SysKeyboard, &lpdikey, NULL)))
	{
		// failed to create system keyboard
		MessageBox(mainWindowHandle, 
			"Direct Input failed to create the system keyboard interface, do you have a keyboard attached?", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdikey->SetCooperativeLevel(mainWindowHandle, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
	{
		// failed to set keyboard co operative level
		MessageBox(mainWindowHandle, 
			"Failed to set keyboard cooperative level, another application may be using it exclusively.", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdikey->SetDataFormat(&c_dfDIKeyboard)))
	{
		// failed to set keyboard data format
		MessageBox(mainWindowHandle, 
			"Failed to set keyboard data format!", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdikey->Acquire()))
	{
		// failed to acquire system keyboard
		MessageBox(mainWindowHandle, 
			"Direct Input failed to acquire the system keyboard, do you have a keyboard attached?", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	// same for mouse

	if (FAILED(lpdi->CreateDevice(GUID_SysMouse, &lpdimouse, NULL)))
	{
		// failed to create system mouse
		MessageBox(mainWindowHandle, 
			"Direct Input failed to create the system mouse interface, do you have a mouse attached?", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdimouse->SetCooperativeLevel(mainWindowHandle, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
	{
		// failed to set mouse co operative level
		MessageBox(mainWindowHandle, 
			"Failed to set mouse cooperative level, another application may be using it exclusively.", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdimouse->SetDataFormat(&c_dfDIMouse)))
	{
		// failed to set mouse data format
		MessageBox(mainWindowHandle, 
			"Failed to set mouse data format!", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}

	if (FAILED(lpdimouse->Acquire()))
	{
		// failed to acquire system mouse
		MessageBox(mainWindowHandle, 
			"Direct Input failed to acquire the system mouse, do you have a mouse attached?", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(mainWindowHandle,WM_CLOSE,0,0);
		return;
	}
	

    
}

void DInput::shutdownDInput()
{

	if (lpdimouse)
		lpdimouse->Unacquire();
	if (lpdimouse)
		lpdimouse->Release();

	if (lpdikey)
		lpdikey->Unacquire();
	if (lpdikey)
		lpdikey->Release();

	if (lpdi)
		lpdi->Release();
}

bool DInput::updateKeyboardState()
{
	if (FAILED(lpdikey->GetDeviceState(sizeof(keystate), (LPVOID)keystate)))
        return false;
	else
		return true;
}

bool DInput::keyDown(unsigned char DIKey)
{
	return ((keystate[DIKey] & 0x80) > 0) ? true : false;
}

bool DInput::updateMouseState()
{
	if (FAILED(lpdimouse->GetDeviceState(sizeof(mouseState), (LPVOID)&mouseState)))
		return false;
	else
		return true;
}

