/*
 * RAWGL32X - aw_pack: original Another World data files -> the data pack the 32X uses in place.
 *
 *   aw_pack DATA_DIR OUT.pack
 *
 * Reads the data with rawgl's own code (Resource: DOS memlist.bin + bankNN, or the Amiga /
 * Atari banks with rawgl's built-in tables), unpacks every resource with rawgl's unpacker,
 * and writes them one after another, unpacked, 4-byte aligned.  Built by host/Makefile.
 * It also prints the size report: per type, and the fixed load of every game part.
 *
 * Pack layout (big-endian, read by resource.cpp under RAWGL32X):
 *   0  "AWPK"   4  version 1   6  data type   8  entry count   10  flags (bit 0: password screen)
 *   12 total size (bytes)
 *   16 entries, 16 bytes each: type, rankNum, present, 0, offset, size, 0
 */
#include "../rawgl-master/resource.h"
#include "../rawgl-master/util.h"

#include <stdarg.h>
// the engine library links rawgl's script code, which needs the host mixer; it logs through this
void host_logf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt); vprintf(fmt, va); va_end(va);
}

static void put16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s DATA_DIR OUT.pack\n", argv[0]);
		return 1;
	}
	g_debugMask = DBG_INFO;
	Resource res(0, argv[1]);
	res.detectVersion();
	const int type = res.getDataType();
	if (type != Resource::DT_DOS && type != Resource::DT_AMIGA && type != Resource::DT_ATARI) {
		fprintf(stderr, "*** data type %d: only the DOS, Amiga and Atari versions are supported\n", type);
		return 1;
	}
	res.readEntries();
	const int count = res._numMemList;
	const uint32_t headerSize = 16 + count * 16;
	uint32_t cap = 4 * 1024 * 1024, size = (headerSize + 3) & ~3;
	uint8_t *pack = (uint8_t *)calloc(cap, 1);
	memcpy(pack, "AWPK", 4);
	put16(pack + 4, 1);
	put16(pack + 6, type);
	put16(pack + 8, count);
	put16(pack + 10, res._hasPasswordScreen ? 1 : 0);

	static const char *names[] = { "sound", "music", "bitmap", "palette", "bytecode", "shape", "bank2" };
	uint32_t byType[8] = { 0 }, numType[8] = { 0 }, missing = 0;
	for (int i = 0; i < count; ++i) {
		MemEntry *me = &res._memList[i];
		uint8_t *e = pack + 16 + i * 16;
		e[0] = me->type;
		e[1] = me->rankNum;
		if (me->bankNum == 0 || me->unpackedSize == 0) {
			continue;  // not part of the game data (as in the original)
		}
		const uint32_t need = me->unpackedSize > me->packedSize ? me->unpackedSize : me->packedSize;
		if (size + need + 4 > cap) {
			fprintf(stderr, "*** pack larger than 4 MB\n");
			return 1;
		}
		if (!res.readBank(me, pack + size)) {
			fprintf(stderr, "warning: resource %d (bank %d) could not be read - left out\n", i, me->bankNum);
			++missing;
			continue;
		}
		e[2] = 1;
		put32(e + 4, size);
		put32(e + 8, me->unpackedSize);
		if (me->type < 7) {
			byType[me->type] += me->unpackedSize;
			++numType[me->type];
		}
		size = (size + me->unpackedSize + 3) & ~3;
	}
	put32(pack + 12, size);
	FILE *f = fopen(argv[2], "wb");
	if (!f || fwrite(pack, 1, size, f) != size) {
		fprintf(stderr, "*** cannot write %s\n", argv[2]);
		return 1;
	}
	fclose(f);

	printf("data type %d, %d entries, %u left out; pack %u bytes (%.1f KB)\n", type, count, missing, size, size / 1024.0);
	printf("password screen: %s\n", res._hasPasswordScreen ? "yes" : "NO (DOS demo data, or its bank file is missing)");
	for (int t = 0; t < 7; ++t) {
		if (numType[t]) printf("  %-9s %3u  %8u bytes\n", names[t], numType[t], byType[t]);
	}
	printf("fixed load per part (palette + code + polygons + shared polygons):\n");
	for (int p = 0; p < 10; ++p) {
		uint32_t sum = 0;
		for (int k = 0; k < 4; ++k) {
			const int n = Resource::_memListParts[p][k];
			if (n && n < count && pack[16 + n * 16 + 2]) sum += (pack[16 + n * 16 + 8] << 24) | (pack[16 + n * 16 + 9] << 16) | (pack[16 + n * 16 + 10] << 8) | pack[16 + n * 16 + 11];
		}
		printf("  part %d: %u bytes (%.1f KB)\n", 16000 + p, sum, sum / 1024.0);
	}
	free(pack);
	return 0;
}
