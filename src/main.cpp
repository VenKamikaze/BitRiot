// WinMain cpp file

// INCLUDES ///////////////////////////////////////////////


/// UNIX INCLUDES
#if defined (__GNUC__) && defined(__unix__)


/// WIN32 INCLUDES
#elif defined (WIN32)

#define INITGUID       // make sure all the COM interfaces are available
                       // instead of this you can include the .LIB file
                       // DXGUID.LIB

#define WIN32_LEAN_AND_MEAN

#include <windows.h>   // include important windows stuff
#include <windowsx.h>
#include <mmsystem.h>

//#include <conio.h>
//#include <io.h>
#endif

//#define INITGUID       // make sure all the COM interfaces are available
                         // instead of this you can include the .LIB file
                         // DXGUID.LIB

//#define WIN32_LEAN_AND_MEAN

//#include <windows.h>   // include important windows stuff
//#include <windowsx.h>
//#include <mmsystem.h>
#include <iostream> // include important C/C++ stuff
//#include <conio.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h> 
#include <math.h>
//#include <io.h>
#include <fcntl.h>

#include "SDL/SDL.h"
#include "SDL/SDL_config.h"
#include "SDL/SDL_video.h"

#include "MickSDLSound.h"

// some more win32 defines
//#if defined (WIN32)
//#include <ddraw.h>  // directX includes
//#endif

// game object includes
#include "GameEngine.h"

#ifndef NULL
#define NULL 0
#endif

// MACROS /////////////////////////////////////////////////

#define KEYDOWN(vk_code) ((GetAsyncKeyState(vk_code) & 0x8000) ? 1 : 0)
#define KEYUP(vk_code)   ((GetAsyncKeyState(vk_code) & 0x8000) ? 0 : 1)

// DEFINES ////////////////////////////////////////////////

// defines for windows interface
#define WINDOW_CLASS_NAME "Bit Riot"  // class name
#define WINDOW_TITLE      "Bit Riot Final Beta Version - SDL Port NO2"
const int WINDOW_WIDTH = (Map::MAP_WIDTH * Map::TILE_WIDTH) + InfoPanel::WIDTH;   // size of window
const int WINDOW_HEIGHT = Map::MAP_HEIGHT * Map::TILE_HEIGHT;

#define WINDOW_BPP        32    // bitdepth of window (8,16,24 etc.)
                                // note: if windowed and not
                                // fullscreen then bitdepth must
                                // be same as system bitdepth
                                // also if 8-bit the a pallete
                                // is created and attached

#define WINDOWED_APP      1     // 0 not windowed, 1 windowed

//const unsigned int COLORKEY_LOW = 0; // low and high values for source
//const unsigned int COLORKEY_HIGH = 0; // color keying (transparency range)

const Uint8 COLORKEY_R = 0;
const Uint8 COLORKEY_G = 0;
const Uint8 COLORKEY_B = 0;
const Uint8 COLORKEY_A = 0;

// PROTOTYPES /////////////////////////////////////////////

// game console
//int consoleInit();
//int consoleShutdown();
int consoleMain();

// GLOBALS ////////////////////////////////////////////////

// M2S: replace HWND with ??

//HWND main_window_handle           = NULL; // save the window handle
//HINSTANCE main_instance           = NULL; // save the instance


// Direct Draw globals
//LPDIRECTDRAW7 lpdd = NULL; // Direct Draw 7 pointer

// M2S: replace LPDIRECTDRAWSURFACE7 with SDL_Surface ??

SDL_Surface* sdl_primary = NULL;

//LPDIRECTDRAWSURFACE7 lpddsprimary = NULL; // pointer to primary surface
//LPDIRECTDRAWSURFACE7 lpddsback = NULL; // pointer to backbuffer surface

// M2S: manual blit clear ??

//DDBLTFX ddbltfxClear; // blit fx for clearing back buffer

// M2S: RECT with SDL_Rect ??

SDL_Rect backRect, windowRect, clientArea; // RECTs for blitting pseudo-backbuffer to primary
//RECT backRect, windowRect, clientArea; // RECTs for blitting pseudo-backbuffer to primary
//LPDIRECTDRAWCLIPPER lpddclipper = NULL;
//DDSURFACEDESC2 ddsd; // DD surface description for primary surface

// game object globals
GameEngine * engine = NULL;



