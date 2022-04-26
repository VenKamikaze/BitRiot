#!/bin/bash
#
if [[ $(basename $(pwd)) == "target" ]]; then
  echo Cleaning up...
  rm -rf BitRiot cmake_install.cmake Makefile CMakeCache.txt CMakeFiles
  cmake ../
  make
else
  echo "Run this from the target directory"
fi

