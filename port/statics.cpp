/* Static members that rawgl's main.cpp defines (same defaults); shared by the host builds,
   aw_pack and the 32X build (port/main_32x.cpp has its own copy). */
#include "../rawgl-master/graphics.h"
#include "../rawgl-master/script.h"
#include "../rawgl-master/video.h"

bool Graphics::_is1991 = false;
bool Graphics::_use555 = false;
bool Video::_useEGA = false;
Difficulty Script::_difficulty = DIFFICULTY_NORMAL;
bool Script::_useRemasteredAudio = true;
