CXX = g++
CXXFLAGS = -std=c++17 -O2 -Iinclude

ifeq ($(OS),Windows_NT)
EXE_EXT = .exe
RM = del /Q /F
READLINE_LIBS = -lreadline -lhistory -lncursesw
else
EXE_EXT =
RM = rm -f
READLINE_LIBS = -lreadline
endif

.PHONY: all clean

all: main hpwl_test

main: src/main.cpp src/cli_repl.cpp src/generator.cpp src/placement.cpp src/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp src/hpwl.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp src/cli_repl.cpp src/generator.cpp src/placement.cpp src/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp src/hpwl.cpp $(READLINE_LIBS) -o main$(EXE_EXT)

# HPWL test executable
hpwl_test: src/hpwl.cpp src/tests_hpwl.cpp
	$(CXX) $(CXXFLAGS) src/hpwl.cpp src/tests_hpwl.cpp -o hpwl_test$(EXE_EXT)

# Clean build artifacts
clean:
	-$(RM) main$(EXE_EXT) hpwl_test$(EXE_EXT)