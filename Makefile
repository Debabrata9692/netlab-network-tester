.RECIPEPREFIX = >
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread -MMD -MP
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)
TARGET := chaos

all: $(TARGET)

$(TARGET): $(OBJ)
> $(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: src/%.cpp | build
> $(CXX) $(CXXFLAGS) -c $< -o $@

build:
> mkdir -p build

clean:
> rm -rf build $(TARGET)

-include $(OBJ:.o=.d)
.PHONY: all clean
