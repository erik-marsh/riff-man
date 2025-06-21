#include "Layout.hpp"

#include <cassert>
#include <format>
#include <ranges>
#include <unordered_map>

#include "Allocators.hpp"
#include "Casts.hpp"
#include "LayoutElements.hpp"
#include "TextUtils.hpp"

// This works a lot differently than my other allocators
// I'm making a lot of strong assumptions in the other
// TODO: figure out better allocation strategies
struct StringArena {
    char* arr;
    int capacity = 0;
    int top = 0;

    void Reserve(size_t cap) {
        arr = static_cast<char*>(std::malloc(cap));
        capacity = cap;
    }

    char* Allocate(int size) {
        assert(arr && "No buffer allocated for StringArena");
        char* start = &arr[top];
        top += size;
        assert((top < capacity) && "Overallocation in StringArena");
        return start;
    }

    void Reset() {
        top = 0;
    }
};

StringArena g_stringArena;
Arena<CustomElement> g_customArena;

std::unordered_map<std::string, TGAImage> g_renderedTextCache;

void InitLayoutArenas(int nChars, int nCustom) {
    g_stringArena.Reserve(nChars);
    g_customArena.Reserve(nCustom);
}

namespace colors {
    constexpr Clay_Color white     { 255, 255, 255, 255 };
    constexpr Clay_Color black     { 0, 0, 0, 255 };
    constexpr Clay_Color lightgray { 100, 100, 100, 255 };
    constexpr Clay_Color darkgray  { 50, 50, 50, 255 };
    constexpr Clay_Color darkergray{ 35, 35, 35, 255 };
}

constexpr Clay_CornerRadius rounding{ 10, 10, 10, 10 };
constexpr Clay_ChildAlignment centered{
    .x = CLAY_ALIGN_X_CENTER,
    .y = CLAY_ALIGN_Y_CENTER
};
constexpr Clay_Sizing growAll{
    .width = CLAY_SIZING_GROW(),
    .height = CLAY_SIZING_GROW()
};

constexpr int panelSpacing = 2;
constexpr Clay_ElementDeclaration root{
    .layout = {
        .sizing = growAll,
        .childGap = panelSpacing,
        .layoutDirection = CLAY_TOP_TO_BOTTOM }
};
constexpr Clay_ElementDeclaration navigation{
    .layout = {
        .sizing = { .width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_PERCENT(0.85) },
        .childGap = panelSpacing,
        .layoutDirection = CLAY_LEFT_TO_RIGHT },
    .backgroundColor = colors::black
};
constexpr Clay_ElementDeclaration collectionView {
    .layout = {
        .sizing = { .width = CLAY_SIZING_PERCENT(0.20), .height = CLAY_SIZING_GROW() },
        .padding = CLAY_PADDING_ALL(16),
        .childGap = 16,
        .layoutDirection = CLAY_TOP_TO_BOTTOM },
    .backgroundColor = colors::darkgray,
    .scroll = { .vertical = true }
};
constexpr Clay_ElementDeclaration songView {
    .layout = {
        .sizing = growAll,
        .padding = CLAY_PADDING_ALL(16),
        .childGap = 16,
        .layoutDirection = CLAY_TOP_TO_BOTTOM },
    .backgroundColor = colors::darkgray,
    .scroll = { .vertical = true }
};
constexpr Clay_ElementDeclaration nowPlaying{
    .layout = {
        .sizing = growAll,
        .padding = CLAY_PADDING_ALL(16),
        .childGap = 16 },
    .backgroundColor = colors::darkergray
};
constexpr Clay_ElementDeclaration trackInfo{
    .layout = {
        .sizing = growAll,
        .childAlignment = centered,
        .layoutDirection = CLAY_TOP_TO_BOTTOM }
};
constexpr Clay_ElementDeclaration timeContainer{
    .layout = {
        .sizing = growAll,
        .childAlignment = centered }
};
constexpr Clay_ElementDeclaration progressBar{
    .layout = {
        .sizing = { .width = CLAY_SIZING_PERCENT(0.65f), .height = CLAY_SIZING_GROW() },
        .childAlignment = centered }
};