// FUNCTIONS //////////////////////////////////////////////
/*
LRESULT CALLBACK WindowProc(HWND hwnd, 
						    UINT msg, 
                            WPARAM wparam, 
                            LPARAM lparam)
{
	// this is the main message handler of the system
	PAINTSTRUCT	ps;		   // used in WM_PAINT
	HDC			hdc;	   // handle to a device context

	// what is the message 
	switch(msg)
		{	
		case WM_CREATE: 
			{
			// do initialization stuff here
			return(0);
			} break;

		case WM_PAINT:
			{
			// start painting
			hdc = BeginPaint(hwnd,&ps);

			// end painting
			EndPaint(hwnd,&ps);
			return(0);
			} break;

		case WM_MOVE:
			{
				int clientX, clientY;
				// get new window co-ords
				clientX = LOWORD(lparam);
				clientY = HIWORD(lparam);

				// update blitting RECT
				clientArea.x = clientX;
				clientArea.y = clientY;
				clientArea.w = clientX + WINDOW_WIDTH -1 ;
				clientArea.h = clientY + WINDOW_HEIGHT - 1;
				return 0;
			} break;


		case WM_DESTROY: 
			{
			// kill the application			
			PostQuitMessage(0);
			return(0);
			} break;

		default:
			break;

		} // end switch

	// process any messages that we didn't take care of 
	return (DefWindowProc(hwnd, msg, wparam, lparam));

} // end WinProc

// WINMAIN ////////////////////////////////////////////////
/*
int WINAPI WinMain(	HINSTANCE hinstance,
					HINSTANCE hprevinstance,
					LPSTR lpcmdline,
					int ncmdshow)
{
	// this is the winmain function
	WNDCLASSEX winclass; // this will hold the class we create
	HWND	   hwnd;	 // generic window handle
	MSG		   msg;		 // generic message
	HDC        hdc = NULL;      // graphics device context

	// first fill in the window class stucture
	winclass.cbSize         = sizeof(WNDCLASSEX);
	winclass.style			= CS_DBLCLKS | CS_OWNDC | 
							CS_HREDRAW | CS_VREDRAW;
	winclass.lpfnWndProc	= WindowProc;
	winclass.cbClsExtra		= 0;
	winclass.cbWndExtra		= 0;
	winclass.hInstance		= hinstance;
	winclass.hIcon			= LoadIcon(NULL, IDI_APPLICATION);
	winclass.hCursor		= LoadCursor(NULL, IDC_ARROW); 
	winclass.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
	winclass.lpszMenuName	= NULL;
	winclass.lpszClassName	= WINDOW_CLASS_NAME;
	winclass.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);

	// register the window class
	if (!RegisterClassEx(&winclass))
		return(0);

	// create the window
	if (!(hwnd = CreateWindowEx(NULL,              // extended style
								WINDOW_CLASS_NAME, // class
								WINDOW_TITLE, // title
								(WINDOWED_APP ? (WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION) : (WS_POPUP | WS_VISIBLE)), 
					 			0,0,	  // initial x,y
								WINDOW_WIDTH,WINDOW_HEIGHT,  // initial width, height
								NULL,	  // handle to parent 
								NULL,	  // handle to menu
								hinstance,// instance of this application
								NULL)))   // extra creation parms
	return(0);

	// save the window handle and instance in a global
	main_window_handle = hwnd;
	main_instance      = hinstance;

	// resize the window so that client is really width x height
	if (WINDOWED_APP)
	{
		// now resize the window, so the client area is the actual size requested
		// since there may be borders and controls if this is going to be a windowed app
		// if the app is not windowed then it won't matter
		windowRect.top = 0;
		windowRect.left = 0;
		windowRect.right = WINDOW_WIDTH-1;
		windowRect.bottom = WINDOW_HEIGHT-1;


		// make the call to adjust window_rect
		AdjustWindowRectEx(&windowRect,
		GetWindowStyle(main_window_handle),
		GetMenu(main_window_handle) != NULL,
		GetWindowExStyle(main_window_handle));

		// now resize the window with a call to MoveWindow()
		MoveWindow(main_window_handle,
				((GetSystemMetrics(SM_CXSCREEN) - WINDOW_WIDTH)/2), // x position
				((GetSystemMetrics(SM_CYSCREEN) - WINDOW_HEIGHT)/2), // y position
				windowRect.right - windowRect.left, // width
				windowRect.bottom - windowRect.top, // height
				FALSE);

		// show the window, so there's no garbage on first render
		ShowWindow(main_window_handle, SW_SHOW);
	} // end if windowed

	// perform all console specific initialization
	consoleInit();

	// enter main event loop
	while(1)
		{
		if (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
		{ 
			// test if this is a quit
			if (msg.message == WM_QUIT)
			break;
		
			// translate any accelerator keys
			TranslateMessage(&msg);

			// send the message to the window proc
			DispatchMessage(&msg);
		} // end if
	    
		// main game processing goes here
		consoleMain();
		
		// check if user is trying to exit
		if (KEYDOWN(VK_ESCAPE))
		{
			PostMessage(main_window_handle, WM_DESTROY,0,0);
		}

		} // end while

	// shutdown game and release all resources
	consoleShutdown();


	// return to Windows like this
	return(int)(msg.wParam);

} // end WinMain
*/



