#pragma once

// Ignores a warning thrown by some clay internal workings that are irrelevant here
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <clay.h>
#pragma GCC diagnostic pop

#include <raylib.h>

Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData);
void RenderFrame(Clay_RenderCommandArray cmds, Font* fonts);

