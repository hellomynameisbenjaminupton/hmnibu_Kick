CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -IAudIO/CustomEngines -IKick/CustomSequencer

.PHONY: all test test_phase1 test_phase2 test_phase3 test_phase4 clean

all: test

test: test_phase1 test_phase2 test_phase3 test_phase4

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

test_phase3: tests/test_phase3_bridge.cpp Kick/CustomSequencer/kick_hardware_bridge.hpp AudIO/CustomEngines/kick_sample_chopper.hpp AudIO/CustomEngines/kick_custom_firmware_core.hpp
	@echo "------------------------------------------------------------"
	@echo "Building & running Phase 3 Hardware Bridge & ComProtocol Suite..."
	@echo "------------------------------------------------------------"
	$(CXX) $(CXXFLAGS) tests/test_phase3_bridge.cpp -o build_test_phase3
	./build_test_phase3

test_phase4: tests/test_phase4_app.cpp Kick/CustomSequencer/kick_firmware_app.hpp Kick/CustomSequencer/kick_hardware_bridge.hpp AudIO/CustomEngines/kick_sample_chopper.hpp AudIO/CustomEngines/kick_custom_firmware_core.hpp
	@echo "------------------------------------------------------------"
	@echo "Building & running Phase 4 Master Audio App & Mixer Suite..."
	@echo "------------------------------------------------------------"
	$(CXX) $(CXXFLAGS) tests/test_phase4_app.cpp -o build_test_phase4
	./build_test_phase4

clean:
	rm -f build_test_phase1 build_test_phase2 build_test_phase3 build_test_phase4 build_test_runner
