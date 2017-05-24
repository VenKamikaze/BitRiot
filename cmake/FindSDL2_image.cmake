# Locate the SDL2_image library. This CMake module is a modified version
# of the original FindSDL_image.cmake file
# ###########################################################################
# Locate SDL_image library
# This module defines
# SDL2IMAGE_LIBRARY, the name of the library to link against
# SDLIMAGE_FOUND, if false, do not try to link to SDL
# SDL2IMAGE_INCLUDE_DIR, where to find SDL/SDL.h
#
# $SDLDIR is an environment variable that would
# correspond to the ./configure --prefix=$SDLDIR
# used in building SDL.
#
# Created by Eric Wing. This was influenced by the FindSDL.cmake 
# module, but with modifications to recognize OS X frameworks and 
# additional Unix paths (FreeBSD, etc).

#=============================================================================
# Copyright 2005-2009 Kitware, Inc.
#
#Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#
#1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
#
#2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
#
#3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#=============================================================================
# (To distributed this file outside of CMake, substitute the full
#  License text for the above reference.)

FIND_PATH(SDL2IMAGE_INCLUDE_DIR SDL_image.h
  HINTS
  $ENV{SDL2IMAGEDIR}
  $ENV{SDL2DIR}
  PATH_SUFFIXES include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include/SDL2
  /usr/include/SDL2
  /sw/include/SDL2 # Fink
  /opt/local/include/SDL2 # DarwinPorts
  /opt/csw/include/SDL2 # Blastwave
  /opt/include/SDL2
)

FIND_LIBRARY(SDL2IMAGE_LIBRARY 
  NAMES SDL2_image
  HINTS
  $ENV{SDL2IMAGEDIR}
  $ENV{SDL2DIR}
  PATH_SUFFIXES lib64 lib
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

SET(SDL2IMAGE_FOUND "NO")
IF(SDL2IMAGE_LIBRARY AND SDL2IMAGE_INCLUDE_DIR)
  SET(SDL2IMAGE_FOUND "YES")
ENDIF(SDL2IMAGE_LIBRARY AND SDL2IMAGE_INCLUDE_DIR)

