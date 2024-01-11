/* This file is used to instantiate code for miniaudio */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MA_NO_OPUS
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"