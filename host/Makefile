# RAWGL32X host builds (PC, no SDL) - the test harness for the 32X port.
#   make            -> rawgl_host     reference: unchanged engine, original data files,
#                                     rawgl's renderer and mixer
#                      rawgl_host32x  the engine as built for the 32X (-DRAWGL32X): data pack
#                                     (AW_PACK) used in place, 4-bpp renderer, 32X mixer
#                      aw_pack        original data files -> data pack (+ size report)
#   make asan       -> the same three with AddressSanitizer (replaces gdb)
# Same compiler rules as the SH2 build: no exceptions, no RTTI.
SRC  = ../rawgl-master
PORT = ../port
CXX ?= g++
CC  ?= gcc
OPT ?= -O2
SAN ?=
CXXFLAGS = $(OPT) -g -Wall -Wno-unused-function -fno-exceptions -fno-rtti -DBYPASS_PROTECTION -MMD $(SAN)
CFLAGS   = $(OPT) -g -Wall -MMD $(SAN)
LDFLAGS  = $(SAN)

ENGINE = bitmap.cpp file.cpp engine.cpp graphics_soft.cpp script.cpp pak.cpp \
	resource.cpp resource_nth.cpp resource_win31.cpp resource_3do.cpp aifcplayer.cpp \
	scaler.cpp screenshot.cpp sfxplayer.cpp staticres.cpp unpack.cpp util.cpp video.cpp
# the 32X file list (port/mars/Makefile builds the same ones)
ENGINE32 = $(filter-out graphics_soft.cpp resource_nth.cpp,$(ENGINE))

REF_OBJS = $(addprefix obj/,$(ENGINE:.cpp=.o)) obj/statics.o
HOST_OBJS = obj/systemstub_host.o obj/mixer_host.o obj/main_host.o obj/pad_map.o
H32_OBJS = $(addprefix obj32/,$(ENGINE32:.cpp=.o)) obj32/graphics_4bpp.o obj32/mixer_32x.o \
	obj32/resource_nth_stub.o obj32/aw_mixer.o \
	obj32/systemstub_host.o obj32/main_host.o obj32/host32x_plat.o obj32/statics.o obj32/pad_map.o

all: rawgl_host rawgl_host32x aw_pack test_padmap

rawgl_host: $(REF_OBJS) $(HOST_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ -lz
rawgl_host32x: $(H32_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ -lz
test_padmap: obj/test_padmap.o obj/pad_map.o
	$(CXX) $(LDFLAGS) -o $@ $^
obj/libref.a: $(REF_OBJS)
	rm -f $@; ar rcs $@ $^
aw_pack: obj/aw_pack.o obj/libref.a obj/mixer_host.o
	$(CXX) $(LDFLAGS) -o $@ obj/aw_pack.o obj/mixer_host.o obj/libref.a -lz

obj/%.o: $(SRC)/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/%.o: %.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/%.o: $(PORT)/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/aw_pack.o: ../tools/aw_pack.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj32/%.o: $(SRC)/%.cpp
	@mkdir -p obj32
	$(CXX) $(CXXFLAGS) -DRAWGL32X -c $< -o $@
obj32/%.o: $(PORT)/%.cpp
	@mkdir -p obj32
	$(CXX) $(CXXFLAGS) -DRAWGL32X -c $< -o $@
obj32/%.o: $(PORT)/%.c
	@mkdir -p obj32
	$(CC) $(CFLAGS) -DRAWGL32X -c $< -o $@
obj32/%.o: %.cpp
	@mkdir -p obj32
	$(CXX) $(CXXFLAGS) -DRAWGL32X -c $< -o $@

asan:
	$(MAKE) clean
	$(MAKE) OPT=-O1 SAN="-fsanitize=address -fno-omit-frame-pointer" all

clean:
	rm -rf obj obj32 rawgl_host rawgl_host32x aw_pack test_padmap

-include $(wildcard obj/*.d obj32/*.d)
