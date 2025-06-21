#pragma once

// Ignores a warning thrown by some clay internal workings that are irrelevant here
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <clay.h>
#pragma GCC diagnostic pop

#include <raylib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "TextUtils.hpp"

void InitRenderer(int screenWidth, int screenHeight);
// userData should be a pointer to TextRenderContext
Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData);
void RenderFrame(Clay_RenderCommandArray cmds, TextRenderContext& textCtx);

