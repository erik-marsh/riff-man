#pragma once

#include <clay.h>
#include <raylib.h>

Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData);
void RenderFrame(Clay_RenderCommandArray cmds, Font* fonts);

