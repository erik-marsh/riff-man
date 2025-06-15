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
    std::string_view uiName;
    std::string_view uiByArtist;
};

struct CollectionEntry {
    EntityId id;
    std::string_view uiName;
};

struct PlaybackState {
    Music audioBuffer;
    const SongEntry* metadata;
    float duration;
    float currTime;
};

// Solves a barely-existent problem
// I just want my strings to be owned by a singleton (effectively)
// I honestly don't know why, I just don't want my song/album metadata to own any strings right now
class UIStringPool {
 public:
    // Register a string with the pool and return a view to it.
    // If the string has already been registered, returns a view to the existing string.
    std::string_view Register(const std::string& str, TextRenderContext& textCtx) {
        auto [it, _] = m_store.emplace(str);
        std::string_view view(*it);
        m_textures.insert({view, RenderText(str, textCtx)});
        return view;
    }

    std::string_view Register(const char* str, TextRenderContext& textCtx) {
        return Register(std::string(str), textCtx);
    }

    std::string_view Register(const unsigned char* str, TextRenderContext& textCtx) {
        return Register(reinterpret_cast<const char*>(str), textCtx);
    }

    std::string_view Register(std::string_view str, TextRenderContext& textCtx) {
        return Register(std::string(str), textCtx);
    }

    const Texture& GetTexture(std::string_view str) const {
        return m_textures.at(str);
    }

 private:
    // fun fact: std::hash<std::string> == std::hash<std::string_view>
    std::unordered_set<std::string> m_store;
    std::unordered_map<std::string_view, Texture> m_textures;
};

