#include <raylib.h>
#define CLAY_IMPLEMENTATION
#include <clay.h>

#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "Renderer.hpp"

constexpr Clay_Vector2 RaylibToClayVector2(Vector2 vector) {
    return Clay_Vector2{
        .x = vector.x,
        .y = vector.y
    };
}

constexpr Clay_String ToClayString(const std::string& str) {
    return {
        .length = static_cast<int32_t>(str.size()),
        .chars = str.data()
    };
}

constexpr Clay_String ToClayString(std::string_view str) {
    return {
        .length = static_cast<int32_t>(str.size()),
        .chars = str.data()
    };
}

// int32_t for compatibility with Clay_String.length
template <int32_t N>
struct StringBuffer {
    // assuming utf8 here
    // allocating an extra char for null terminator compatibility
    std::array<char, N+1> buffer;
    int32_t size;
};

template<int32_t N>
constexpr Clay_String ToClayString(const StringBuffer<N>& str) {
    return {
        .length = str.size,
        .chars = str.buffer.data()
    };
}

// designed to hold a maximum of hhh:mm:ss (9 chars)
StringBuffer<9> playbackTime;
StringBuffer<9> trackLength;

// If we declare our layout in a function,
// the variables declared inside are not visible to the renderer.
// I just decided to use global buffers for any necessary string conversions.
template <int32_t N>
void TimeFormat(float seconds, StringBuffer<N>& dest) {
    const int asInt = static_cast<int>(seconds);
    const int ss = asInt % 60;
    const int mm = asInt / 60;
    const int hh = mm / 60;

    std::format_to_n_result<char*> res;
    if (hh == 0)
        res = std::format_to_n(dest.buffer.data(), dest.buffer.size()-1, "{}:{:02}", mm, ss);
    else
        res = std::format_to_n(dest.buffer.data(), dest.buffer.size()-1, "{}:{}:{:02}", hh, mm, ss);
    *res.out = '\0';
    dest.size = res.size;
}

std::string TimeFormat(float seconds) {
    const int asInt = static_cast<int>(seconds);
    const int ss = asInt % 60;
    const int mm = asInt / 60;
    const int hh = mm / 60;

    if (hh == 0)
        return std::format("{}:{:02}", mm, ss);
    else
        return std::format("{}:{}:{:02}", hh, mm, ss);
}

typedef struct
{
    Clay_Vector2 clickOrigin;
    Clay_Vector2 positionOrigin;
    bool mouseDown;
} ScrollbarData;

ScrollbarData scrollbar{
    .clickOrigin = { 0, 0 },
    .positionOrigin = { 0, 0 },
    .mouseDown = false
};

bool debugEnabled = false;

struct Song {
    Music buffer;
    std::string name;
    float length;  // in seconds
};

struct PlaybackState {
    Song* currSong;
    float currTime;
};

struct InputState {
    Song* hoveredSong;
};

Clay_RenderCommandArray TestLayout(PlaybackState& state, InputState& input, std::vector<Song>& songs) {
    static constexpr Clay_ElementDeclaration songEntryBase{
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIXED(75) },
            .padding = CLAY_PADDING_ALL(16),
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                .y = CLAY_ALIGN_Y_CENTER } },
        .backgroundColor = { 100, 100, 100, 255 },
        .cornerRadius = { 10, 10, 10, 10},
        // .border = { .color = { 255, 0, 0, 255 }, .width = { 15, 15, 15, 15, 0 },  },
    };

    // Clay internally caches font configs, so this isn't quite the best approach
    static Clay_TextElementConfig bodyText{
        .textColor = { 255, 255, 255, 255 },
        .fontSize = 24
    };

    static auto MakeSongEntry = [&input](Song& song, int songIndex) {
        Clay_ElementDeclaration config = songEntryBase;
        config.id = CLAY_IDI("Song", songIndex);
        config.userData = &song;
        CLAY(config) {
            CLAY_TEXT(ToClayString(song.name), &bodyText);
            if (Clay_Hovered())
                input.hoveredSong = &song;
        }
    };

    static const Clay_ElementDeclaration rootLayout{
        .id = CLAY_ID("RootContainer"),
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                .height = CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM }
    };

    static const Clay_ElementDeclaration navigationLayout{
        .id = CLAY_ID("NavigationPanel"),
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                .height = CLAY_SIZING_PERCENT(0.85) },
                    .padding = CLAY_PADDING_ALL(16),
                    .childGap = 16,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = { 50, 50, 50, 255 },
        .scroll = { .vertical = true }
    };

    static const Clay_ElementDeclaration controlLayout{
        .id = CLAY_ID("ControlPanel"),
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                .height = CLAY_SIZING_GROW(0) },
                    .padding = CLAY_PADDING_ALL(16),
                    .childGap = 16 },
        .backgroundColor = { 35, 35, 35, 255 }
    };

    input.hoveredSong = nullptr;
    Clay_BeginLayout();
    // TODO: eventually I will want a small gap between the two panels
    CLAY(rootLayout) {
        CLAY(navigationLayout) {
            for (auto [i, song] : std::views::enumerate(songs))
                MakeSongEntry(song, i);
        }
        CLAY(controlLayout) {
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_GROW(0) },
                               .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                                   .y = CLAY_ALIGN_Y_CENTER } } }) {
                TimeFormat(state.currTime, playbackTime);
                CLAY_TEXT(ToClayString(playbackTime), &bodyText);
            }
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.65f),
                                           .height = CLAY_SIZING_GROW(0) },
                               .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                                   .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.95f),
                                               .height = CLAY_SIZING_FIXED(25) } },
                       .backgroundColor = { 0, 0, 0, 255 }, }) {
                    float playbackTimePercent = state.currTime / state.currSong->length;
                    CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(playbackTimePercent),
                                       .height = CLAY_SIZING_GROW(0) } },
                           .backgroundColor = { 255, 255, 255, 255 }, }) {}
                }
            }
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_GROW(0) },
                               .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                                   .y = CLAY_ALIGN_Y_CENTER } } }) {
                TimeFormat(state.currSong->length, trackLength);
                CLAY_TEXT(ToClayString(trackLength), &bodyText);
            }
        }
    }
    return Clay_EndLayout();
}

