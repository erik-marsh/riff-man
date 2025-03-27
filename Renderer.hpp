#pragma once

// Ignores a warning thrown by some clay internal workings that are irrelevant here
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <clay.h>
#pragma GCC diagnostic pop

#include <raylib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

// userData should be FT_Face (which is a pointer type)
void RenderFrame(Clay_RenderCommandArray cmds, FT_Face face);
Clay_Dimensions FTMeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData);
void FTPrintError(int error);
