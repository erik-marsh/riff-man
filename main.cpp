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

// parallel to what will be the database entry for a song
// basing this schema partially off of https://schema.org/MusicRecording
// TODO: i have no idea if we want this to be an owning struct or a reference struct
//       i will assume owning until further notice
enum class AudioFormat {
    MP3,
    OPUS
};

struct SongEntry {
    // data for program logic
    long id;
    std::string filename;
    AudioFormat fileFormat;
    
    // file metadata
    std::string name;
    std::string byArtist;
    // std::string inAlbum;
    // float duration;  // in seconds
};

struct PlaybackState {
    Music audioBuffer;
    const SongEntry* metadata;
    float duration;
    float currTime;
};

struct HoverInput {
    long songId;
};

struct StringBuffer {
    char* buffer;
    size_t capacity;
    size_t size;
};

// designed to hold a maximum of hhh:mm:ss (9 chars)
// memory is alloc'd in main()
StringBuffer playbackTime;
StringBuffer trackLength;

namespace colors {
    // fun color pallete i generated from coolers.co
    // i mostly like the black, white, and the blue as a highlight color
    // the red and green i can really give or take
    constexpr Clay_Color black    { 0x0D, 0x1B, 0x1E, 0xFF };
    constexpr Clay_Color gray     { 0x45, 0x55, 0x55, 0xFF };
    constexpr Clay_Color lightgray{ 0x80, 0x80, 0x80, 0xFF };
    constexpr Clay_Color blue     { 0x05, 0x8E, 0xD9, 0xFF };
    constexpr Clay_Color white    { 0xCD, 0xDD, 0xDD, 0xFF };
    // constexpr Color red  { 0xF2, 0x54, 0x2D, 0xFF };
    // constexpr Color green{ 0x58, 0x81, 0x57, 0xFF };
};

constexpr Clay_Vector2 RaylibToClayVector2(Vector2 vector) {
    return Clay_Vector2{ .x = vector.x, .y = vector.y };
}

constexpr Clay_String ToClayString(const std::string& str) {
    return { .length = static_cast<int32_t>(str.size()), .chars = str.data() };
}

constexpr Clay_String ToClayString(std::string_view str) {
    return { .length = static_cast<int32_t>(str.size()), .chars = str.data() };
}

constexpr Clay_String ToClayString(const StringBuffer& str) {
    return { .length = static_cast<int32_t>(str.size), .chars = str.buffer };
}

// If we declare our layout in a function,
// the variables declared inside are not visible to the renderer.
// I just decided to use global buffers for any necessary string conversions.
void TimeFormat(float seconds, StringBuffer& dest) {
    const int asInt = static_cast<int>(seconds);
    const int ss = asInt % 60;
    const int mm = asInt / 60;
    const int hh = mm / 60;

    std::format_to_n_result<char*> res;
    if (hh == 0) [[likely]]
        res = std::format_to_n(dest.buffer, dest.capacity - 1, "{}:{:02}", mm, ss);
    else [[unlikely]]
        res = std::format_to_n(dest.buffer, dest.capacity - 1, "{}:{}:{:02}", hh, mm, ss);
    *res.out = '\0';
    dest.size = res.size;
}

Clay_RenderCommandArray MakeLayout(PlaybackState& state, HoverInput& hovers, const std::vector<SongEntry>& songs) {
    static constexpr Clay_ElementDeclaration songEntryBase{
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIXED(75) },
            .padding = CLAY_PADDING_ALL(16),
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                .y = CLAY_ALIGN_Y_CENTER } },
        .backgroundColor = { 100, 100, 100, 255 },
        .cornerRadius = { 10, 10, 10, 10}
    };

    // Clay internally caches font configs, so this isn't quite the best approach
    static Clay_TextElementConfig bodyText{
        .textColor = { 255, 255, 255, 255 },
        .fontSize = 24
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

    static constexpr Clay_ElementDeclaration timeLayout{
       .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                               .height = CLAY_SIZING_GROW(0) },
                   .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                       .y = CLAY_ALIGN_Y_CENTER } }
    };

    static auto MakeSongEntry = [&hovers](const SongEntry& song, int songIndex) {
        Clay_ElementDeclaration config = songEntryBase;
        config.id = CLAY_IDI("Song", songIndex);
        CLAY(config) {
            CLAY_TEXT(ToClayString(song.name), &bodyText);
            if (Clay_Hovered())
                hovers.songId = song.id;
        }
    };

    // hovers.song = nullptr;
    hovers.songId = -1;
    Clay_BeginLayout();
    // TODO: eventually I will want a small gap between the two panels
    CLAY(rootLayout) {
        CLAY(navigationLayout) {
            for (auto [i, song] : std::views::enumerate(songs))
                MakeSongEntry(song, i);
        }
        CLAY(controlLayout) {
            CLAY(timeLayout) {
                TimeFormat(state.currTime, playbackTime);
                CLAY_TEXT(ToClayString(playbackTime), &bodyText);
            }
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.65f), .height = CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.95f), .height = CLAY_SIZING_FIXED(25) } }, .backgroundColor = { 0, 0, 0, 255 }, }) {
                    const float currTime = state.currTime > state.duration ? state.duration : state.currTime;
                    const float progress = state.duration > 0.0f ? currTime / state.duration : 0.0f;
                    CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(progress), .height = CLAY_SIZING_GROW(0) } }, .backgroundColor = { 255, 255, 255 ,255 } }) {}
                }
            }
            CLAY(timeLayout) {
                TimeFormat(state.duration, trackLength);
                CLAY_TEXT(ToClayString(trackLength), &bodyText);
            }
        }
    }
    return Clay_EndLayout();
}

