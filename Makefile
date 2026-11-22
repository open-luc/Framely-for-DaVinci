# Framely build (macOS, clang). No CMake needed.
SDK      := vendor/openfx
SUP      := $(SDK)/Support
CXX      := clang++
ARCHS    ?= -arch arm64
CXXFLAGS := -std=c++17 -O3 -fvisibility=hidden -I$(SDK)/include -I$(SUP)/include -I$(SUP)/Plugins/include -Isrc $(ARCHS) -mmacosx-version-min=11.0 -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations
LDFLAGS  := -bundle -framework OpenGL $(ARCHS) -mmacosx-version-min=11.0
SUPSRC   := $(addprefix $(SUP)/Library/,ofxsCore.cpp ofxsImageEffect.cpp ofxsInteract.cpp ofxsLog.cpp ofxsMultiThread.cpp ofxsParams.cpp ofxsProperty.cpp ofxsPropertyValidation.cpp)
B        ?= build
SUPOBJ   := $(patsubst $(SUP)/Library/%.cpp,$(B)/sup/%.o,$(SUPSRC))

all: framely

$(B)/sup/%.o: $(SUP)/Library/%.cpp
	@mkdir -p $(B)/sup
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- probe ----
PROBE := build/FramelyProbe.ofx.bundle
probe: $(PROBE)/Contents/MacOS/FramelyProbe.ofx
$(PROBE)/Contents/MacOS/FramelyProbe.ofx: probe/Probe.cpp $(SUPOBJ)
	@mkdir -p $(PROBE)/Contents/MacOS
	$(CXX) $(CXXFLAGS) probe/Probe.cpp $(SUPOBJ) $(LDFLAGS) -o $@
	sed -e 's/@NAME@/FramelyProbe/g' -e 's/@ID@/com.framely.probe/g' packaging/Info.plist.in > $(PROBE)/Contents/Info.plist
	codesign --force -s - $(PROBE)

# ---- Framely ----
FR := $(B)/Framely.ofx.bundle
framely: $(FR)/Contents/MacOS/Framely.ofx
$(FR)/Contents/MacOS/Framely.ofx: src/FramelyPlugin.cpp src/FramelyCore.h src/FramelyUpdate.h $(SUPOBJ)
	@mkdir -p $(FR)/Contents/MacOS
	$(CXX) $(CXXFLAGS) src/FramelyPlugin.cpp $(SUPOBJ) $(LDFLAGS) -o $@
	sed -e 's/@NAME@/Framely/g' -e 's/@ID@/com.framely.pictureinpicture/g' packaging/Info.plist.in > $(FR)/Contents/Info.plist
	codesign --force -s - $(FR)

# Installs into the user-owned bundle folder created once with admin rights. Resolve must restart to load it.
install: framely
	@# swap Contents by rename (overwriting a loaded plugin in place can get killed by macOS)
	B=/Library/OFX/Plugins/Framely.ofx.bundle; rm -rf "$$B/Contents.old"; [ ! -d "$$B/Contents" ] || mv "$$B/Contents" "$$B/Contents.old"; ditto $(FR)/Contents "$$B/Contents"; rm -rf "$$B/Contents.old"

harness: build/harness-bin
build/harness-bin: tools/harness.cpp src/FramelyCore.h
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Isrc tools/harness.cpp -lz -o $@

# Universal (arm64 + x86_64) release zip in dist/
dist:
	$(MAKE) B=build/universal ARCHS="-arch arm64 -arch x86_64" framely
	@mkdir -p dist
	rm -f dist/Framely-macOS.zip
	cd build/universal && ditto -c -k --keepParent Framely.ofx.bundle ../../dist/Framely-macOS.zip
	@echo built dist/Framely-macOS.zip

clean:
	rm -rf build

.PHONY: all probe framely install harness dist clean
