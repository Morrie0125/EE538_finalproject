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

all: generator placement roundtrip_test

generator: src/generator.cpp
	$(CXX) $(CXXFLAGS) src/generator.cpp -o generator$(EXE_EXT)

placement: src/placement.cpp src/parser.cpp src/writer.cpp
	$(CXX) $(CXXFLAGS) src/placement.cpp src/parser.cpp src/writer.cpp -o placement$(EXE_EXT)

roundtrip_test: src/test_roundtrip.cpp src/parser.cpp src/writer.cpp
	$(CXX) $(CXXFLAGS) src/test_roundtrip.cpp src/parser.cpp src/writer.cpp -o roundtrip_test$(EXE_EXT)

clean:
	-$(RM) generator$(EXE_EXT) placement$(EXE_EXT) roundtrip_test$(EXE_EXT)