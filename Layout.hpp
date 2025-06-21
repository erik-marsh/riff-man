#pragma once

#define CLAY_IMPLMENTATION
#include <clay.h>

#include <span>
#include <vector>

#include "Allocators.hpp"
#include "Data.hpp"

struct LayoutInput {
    int songIndex;
    int collectionIndex;
};

struct LayoutResult {
    Clay_RenderCommandArray renderCommands;
    LayoutInput input;
};

void InitLayoutArenas(int nChars, int nCustom);

struct TextRenderContext;
LayoutResult MakeLayout(const PlaybackState& state,
                        std::span<const SongEntry> songs,
                        std::span<const CollectionEntry> collections);
