FIND_PATH(RMLUI_HEADERS RmlUi/Core.h  # find RmlUi/Core.h and set it in RMLUI_HEADERS
  HINTS
  PATH_SUFFIXES Include  # postfix the path 'Include' when searching the paths below for 'RmlUi/Core.h'
  PATHS
  $ENV{RMLUI}  # Use environment variable RMLUI and see if $RMLUI/include/RmlUi/Core.h exists
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

FIND_LIBRARY(RMLUI_CORE RmlCore
  HINTS
  PATH_SUFFIXES lib lib64 libs/Win32 libs/Win64
  PATHS
  $ENV{RMLUI}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

FIND_LIBRARY(RMLUI_DEBUGGER RmlDebugger
  HINTS
  PATH_SUFFIXES lib lib64 libs/Win32 libs/Win64
  PATHS
  $ENV{RMLUI}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

SET(RMLUI_FOUND "NO")
IF(RMLUI_CORE AND RMLUI_DEBUGGER AND RMLUI_HEADERS)
  SET(RMLUI_FOUND "YES")
  SET(RMLUI_LIBRARIES ${RMLUI_CORE} ${RMLUI_DEBUGGER})
ENDIF()
