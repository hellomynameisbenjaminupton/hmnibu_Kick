CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -IAudIO/CustomEngines

.PHONY: all test test_phase1 test_phase2 clean

all: test

test: test_phase1 test_phase2

test_phase1: tests/test_firmware_core.cpp AudIO/CustomEngines/kick_custom_firmware_core.hpp
	@echo "------------------------------------------------------------"
	@echo "Building & running Phase 1 Firmware Core Test Suite..."
	@echo "------------------------------------------------------------"
	$(CXX) $(CXXFLAGS) tests/test_firmware_core.cpp -o build_test_phase1
	./build_test_phase1

test_phase2: tests/test_phase2_chopper.cpp AudIO/CustomEngines/kick_sample_chopper.hpp AudIO/CustomEngines/kick_custom_firmware_core.hpp
	@echo "------------------------------------------------------------"
	@echo "Building & running Phase 2 Sample Chopper & WSOLA Test Suite..."
	@echo "------------------------------------------------------------"
	$(CXX) $(CXXFLAGS) tests/test_phase2_chopper.cpp -o build_test_phase2
	./build_test_phase2

clean:
	rm -f build_test_phase1 build_test_phase2 build_test_runner
