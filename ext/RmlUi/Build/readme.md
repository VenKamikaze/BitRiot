# Quick build instructions #

```
curl -o RmlUi-4.3.tar.gz https://github.com/mikke89/RmlUi/archive/refs/tags/4.3.tar.gz 
tar -xvf RmlUi-4.3.tar.gz
cd RmlUi*
mkdir Build
cd Build
ccmake ..
# press c
# LUA off, SHARED_LIBRARY off, SAMPLES off
# press c
# press g
make -j8
```
