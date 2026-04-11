CXX = g++
CXXFLAGS = -std=c++17 -O2 -Iinclude

ifeq ($(OS),Windows_NT)
EXE_EXT = .exe
RM = del /Q /F
else
EXE_EXT =
RM = rm -f
endif

.PHONY: all clean

all: main

main: src/main.cpp src/generator.cpp src/placement.cpp src/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp src/generator.cpp src/placement.cpp src/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp -o main$(EXE_EXT)

clean:
	-$(RM) main$(EXE_EXT)