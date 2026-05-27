BUILD ?= Release

BUILDDIR ?= build

all:
	cmake -S . -B $(BUILDDIR) -DCMAKE_BUILD_TYPE=$(BUILD)
	cmake --build $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
