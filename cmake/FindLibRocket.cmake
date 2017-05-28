FIND_PATH(LIBROCKET_HEADERS Rocket/Core.h  # find Rocket/Core.h and set it in LIBROCKET_HEADERS
  HINTS
  PATH_SUFFIXES Include  # postfix the path 'Include' when searching the paths below for 'Rocket/Core.h'
  PATHS
  $ENV{LIBROCKET}  # Use environment variable LIBROCKET and see if $LIBROCKET/include/Rocket/Core.h exists
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

FIND_LIBRARY(LIBROCKET_CONTROLS RocketControls
  HINTS
  PATH_SUFFIXES lib lib64 libs/Win32 libs/Win64
  PATHS
  $ENV{LIBROCKET}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

FIND_LIBRARY(LIBROCKET_CORE RocketCore
  HINTS
  PATH_SUFFIXES lib lib64 libs/Win32 libs/Win64
  PATHS
  $ENV{LIBROCKET}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

FIND_LIBRARY(LIBROCKET_DEBUGGER RocketDebugger
  HINTS
  PATH_SUFFIXES lib lib64 libs/Win32 libs/Win64
  PATHS
  $ENV{LIBROCKET}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

SET(LIBROCKET_FOUND "NO")
IF(LIBROCKET_CONTROLS AND LIBROCKET_CORE AND LIBROCKET_DEBUGGER AND LIBROCKET_HEADERS)
  SET(LIBROCKET_FOUND "YES")
  SET(LIBROCKET_LIBRARIES ${LIBROCKET_CONTROLS} ${LIBROCKET_CORE} ${LIBROCKET_DEBUGGER})
ENDIF()
