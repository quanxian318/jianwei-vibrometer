# vibmon - AX7010 Linux application for the vision vibration meter.
#
# Host build and unit tests (Linux / WSL):   make test
# Target build inside the PetaLinux SDK shell:
#   source <sdk>/environment-setup-cortexa9t2hf-neon-xilinx-linux-gnueabi
#   make            (CXX/CC/CXXFLAGS/LDFLAGS come from the SDK environment)
#
# Optional: ST VL53L1X ULD sources in third_party/vl53l1x_uld enable ToF ranging.

CXX ?= g++
CC ?= gcc
BUILD ?= build
PROTO_DIR := ../../../shared/vibproto/src
ULD_DIR := third_party/vl53l1x_uld
AXV_DIR := third_party/axvibration

CXXFLAGS ?= -O2 -g
CFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -MMD -MP
override CFLAGS += -MMD -MP
override CPPFLAGS += -Isrc -I$(PROTO_DIR) -Isrc/drv/vl53l1x_uld_port -I$(AXV_DIR)
override LDFLAGS += -pthread

LIB_DIRS := src/util src/hw src/drv src/drv/vl53l1x_uld_port src/link src/measure src/stereo src/gfx src/app src/ui
LIB_CPP := $(filter-out src/app/main.cpp,$(foreach d,$(LIB_DIRS),$(wildcard $(d)/*.cpp))) \
           $(PROTO_DIR)/vp_json.cpp $(PROTO_DIR)/vp_frame.cpp
# The team's multi-point vibration core (C, with sub-Nyquist search extension).
LIB_C := $(AXV_DIR)/portable_vibration.c $(AXV_DIR)/spatial_core.c $(AXV_DIR)/vb_runtime.c
ifneq ($(wildcard $(ULD_DIR)/VL53L1X_api.c),)
  override CPPFLAGS += -DHAVE_VL53L1X_ULD -I$(ULD_DIR)
  LIB_C += $(ULD_DIR)/VL53L1X_api.c $(wildcard $(ULD_DIR)/VL53L1X_calibration.c)
endif

vpath %.cpp $(sort $(dir $(LIB_CPP))) tools tests src/app
vpath %.c $(sort $(dir $(LIB_C)))

LIB_OBJ := $(addprefix $(BUILD)/obj/,$(notdir $(LIB_CPP:.cpp=.o) $(LIB_C:.c=.o)))
LIB := $(BUILD)/libvibmon.a

APPS := $(BUILD)/vibmond $(BUILD)/vibmonctl $(BUILD)/carrier_test $(BUILD)/vision_ref $(BUILD)/clip_track $(BUILD)/lk_vectors $(BUILD)/stereo_eval $(BUILD)/sgm_vectors \
        $(BUILD)/plcam $(BUILD)/clip_multipoint $(BUILD)/pdm_vectors $(BUILD)/wav_acoustic
TEST_SRC := $(wildcard tests/test_*.cpp)
TESTS := $(patsubst tests/%.cpp,$(BUILD)/%,$(TEST_SRC))

.PHONY: all test integration-test clean
all: $(APPS)

$(BUILD)/obj/%.o: %.cpp | $(BUILD)/obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/vibmond: $(BUILD)/obj/main.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/vibmonctl: $(BUILD)/obj/vibmonctl.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/carrier_test: $(BUILD)/obj/carrier_test.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/vision_ref: $(BUILD)/obj/vision_ref.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/clip_track: $(BUILD)/obj/clip_track.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/lk_vectors: $(BUILD)/obj/lk_vectors.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/stereo_eval: $(BUILD)/obj/stereo_eval.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/sgm_vectors: $(BUILD)/obj/sgm_vectors.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/clip_multipoint: $(BUILD)/obj/clip_multipoint.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/plcam: $(BUILD)/obj/plcam.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/pdm_vectors: $(BUILD)/obj/pdm_vectors.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/wav_acoustic: $(BUILD)/obj/wav_acoustic.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/multipoint_bench: $(BUILD)/obj/multipoint_bench.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_%: $(BUILD)/obj/test_%.o $(LIB)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/obj:
	mkdir -p $@

test: $(TESTS)
	@set -e; for t in $(TESTS); do $$t; done

integration-test: all
	python3 tests/test_prepare_an5642_image.py
	python3 tests/test_app_host.py $(BUILD)/vibmond
	sh tests/smoke_host.sh $(BUILD)

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/obj/*.d)
