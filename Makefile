# Framely build (macOS, clang). No CMake needed.
SDK      := vendor/openfx
SUP      := $(SDK)/Support
CXX      := clang++
ARCHS    ?= -arch arm64
CXXFLAGS := -std=c++17 -O3 -fvisibility=hidden -I$(SDK)/include -I$(SUP)/include -I$(SUP)/Plugins/include -Isrc $(ARCHS) -mmacosx-version-min=11.0 -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations
LDFLAGS  := -bundle -framework OpenGL $(ARCHS) -mmacosx-version-min=11.0
SUPSRC   := $(addprefix $(SUP)/Library/,ofxsCore.cpp ofxsImageEffect.cpp ofxsInteract.cpp ofxsLog.cpp ofxsMultiThread.cpp ofxsParams.cpp ofxsProperty.cpp ofxsPropertyValidation.cpp)
SUPOBJ   := $(patsubst $(SUP)/Library/%.cpp,build/sup/%.o,$(SUPSRC))

all: probe

build/sup/%.o: $(SUP)/Library/%.cpp
	@mkdir -p build/sup
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- probe ----
PROBE := build/FramelyProbe.ofx.bundle
probe: $(PROBE)/Contents/MacOS/FramelyProbe.ofx
$(PROBE)/Contents/MacOS/FramelyProbe.ofx: probe/Probe.cpp $(SUPOBJ)
	@mkdir -p $(PROBE)/Contents/MacOS
	$(CXX) $(CXXFLAGS) probe/Probe.cpp $(SUPOBJ) $(LDFLAGS) -o $@
	sed -e 's/@NAME@/FramelyProbe/g' -e 's/@ID@/com.framely.probe/g' packaging/Info.plist.in > $(PROBE)/Contents/Info.plist
	codesign --force -s - $(PROBE)

clean:
	rm -rf build

.PHONY: all probe clean
