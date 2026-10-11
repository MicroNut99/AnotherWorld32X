# Another World 32X (RAWGL32X) - SH2 build: rawgl's engine (C++) + the 32X port, code in ROM.
#   make  -> rawgl_sh2.elf, rawgl_sh2.bin, rawgl.map
# Engine files: the same list as host/Makefile's rawgl_host32x (tested on the PC first).
# C++ without exceptions, RTTI, thread-safe statics or __cxa_atexit, linked with gcc (no
# libstdc++): port/cxx_support.cpp has the few runtime pieces.

ifdef GENDEV
ROOTDIR = $(GENDEV)
else
ROOTDIR = /opt/toolchains/sega
endif
PREFIX = $(ROOTDIR)/sh-elf/bin/sh-elf-
CC     = $(PREFIX)gcc
CXX    = $(PREFIX)g++
OBJC   = $(PREFIX)objcopy
SIZE   = $(PREFIX)size

ROOT := ../..
SRC  := $(ROOT)/rawgl-master
PORT := $(ROOT)/port

ENGINE := bitmap.cpp file.cpp engine.cpp script.cpp pak.cpp resource.cpp resource_win31.cpp \
          resource_3do.cpp aifcplayer.cpp scaler.cpp screenshot.cpp sfxplayer.cpp staticres.cpp \
          unpack.cpp util.cpp video.cpp
PORT_CXX := graphics_4bpp.cpp mixer_32x.cpp resource_nth_stub.cpp statics.cpp \
            systemstub_32x.cpp main_32x.cpp cxx_support.cpp pad_map.cpp
PORT_C   := aw_mixer.c plat_mars.c

DEFS   = -DRAWGL32X -DRAWGL32X_MARS -DBYPASS_PROTECTION -DNDEBUG -D__32X__ -DMARS -DT32X_NO_PACING \
         $(if $(BUILD_ID),-DT32X_BUILD_ID='"$(BUILD_ID)"')
COMMON = -m2 -mb -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing -Wall \
         -Wno-unused-function -I$(PORT) $(DEFS) $(EXTRA_CFLAGS) -MMD -MP
CFLAGS   = $(COMMON) -std=gnu11 -Wno-missing-braces
CXXFLAGS = $(COMMON) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
           -fno-asynchronous-unwind-tables -Wno-unused-variable
LDFLAGS  = -m2 -mb -nostartfiles -T rawgl.ld -Wl,--gc-sections -Wl,-Map=rawgl.map

OBJS := crt0.o \
        $(patsubst %.cpp,obj/engine/%.o,$(ENGINE)) \
        $(patsubst %.cpp,obj/port/%.o,$(PORT_CXX)) \
        $(patsubst %.c,obj/port/%.o,$(PORT_C)) \
        obj/port/syscalls.o

all: rawgl_sh2.bin

rawgl_sh2.elf: $(OBJS) rawgl.ld
	$(CC) $(LDFLAGS) $(OBJS) -lm -lc -lgcc -o $@
	$(SIZE) -A $@ | grep -E "sdcode|data|text|bss|Total"

# Flat image from ROM 0x8000: .sdcode + .data (copied to SDRAM), then .text.
rawgl_sh2.bin: rawgl_sh2.elf
	$(OBJC) -O binary -j .sdcode -j .data -j .text $< $@

crt0.o: crt0.s
	$(CC) -m2 -mb -c $< -o $@

obj/engine/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/port/%.o: $(PORT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/port/%.o: $(PORT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

obj/port/syscalls.o: syscalls.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -rf obj *.o *.elf *.bin rawgl.map

.PHONY: all clean
