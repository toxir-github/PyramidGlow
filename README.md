# Pyramid Glow (OpenFX, CPU)

Layout: put this folder next to a checkout of the OpenFX SDK:
    git clone https://github.com/AcademySoftwareFoundation/openfx.git
    (this folder = ./PyramidGlow, SDK = ./openfx)

Build (Windows, Visual Studio 2022):
    cmake -S PyramidGlow -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
Output: build/PyramidGlow.ofx.bundle  (Win64 folder inside)
Install: copy the whole PyramidGlow.ofx.bundle folder to
    C:\Program Files\Common Files\OFX\Plugins\
then restart Resolve. Effect shows under OpenFX > Glow Lab > Pyramid Glow.

macOS: cmake -S PyramidGlow -B build && cmake --build build
    The bundle needs an Info.plist and code-signing; see OpenFX docs.

Files:
  glow_core.h   the algorithm (no OpenFX dependency, easy to read/modify)
  PyramidGlow.cpp   OpenFX wrapper: params + render
  test_core.cpp     run the algorithm on a raw float RGBA file