void ClayError(Clay_ErrorData errorData) {
    printf("%s\n", errorData.errorText.chars);
}

void LoadSong(PlaybackState& state, const SongEntry& song) {
    if (IsMusicValid(state.audioBuffer)) {
        StopMusicStream(state.audioBuffer);
        UnloadMusicStream(state.audioBuffer);
    }

    state.audioBuffer = LoadMusicStream(song.filename.c_str());
    state.metadata = &song;
    state.duration = GetMusicTimeLength(state.audioBuffer);
    state.currTime = 0.0f;

    PlayMusicStream(state.audioBuffer);
}

Clay_Dimensions GetScreenDimensions() {
    return {
        .width = static_cast<float>(GetScreenWidth()),
        .height = static_cast<float>(GetScreenHeight())
    };
}

int main() {
    // SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(960, 540, "[Riff Man]");
    SetTargetFPS(60);
    InitAudioDevice();

    const uint64_t clayArenaSize = Clay_MinMemorySize();
    Clay_Arena clayArena = Clay_CreateArenaWithCapacityAndMemory(clayArenaSize, malloc(clayArenaSize));
    Clay_ErrorHandler clayErr{
        .errorHandlerFunction = ClayError,
        .userData = nullptr
    };
    Clay_Initialize(clayArena, GetScreenDimensions(), clayErr);

    bool debugEnabled = false;

    playbackTime.buffer = new char[10];
    playbackTime.capacity = 10;
    playbackTime.size = 0;

    trackLength.buffer = new char[10];
    trackLength.capacity = 10;
    trackLength.size = 0;

    std::vector<SongEntry> songs(3);
    songs[0].id = 0;
    songs[0].filename = "white-wind.mp3";
    songs[0].fileFormat = AudioFormat::MP3;
    songs[0].name = "White Wind";
    songs[0].byArtist = "Foreground Eclipse";

    songs[1].id = 1;
    songs[1].filename = "riff-man.mp3";
    songs[1].fileFormat = AudioFormat::MP3;
    songs[1].name = "Riff Man";
    songs[1].byArtist = "Zazen Boys";

    songs[2].id = 2;
    songs[2].filename = "kali-ma.mp3";
    songs[2].fileFormat = AudioFormat::MP3;
    songs[2].name = "Kali Ma";
    songs[2].byArtist = "Cult of Fire";

    PlaybackState state;
    state.metadata = nullptr;
    state.currTime = 0.0f;
    state.duration = 0.0f;

    HoverInput hovers;
    hovers.songId = -1;

    Font fonts[1];
    fonts[0] = LoadFontEx("resources/Roboto-Regular.ttf", 48, 0, 400);
    SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);

    Clay_SetMeasureTextFunction(MeasureText, fonts);

    while (!WindowShouldClose()) {
        // Phase 1: input state updates
        if (IsKeyPressed(KEY_D)) {
            debugEnabled = !debugEnabled;
            Clay_SetDebugModeEnabled(debugEnabled);
        }

        const Clay_Vector2 mousePosition = RaylibToClayVector2(GetMousePosition());
        const Clay_Vector2 mouseWheelDelta = RaylibToClayVector2(GetMouseWheelMoveV());
        // NOTE: the example code is wrong
        //       this function only cares about the up/down state, not the release/press state
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0));
        // NOTE: parameter 1: enableDragScrolling
        Clay_UpdateScrollContainers(true, mouseWheelDelta, GetFrameTime());
        
        // Phase 2: application state updates
        // TODO: try not to conflate scrolling actions with selection actions
        if (hovers.songId > -1 && IsMouseButtonReleased(0))
            LoadSong(state, songs[hovers.songId]);

        if (IsMusicValid(state.audioBuffer)) {
            UpdateMusicStream(state.audioBuffer);
            state.currTime = GetMusicTimePlayed(state.audioBuffer);
        }

        // Phase 3: layout
        Clay_SetLayoutDimensions(GetScreenDimensions());
        // note how the input state update that happens here
        // is implicitly part of phase 1 of the next loop
        Clay_RenderCommandArray renderCommands = MakeLayout(state, hovers, songs);

        // Phase 4: render
        BeginDrawing();
        ClearBackground(BLACK);
        RenderFrame(renderCommands, fonts);
        EndDrawing();
    }

    delete[] playbackTime.buffer;
    delete[] trackLength.buffer;

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
