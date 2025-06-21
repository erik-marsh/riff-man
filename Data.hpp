#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <raylib.h>

#include "TextUtils.hpp"

// see https://schema.org/MusicRecording for some info
// also look up the multimedia section of "awesome-falsehood"
enum class AudioFormat {
    MP3,
    OPUS
};

using EntityId = long int;
constexpr EntityId NO_ENTITY = -1;

struct SongEntry {
    EntityId id;
    std::string filename;
    AudioFormat fileFormat;
    std::string name;
    std::string byArtist;
};

struct CollectionEntry {
    EntityId id;
    std::string name;
};

struct PlaybackState {
    Music audioBuffer;
    const SongEntry* metadata;
    float duration;
    float currTime;
};

