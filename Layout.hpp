#pragma once

#define CLAY_IMPLMENTATION
#include <clay.h>

#include <vector>

#include "Allocators.hpp"
#include "Data.hpp"

struct LayoutInfo {
    Clay_RenderCommandArray renderCommands;
    long hoveredSongId;
    long hoveredCollectionId;
};

LayoutInfo MakeLayout(const PlaybackState& state,
                      Arena<SongEntry>& songArena,
                      Arena<CollectionEntry>& collectionArena,
                      const std::vector<int>& songs,
                      const UIStringPool& pool);

