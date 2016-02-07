# BitRiot
Clone / re-implementation of the Amiga BugBomber game

This game is a re-implementation of 'BugBomber' - a great Amiga game that is similar to Bomberman but with a twist of being able to spawn your own helper robots. I found the code for BitRiot when I was searching to see if anyone recreated bomberman. It was built by one guy as a university project and posted to a forum.

It was original built against the win32 API, and I figured I'd like to learn the difficulties in porting a game from win32 to something Linux native. I wasn't quite ready to jump into OpenGL / 3D APIs, so I took the approach of porting to SDL 1.2.x with normal software blitting.

I contacted the original author and they were happy for me to work on this code. When I asked about licencing they said they would have licensed it as something free "like Apache", hence the Apache Software License now added to the source code!

The game now works in Linux w/SDL 1.2.x, however may have some minor input issues which I haven't bothered to clean up.

There are two software tags:
* win32-v1
* sdl-v1.1

The master branch has the latest sdl-v1.1 code and is unlikely to advance as I lost interest in the project some time ago.

Things that I'd like to do, but likely won't ever find time for (in order of personal interest):

* Cleanups / fix up the input code a bit
* Port this to SDL2 (and I guess OpenGL)
* Write up some notes to explain the porting process to SDL2, as a quick 'n' dirty tutorial for others thinking of doing this for other games.
* Scaling / resolution options.
* Create a basic menu selection screen on startup to configure game parameters
* Handle controller input
* Network play (honestly, unless someone else did this I highly doubt I'd get around to even attempting)

If you'd ever like to work on this code, please fork it and file pull requests - I'd be glad to accept them.

Please note: If you'd found the original BitRiot release anywhere, you might notice the artwork in this one was different. This was only done out of safety (more like paranoia!) incase there were any concerns over the artwork included in the original release.