int consoleInit()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
    	cerr << "Failed to SDL_Init video!";
    	return 1;
    }


    if (!(sdl_primary = SDL_SetVideoMode(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_BPP, SDL_DOUBLEBUF|SDL_HWSURFACE)))
    {
    	cerr << "Failed to create sdl_primary!";
        return 1;
    }

    if (TTF_Init() == -1)
    {
    	cerr << "Failed in TTF_Init!!";
        return 1;
    }

    try
    {
    	MickSDLSound::getInstance()->initAudio();
    }
    catch(exception &e)
    {
    	cerr << "Could not init audio!";
    	return 1;
    }


    // this function is where you do all the initialization
	// for your game

	//ddsd.dwFlags = DDSD_CAPS; // enable valid field(s)

//	ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

	// check pixel format is ok
/*	DDPIXELFORMAT pixelFormat;
	memset(&pixelFormat,0,sizeof(pixelFormat));
	pixelFormat.dwSize = sizeof(pixelFormat);	

	lpddsprimary->GetPixelFormat(&pixelFormat);

	if (pixelFormat.dwRGBBitCount != WINDOW_BPP) 
	{
		MessageBox(main_window_handle, 
			"This application requires 32 bit colour depth, please change your desktop settings.", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(main_window_handle,WM_CLOSE,0,0);
		return(0);
	}
*/
	// add clipper to primary surface
/*
	lpdd->CreateClipper(NULL, &lpddclipper, NULL);
	lpddclipper->SetHWnd(NULL, main_window_handle);
	lpddsprimary->SetClipper(lpddclipper);
	lpddclipper->Release(); // due to 2 count problem

	*/
	// update blitting RECT
	clientArea.x = 0;
	clientArea.y = 0;
	clientArea.w = WINDOW_WIDTH -1 ;
	clientArea.h = WINDOW_HEIGHT - 1;

	SDL_SetClipRect(sdl_primary, &clientArea);
	/*

	// due to windowed mode, backbuffer is a normal offscreen surface
	ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS; // enable valid field(s)
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
	ddsd.dwWidth = WINDOW_WIDTH;
	ddsd.dwHeight = WINDOW_HEIGHT;
	if (FAILED(lpdd->CreateSurface(&ddsd, &lpddsback, NULL)))
	{
		// failed to create backbuffer surface
		MessageBox(main_window_handle, 
			"Failed to set backbuffer surface, check that your screen display meets the system requirements.", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(main_window_handle,WM_CLOSE,0,0);
		return(0);
	}
	// set colour keying here */
	//DDCOLORKEY colorKey;
	//colorKey.dwColorSpaceLowValue = COLORKEY_LOW;
	//colorKey.dwColorSpaceHighValue = COLORKEY_HIGH;
	//lpddsback->SetColorKey(DDCKEY_SRCBLT, &colorKey);

	//SDL_SetColorKey(sdl_back, SDL_SRCCOLORKEY, SDL_MapRGB(sdl_back->format, COLORKEY_R, COLORKEY_G, COLORKEY_B));

	// init blit fx for clearing buffer
	//memset(&ddbltfxClear,0,sizeof(ddbltfxClear));
	//ddbltfxClear.dwSize = sizeof(ddbltfxClear);
	//ddbltfxClear.dwFillColor = (255 << 16) + (100 << 8); // color for filling in back buffer // YELLOW??

	// initialize directinput
	//DInput::initDInput(main_instance, main_window_handle);

	// initialize directsound and directmusic
	//DSound_Init();
	//DSound * initDSound = //M2S SOUND DSound::getInstance();
	//initDSound->initialise(main_window_handle);
	//initDSound->setMuted(true);

	// set window handle to data reader
	//unused
	//DataReader * reader = DataReader::getInstance();
	//reader->setWindowHandle(main_window_handle);

	// seed random number generator
	srand(SDL_GetTicks());

	// all other initialization code goes here...

	Map * staticMap = Map::getInstance();
	staticMap->init();  // M2S does sdl_primary belong here? was: lpdd

	EntityRendererFactory * erf = EntityRendererFactory::getInstance();
	erf->initSurfaces(sdl_primary);   // M2S does sdl_primary belong here? was: lpdd

	engine = new GameEngine(sdl_primary);   // M2S does sdl_primary belong here?

	return 0;

} // end Game_Init

