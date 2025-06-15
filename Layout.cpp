#include "Layout.hpp"

#include <cassert>
#include <format>
#include <ranges>

struct StringArena {
    static constexpr int arrSize = 1024;
    char arr[arrSize];
    int top = 0;

    char* Allocate(int size) {
        char* start = &arr[top];
        top += size;
        assert((top < arrSize) && "Overallocation in StringArena");
        return start;
    }

    void Reset() {
        top = 0;
    }
};

StringArena stringArena;

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
    char* str = stringArena.Allocate(cap);

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

Clay_ElementDeclaration MakeButtonConfig() {
    return {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT() },
            .padding = CLAY_PADDING_ALL(16),
            .childAlignment = centered },
        .backgroundColor = colors::lightgray,
        .cornerRadius = rounding
    };
};

// Returns if the button is hovered or not.
bool MakeButton(const Texture& tex) {
    bool hovered = false;
    CLAY(MakeButtonConfig()) {
        CLAY(MakeImageConfig(tex)) {}
        hovered = Clay_Hovered();
    }
    return hovered;
}

LayoutResult MakeLayout(const PlaybackState& state,
                       std::span<const SongEntry> songs,
                       std::span<const CollectionEntry> collections,
                       const UIStringPool& pool) {
    stringArena.Reset();

    LayoutResult ret;
    ret.input.songIndex = -1;
    ret.input.collectionIndex = -1;

    Clay_BeginLayout();

    CLAY(root) {
        CLAY(navigation) {
            CLAY(collectionView) {
                for (int i = 0; i < collections.size(); i++) {
                    const CollectionEntry& coll = collections[i];
                    const Texture& tex = pool.GetTexture(coll.uiName);
                    const bool hovered = MakeButton(tex);
                    if (hovered)
                        ret.input.collectionIndex = i;
                }
            }
            CLAY(songView) {
                for (int i = 0; i < songs.size(); i++) {
                    const SongEntry& song = songs[i];
                    const Texture& tex = pool.GetTexture(song.uiName);
                    const bool hovered = MakeButton(tex);
                    if (hovered)
                        ret.input.songIndex = i;
                }
            }
        }
        CLAY(nowPlaying) {
            CLAY({ .layout = { .sizing = growAll, .childAlignment = centered, .layoutDirection = CLAY_TOP_TO_BOTTOM }}) {
                if (state.metadata) {
                    const Texture& title = pool.GetTexture(state.metadata->uiName);
                    const Texture& artist = pool.GetTexture(state.metadata->uiByArtist);

                    CLAY(MakeImageConfig(title)) {}
                    CLAY(MakeImageConfig(artist)) {}
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

