# The Legend of Tilda
#
#   make            build the game and the world tool
#   make run        build and start the game
#   make world      regenerate assets/world.bin (overwrites your edits!)
#   make dump       print assets/world.bin as an ASCII map and check it
#
# Needs a C++17 compiler and the SDL2 development files (sdl2-config in your PATH).
# Debian/Ubuntu: sudo apt install build-essential libsdl2-dev
# macOS:         brew install sdl2
# Windows:       use MSYS2 (pacman -S mingw-w64-x86_64-SDL2) or the CMakeLists.txt

CXX        ?= g++
SDL2_CONFIG ?= sdl2-config
CXXFLAGS   ?= -O2 -std=c++17 -Wall -Wextra
SDL_CFLAGS = $(shell $(SDL2_CONFIG) --cflags)
SDL_LIBS   = $(shell $(SDL2_CONFIG) --libs)

all: tilda

tilda: src/main.cpp src/font.h src/world.h src/sprites.h src/sprites.h src/story.h src/stb_image.h src/modplay.h
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ src/main.cpp $(SDL_LIBS)

run: tilda
	./tilda

clean:
	rm -f tilda mkworld modtest

.PHONY: all run world dump clean
