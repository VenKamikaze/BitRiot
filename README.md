# BitRiot
Clone / re-implementation of the Amiga BugBomber game

![Screenshot](https://raw.github.com/VenKamikaze/BitRiot/master/doco/screenshots/BitRiot-1.png)

This game is a re-implementation of 'BugBomber' - a great Amiga game that is similar to Bomberman but with a twist of being able to spawn your own helper robots. I found the code for BitRiot when I was searching to see if anyone recreated bomberman. It was built by one guy as a university project and posted to a forum.

It was original built against the win32 API, and I figured I'd like to learn the difficulties in porting a game from win32 to something Linux native. I wasn't quite ready to jump into OpenGL / 3D APIs, so I took the approach of porting to SDL 1.2.x with normal software blitting.

I contacted the original author and they were happy for me to work on this code. When I asked about licencing they said they would have licensed it as something free "like Apache", hence the Apache Software License now added to the source code!

*Thanks to contributors, the game now supports SDL2 and game controllers!*

There are also older versions of the game code working with SDL 1.2.x, and an even older version that was originally written against win32 + directdraw. Note that tags before sdl2 are not maintained and do not have the fixes the SDL2 version contains.

## Tags

Here's an example of the tagged releases:
* win32-v1
* sdl-v1.1
* sdl2-v1.2
* sdl2-v1.3

This indicates both the framework used underneath (win32, sdl, sdl2), plus the version of the game code, e.g. v1.1, v1.2. It means that the sdl2-v1.3 tag contains a lot of fixes and changes that are not within the sdl-v1.1 tag.

The master branch typically has the latest stable sdl2 code. It may or may not advance further as I don't have as much time to spend on this as I'd like. That being said, I'm very open to any contributions. Contributions to this code base are what enabled SDL2 support in the first place.

The newest SDL2 version (v1.3) now supports a larger map option on startup (-b), fullscreen (-f), game controllers (plus rumble support) and hot joining (take over from CPU player).

![Larger map screenshot](https://raw.github.com/VenKamikaze/BitRiot/master/doco/screenshots/BitRiot-2.png)


## Branches

The sdl2 branch should always have a reasonably stable SDL2 release of the game, with ongoing sdl2 development limited to the 'sdl2_ongoing' branch.

Things that I'd like to do, but likely won't ever find time for (in order of personal interest). Note if the top two items are completed I'll remove the 'Beta' tag from the game title:

* Create a basic menu selection screen on startup to configure game parameters
* Allow players to restart the game
* ~~Fix up the input code~~ (done!)
* ~~Port this to SDL2~~ (done! work may or may not continue to switch a lot of surfaces into textures)
* Cleanups (~~code formatting and style~~ (done), also some collections always grow e.g. in the input code)
* Scaling / resolution options (Partly done - fullscreen option '-f' available).
* Write up some notes to explain the porting process to SDL2, as a quick 'n' dirty tutorial for others thinking of doing this for other games.
* ~~Controller input~~ (done!)
* Network play (honestly, unless someone else did this I highly doubt I'd get around to even attempting)

If you'd ever like to work on this code, please fork it and file pull requests - I'd be glad to accept them.

Please note: If you'd found the original BitRiot release anywhere, you might notice the artwork in this one was different. This was only done out of safety incase there were any concerns over the artwork included in the original release.

# Build

To build the game, first make sure you have the necessary dependencies installed on your system. The main master branch is now using SDL2. Assuming you are compiling this branch, you will need:
CMake 3.4.3, SDL 2.0.x, SDL2_mixer and SDL2_ttf.

on ubuntu/debian:
```
sudo apt-get install cmake libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev
```

## CMake

```
git clone https://github.com/VenKamikaze/BitRiot.git
cd BitRiot/target
cmake ../
make
./BitRiot
```

### CMake with MinGW

```
cd BitRiot
target/cmake.exe -G "MinGW Makefiles" ..
target/mingw32-make
```

Steps shown above:

1. Clone the repository:
2. Change into the target directory inside your cloned BitRiot repository
3. Generate a Makefile with CMake
4. Build it
5. Run the game - you should now have a 'BitRiot' executable in your current directory:

