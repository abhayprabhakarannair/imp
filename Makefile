CXX ?= g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Isrc
BUILD := build/host

test: $(BUILD)/tests
	$(BUILD)/tests

$(BUILD)/tests: $(wildcard test/*.cpp) $(wildcard src/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(wildcard test/*.cpp) -o $@

clean:
	rm -rf build

.PHONY: test clean
