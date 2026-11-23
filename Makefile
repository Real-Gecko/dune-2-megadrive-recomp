BUILD := build
WIN_BUILD := $(BUILD)/win
WIN_DEPS := $(BUILD)/win-deps
SDL3_VER := 3.4.20
SDL3_WIN := $(WIN_DEPS)/SDL3-$(SDL3_VER)/x86_64-w64-mingw32

.PHONY: all dune2 win clean

all: dune2

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD)

dune2: | $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j --target dune2

$(SDL3_WIN):
	mkdir -p $(WIN_DEPS)
	curl -fL https://github.com/libsdl-org/SDL/releases/download/release-$(SDL3_VER)/SDL3-devel-$(SDL3_VER)-mingw.tar.gz | tar xz -C $(WIN_DEPS)

$(WIN_BUILD)/CMakeCache.txt: | $(SDL3_WIN)
	cmake -S . -B $(WIN_BUILD) -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
		-DCMAKE_PREFIX_PATH=$(abspath $(SDL3_WIN))

# Cross-compiled Windows build: dune2.exe and SDL3.dll.
win: | $(WIN_BUILD)/CMakeCache.txt
	cmake --build $(WIN_BUILD) -j --target dune2
	cp $(SDL3_WIN)/bin/SDL3.dll .

clean:
	rm -rf $(BUILD) dune2 dune2.exe SDL3.dll