///////////////////////////////////////////////////////////
/*
int consoleShutdown()
{
	// this function is where you shutdown your game and
	// release all resources that you allocated

	// shut everything down


	DInput::shutdownDInput();

	if (lpddclipper)
		lpddclipper->Release();
	if (lpddsback)
		lpddsback->Release();
	if (lpddsprimary)
		lpddsprimary->Release();
	if (lpdd)
		lpdd->Release();


	return 0;
} // end Game_Shutdown

//////////////////////////////////////////////////////////
*/
int consoleMain()
{
	SDL_FillRect(sdl_primary, NULL, SDL_MapRGB(sdl_primary->format, 200, 200, 100)); // as per ddbltfxClear

	// start the timing clock
	//Uint32 startTime = GetTickCount();
	Uint32 startTime = SDL_GetTicks();

	// clear the drawing surface 
	//lpddsback->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfxClear);
	//SDL_FillRect(sdl_back, NULL, SDL_MapRGB(sdl_back->format, 255, 255, 100)); // as per ddbltfxClear

	//sdl_back->BlitSurface(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfxClear);

	//SDL_BlitSurface(m_iconSurface, &m_iconRect, surface, &m_screenRect);

	engine->runEngine();

	// blit from back buffer to primary
	//lpddsprimary->Blt(&clientArea, lpddsback, &backRect, DDBLT_WAIT, NULL);
	//SDL_BlitSurface(m_iconSurface, &m_iconRect, surface, &m_screenRect);

	//SDL_BlitSurface(sdl_back, &backRect, sdl_primary, &clientArea);

	//while ((GetTickCount() - startTime) < 33)
	while ((SDL_GetTicks() - startTime) < 33)
	{
		//wait a bit to sync to 30fps
		//Sleep(1);
		SDL_Delay(1);
	}

	return 0;
 
} // end Game_Main



int main(int argc, char* argv[])
{
    int quitkey = 0;

    // Init sub-systems
    try
    {
    	quitkey = consoleInit();

        //Set the window caption
        SDL_WM_SetCaption( WINDOW_TITLE, NULL );
    }
    catch(exception &e)
    {
    	cerr << e.what();
    	quitkey = -1;
    }

    // Do game loop
    try
    {
		while(!quitkey)
		{

		    //The frame rate regulator
		    //Timer fps;

		    //Start the frame timer
		    //fps.start();

			if(SDL_MUSTLOCK(sdl_primary))
			{
				if(SDL_LockSurface(sdl_primary) < 0)
					break;
			}

			try
			{
				// main game processing goes here
				consoleMain();
			}
			catch(InputException& e)
			{
				if(e.gotQuit())
				{
					quitkey = 1;
				}
				else
				{
					cerr << "Caught strange InputException! ";
					cerr << e.what();
					break;
				}
			}

			if(sdl_primary != NULL)
			{
				SDL_Flip(sdl_primary);
			}
			else
			{
				cerr << "Something major went wrong. Lost primary buffer pointer.";
				break;
			}

			if(SDL_MUSTLOCK(sdl_primary))
				SDL_UnlockSurface(sdl_primary);

		    //Cap the frame rate
		    //if( fps.get_ticks() < 1000 / FRAMES_PER_SECOND )
		    //{
		    //     SDL_Delay( ( 1000 / FRAMES_PER_SECOND ) - fps.get_ticks() );
		    //}
			//SDL_Delay(20);
		}

    }
    catch(exception& e)
    {
    	printf("Caught exception in main game loop.");
    	cerr << "Caught exception! ";
    	cerr << e.what();
    }

    // Un-init section
    try
    {
		if(TTF_WasInit())
		{
			TTF_Quit(); //Quit SDL_ttf
		}

		if(SDL_WasInit(SDL_INIT_EVERYTHING))
		{
			if(sdl_primary != NULL)
			{
				SDL_FreeSurface(sdl_primary);
				sdl_primary = NULL;
			}

			SDL_Quit();
		}
    }
    catch(exception &e)
    {
    	cerr << "Caught exception when de-initializing subsystems!" << endl;
    	cerr << e.what();
    }

	if (engine)
		delete engine;

	delete MickSDLSound::getInstance(); // hmm...

    cout << "Exiting.. " << endl;
    return 0;
}


//////////////////////////////////////////////////////////
