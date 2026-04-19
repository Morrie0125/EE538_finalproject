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

all: main hpwl_test adjacency_test delta_hpwl_test move_engine_test sa_test

main: src/main.cpp src/cli_repl.cpp src/generator.cpp src/demo_config.cpp src/placement.cpp src/sa.cpp src/sa_logger.cpp src/cleanup.cpp test/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp src/hpwl.cpp src/adjacency.cpp src/delta_hpwl.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp src/cli_repl.cpp src/generator.cpp src/demo_config.cpp src/placement.cpp src/sa.cpp src/sa_logger.cpp src/cleanup.cpp test/test_roundtrip.cpp src/visualize.cpp src/parser.cpp src/writer.cpp src/hpwl.cpp src/adjacency.cpp src/delta_hpwl.cpp $(READLINE_LIBS) -o main$(EXE_EXT)

# HPWL test executable
hpwl_test: src/hpwl.cpp test/tests_hpwl.cpp
	$(CXX) $(CXXFLAGS) src/hpwl.cpp test/tests_hpwl.cpp -o hpwl_test$(EXE_EXT)

# Adjacency test executable
adjacency_test: src/adjacency.cpp test/test_adjacency.cpp
	$(CXX) $(CXXFLAGS) src/adjacency.cpp test/test_adjacency.cpp -o adjacency_test$(EXE_EXT)

# Delta HPWL v1
delta_hpwl_test: src/delta_hpwl.cpp test/test_delta_hpwl.cpp src/adjacency.cpp
	$(CXX) $(CXXFLAGS) src/delta_hpwl.cpp test/test_delta_hpwl.cpp src/adjacency.cpp -o delta_hpwl_test$(EXE_EXT)

# Move engine test executable
move_engine_test: test/test_move_engine.cpp src/generator.cpp src/demo_config.cpp
	$(CXX) $(CXXFLAGS) test/test_move_engine.cpp src/generator.cpp src/demo_config.cpp -o move_engine_test$(EXE_EXT)

# SA parameter sweep executable
sa_test: test/sa_test.cpp src/placement.cpp src/generator.cpp src/demo_config.cpp
	$(CXX) $(CXXFLAGS) test/sa_test.cpp src/placement.cpp src/generator.cpp src/demo_config.cpp $(READLINE_LIBS) -o sa_test$(EXE_EXT)

# Clean build artifacts
clean:
	-$(RM) main$(EXE_EXT) hpwl_test$(EXE_EXT) adjacency_test$(EXE_EXT) delta_hpwl_test$(EXE_EXT) move_engine_test$(EXE_EXT) sa_test$(EXE_EXT)