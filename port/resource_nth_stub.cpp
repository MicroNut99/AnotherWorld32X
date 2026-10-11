/* RAWGL32X - the 15th/20th anniversary data reader (resource_nth.cpp, needs zlib) is not
   built for the 32X; Resource only asks it for those editions, which the pack never holds. */
#include "../rawgl-master/resource_nth.h"
ResourceNth *ResourceNth::create(int edition, const char *dataPath) { return 0; }