void ClayError(Clay_ErrorData errorData) {
    printf("%s\n", errorData.errorText.chars);
}

void ChangeSong(PlaybackState& state, Song& song) {
    if (state.currSong)
        StopMusicStream(state.currSong->buffer);
    state.currSong = &song;
    state.currTime = 0.0f;
    PlayMusicStream(state.currSong->buffer);
}

int main() {
    const uint64_t clayArenaSize = Clay_MinMemorySize();
    Clay_Arena clayArena = Clay_CreateArenaWithCapacityAndMemory(clayArenaSize, malloc(clayArenaSize));
    Clay_Dimensions clayDim{
        .width = static_cast<float>(GetScreenWidth()),
        .height = static_cast<float>(GetScreenHeight())
    };
    Clay_ErrorHandler clayErr{
        .errorHandlerFunction = ClayError,
        .userData = nullptr
    };
    Clay_Initialize(clayArena, clayDim, clayErr);

    // SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(1024, 768, "[Riff Man]");
    InitAudioDevice();

    std::vector<Song> songs(3);
    songs[0].buffer = LoadMusicStream("white-wind.mp3");
    songs[0].length = GetMusicTimeLength(songs[0].buffer);
    songs[0].name = "Foreground Eclipse - White Wind";
    songs[1].buffer = LoadMusicStream("riff-man.mp3");
    songs[1].length = GetMusicTimeLength(songs[1].buffer);
    songs[1].name = "Zazen Boys - Riff Man";
    songs[2].buffer = LoadMusicStream("kali-ma.mp3");
    songs[2].length = GetMusicTimeLength(songs[2].buffer);
    songs[2].name = "Cult of Fire - Kali Ma";

    PlaybackState state;
    ChangeSong(state, songs[0]);

    InputState input;
    input.hoveredSong = nullptr;

    SetTargetFPS(60);

    Font fonts[1];
    fonts[0] = LoadFontEx("resources/Roboto-Regular.ttf", 48, 0, 400);
    SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);

    Clay_SetMeasureTextFunction(MeasureText, fonts);

    while (!WindowShouldClose()) {
        UpdateMusicStream(state.currSong->buffer);
        state.currTime = GetMusicTimePlayed(state.currSong->buffer);

        if (IsKeyPressed(KEY_D)) {
            debugEnabled = !debugEnabled;
            Clay_SetDebugModeEnabled(debugEnabled);
        }

        Clay_Vector2 mousePosition = RaylibToClayVector2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0) && !scrollbar.mouseDown);
        Clay_SetLayoutDimensions(Clay_Dimensions{
                static_cast<float>(GetScreenWidth()),
                static_cast<float>(GetScreenHeight()) });

        if (!IsMouseButtonDown(0))
            scrollbar.mouseDown = false;

        if (input.hoveredSong && IsMouseButtonDown(0))
            ChangeSong(state, *input.hoveredSong);

        if (IsMouseButtonDown(0) && !scrollbar.mouseDown && Clay_PointerOver(CLAY_ID("ScrollBar"))) {
            Clay_ScrollContainerData data = Clay_GetScrollContainerData(CLAY_ID("MainContent"));
            scrollbar.clickOrigin = mousePosition;
            scrollbar.positionOrigin = *data.scrollPosition;
            scrollbar.mouseDown = true;
        } else if (scrollbar.mouseDown) {
            Clay_ScrollContainerData data = Clay_GetScrollContainerData(CLAY_ID("MainContent"));
            if (data.contentDimensions.height > 0) {
                Clay_Vector2 ratio{
                    data.contentDimensions.width / data.scrollContainerDimensions.width,
                    data.contentDimensions.height / data.scrollContainerDimensions.height,
                };
                if (data.config.vertical) {
                    const float delta = scrollbar.clickOrigin.y - mousePosition.y;
                    data.scrollPosition->y = scrollbar.positionOrigin.y + delta * ratio.y;
                }
                if (data.config.horizontal) {
                    const float delta = scrollbar.clickOrigin.x - mousePosition.x;
                    data.scrollPosition->x = scrollbar.positionOrigin.x + delta * ratio.x;
                }
            }
        }

        Clay_Vector2 mouseWheelDelta = RaylibToClayVector2(GetMouseWheelMoveV());
        Clay_UpdateScrollContainers(true, mouseWheelDelta, GetFrameTime());

        Clay_RenderCommandArray renderCommands = TestLayout(state, input, songs);

        BeginDrawing();
        ClearBackground(BLACK);
        RenderFrame(renderCommands, fonts);
        EndDrawing();
    }

    UnloadMusicStream(songs[0].buffer);
    UnloadMusicStream(songs[1].buffer);
    UnloadMusicStream(songs[2].buffer);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
