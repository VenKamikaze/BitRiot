#!/bin/bash
#
[[ ! -x $(which astyle) ]] && echo "Could not find astyle. Aborting..." && exit 1

for x in *.cpp RmlUI/glue/*.cpp; do
  astyle --mode=c --lineend=linux --add-brackets --convert-tabs --indent-switches --indent-cases --indent=spaces=2 --style=allman --indent-classes "$x"
done

for x in *.h RmlUI/glue/*.h; do
  astyle --mode=c --lineend=linux --add-brackets --convert-tabs --indent-switches --indent-cases --indent=spaces=2 --style=allman --indent-classes "$x"
done
