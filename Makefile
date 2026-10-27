CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -IAudIO/CustomEngines

.PHONY: all test clean

all: test

test: tests/test_firmware_core.cpp AudIO/CustomEngines/kick_custom_firmware_core.hpp
	$(CXX) $(CXXFLAGS) tests/test_firmware_core.cpp -o build_test_runner
	./build_test_runner

clean:
	rm -f build_test_runner