void MakeProgressBar(float currTime, float duration) {
    constexpr Clay_Sizing fullBar{
        .width = CLAY_SIZING_PERCENT(0.95f),
        .height = CLAY_SIZING_FIXED(25)
    };

    currTime = currTime > duration ? duration : currTime;
    const float progress = duration > 0.0f ? currTime / duration : 0.0f;
    const Clay_Sizing partialBar{
        .width = CLAY_SIZING_PERCENT(progress),
        .height = CLAY_SIZING_GROW()
    };

    CLAY({ .layout = { .sizing = fullBar }, .backgroundColor = colors::black }) {
        CLAY({ .layout = { .sizing = partialBar }, .backgroundColor = colors::white }) {}
    }
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
Clay_ElementDeclaration MakeImageConfig(const Texture& tex) {
    return {
        .layout = {
            .sizing = {
                .width = CLAY_SIZING_FIXED(tex.width),
                .height = CLAY_SIZING_FIXED(tex.height) } },
        .image = {
            .imageData = &const_cast<Texture&>(tex),
            .sourceDimensions = { .width = tex.width, .height = tex.height } }
    };
};
#pragma GCC diagnostic pop

Clay_String MakeTimeString(float seconds) {
    const int asInt = static_cast<int>(seconds);
    const int ss = asInt % 60;
    const int mm = asInt / 60;
    const int hh = mm / 60;

    constexpr int cap = 10;
    char* str = g_stringArena.Allocate(cap);

    std::format_to_n_result<char*> res;
    if (hh == 0)
        res = std::format_to_n(str, cap - 1, "{}:{:02}", mm, ss);
    else
        res = std::format_to_n(str, cap - 1, "{}:{:02}:{:02}", hh, mm, ss);
    *res.out = '\0';

    return {
        .length = static_cast<int32_t>(res.size),
        .chars = str
    };
};

// Returns if the button is hovered or not.
bool MakeButton(std::string_view str) {
    constexpr Clay_ElementDeclaration buttonFrame{
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT() },
            .padding = CLAY_PADDING_ALL(16),
            .childAlignment = centered },
        .backgroundColor = colors::lightgray,
        .cornerRadius = rounding
    };

    bool hovered = false;
    CLAY(buttonFrame) {
        CLAY_TEXT(casts::clay::String(str), CLAY_TEXT_CONFIG({}));
        hovered = Clay_Hovered();
    }
    return hovered;
}

// ugly disgusting wokraround for how i draw utf8 text to a single texture buffer.
// that text gets rendered after all other render commands, and therefore
// will render over everything else. this will likely be replaced if i need
// to actually have floating containers.
Clay_ElementDeclaration AppendUTF8Scissor(const Clay_ElementDeclaration& decl) {
    int i = g_customArena.Allocate();
    g_customArena.arr[i].type = CustomElement::Type::UTF8_TEXT_SCISSOR;

    Clay_ElementDeclaration ret = decl;
    ret.custom = { .customData = reinterpret_cast<void*>(&g_customArena.arr[i]) };
    return ret;
}

LayoutResult MakeLayout(const PlaybackState& state,
                        std::span<const SongEntry> songs,
                        std::span<const CollectionEntry> collections) {
    g_stringArena.Reset();
    g_customArena.Reset();

    LayoutResult ret;
    ret.input.songIndex = -1;
    ret.input.collectionIndex = -1;

    Clay_BeginLayout();

    CLAY(root) {
        CLAY(navigation) {
            CLAY(AppendUTF8Scissor(collectionView)) {
                for (const auto& [i, coll] : std::views::enumerate(collections)) {
                    CLAY({}) {
                        bool hovered = MakeButton(coll.name);
                        if (hovered)
                            ret.input.collectionIndex = i;
                    }
                }
            }
            CLAY(AppendUTF8Scissor(songView)) {
                for (const auto& [i, song] : std::views::enumerate(songs)) {
                    CLAY({}) {
                        bool hovered = MakeButton(song.name);
                        if (hovered)
                            ret.input.songIndex = i;
                    }
                }
            }
        }
        CLAY(nowPlaying) {
            CLAY(AppendUTF8Scissor(trackInfo)) {
                if (state.metadata) {
                    CLAY_TEXT(casts::clay::String(state.metadata->name), CLAY_TEXT_CONFIG({}));
                    CLAY_TEXT(casts::clay::String(state.metadata->byArtist), CLAY_TEXT_CONFIG({}));
                    CLAY_TEXT(CLAY_STRING("album"), CLAY_TEXT_CONFIG({}));
                }
            }
            CLAY(timeContainer) {
                CLAY_TEXT(MakeTimeString(state.currTime), CLAY_TEXT_CONFIG({}));
            }
            CLAY(progressBar) {
                MakeProgressBar(state.currTime, state.duration);
            }
            CLAY(timeContainer) {
                CLAY_TEXT(MakeTimeString(state.duration), CLAY_TEXT_CONFIG({}));
            }
        }
    }

    ret.renderCommands = Clay_EndLayout();
    return ret;
}

