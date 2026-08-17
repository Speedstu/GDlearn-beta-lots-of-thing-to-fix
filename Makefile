# gdlearn2 - simple, dependency-free build.
#   make            release build -> build/gdlearn
#   make debug      -O0 -g -fsanitize=address,undefined
#   make test       build + run the selftest
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -march=native -ffast-math -fno-finite-math-only \
            -Wall -Wextra -Wno-unused-parameter -Isrc
LDFLAGS  ?= -pthread

SRC  := $(shell find src -name '*.cpp')
OBJ  := $(patsubst src/%.cpp,build/obj/%.o,$(SRC))
BIN  := build/gdlearn

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)
	@echo "built $@"

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS := -std=c++17 -O0 -g -fsanitize=address,undefined -Wall -Wextra -Isrc
debug: LDFLAGS := -pthread -fsanitize=address,undefined
debug: clean $(BIN)

test: $(BIN)
	./$(BIN) selftest

clean:
	rm -rf build

.PHONY: all clean test debug
