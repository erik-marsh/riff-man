#include <raylib.h>
#define CLAY_IMPLEMENTATION
#include <clay.h>
#include <sqlite3.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <format>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Renderer.hpp"
#include "TextUtils.hpp"
#include "Casts.hpp"

// see https://schema.org/MusicRecording for some info
// also look up the multimedia section of "awesome-falsehood"
enum class AudioFormat {
    MP3,
    OPUS
};

struct SongEntry {
    long id;
    std::string filename;
    AudioFormat fileFormat;
    std::string_view uiName;
    std::string_view uiByArtist;
};

struct CollectionEntry {
    long id;
    std::string_view uiName;
};

struct PlaybackState {
    Music audioBuffer;
    const SongEntry* metadata;
    float duration;
    float currTime;
};

struct StringBuffer {
    char* buffer;
    size_t capacity;
    size_t size;
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


// designed to hold a maximum of hhh:mm:ss (9 chars)
// memory is alloc'd in main()
StringBuffer playbackTime;
StringBuffer trackLength;

// TODO: does not cleanly go into Casts.hpp because of my StringBuffer struct
namespace casts::clay {
constexpr Clay_String String(const StringBuffer& str) {
    return {
        .length = static_cast<int32_t>(str.size),
        .chars = str.buffer };
}
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
    if (hh == 0)
        res = std::format_to_n(dest.buffer, dest.capacity - 1, "{}:{:02}", mm, ss);
    else
        res = std::format_to_n(dest.buffer, dest.capacity - 1, "{}:{}:{:02}", hh, mm, ss);
    *res.out = '\0';
    dest.size = res.size;
}

struct LayoutInfo {
    Clay_RenderCommandArray renderCommands;
    long hoveredSongId;
    long hoveredCollectionId;
};

LayoutInfo MakeLayout(const PlaybackState& state,
                      const std::vector<CollectionEntry>& collections,
                      const std::vector<SongEntry>& songs,
                      const UIStringPool& pool) {
    // TODO: check if things need to be static constexpr
    constexpr Clay_Color white     { 255, 255, 255, 255 };
    constexpr Clay_Color black     { 0, 0, 0, 255 };
    constexpr Clay_Color lightgray { 100, 100, 100, 255 };
    constexpr Clay_Color darkgray  { 50, 50, 50, 255 };
    constexpr Clay_Color darkergray{ 35, 35, 35, 255 };

    constexpr Clay_ChildAlignment centered{ .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER };
    constexpr Clay_CornerRadius rounding{ 10, 10, 10, 10 };
    constexpr Clay_Sizing growAll{ .width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_GROW() };

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
        .backgroundColor = black
    };
    constexpr Clay_ElementDeclaration collectionView {
        .layout = {
            .sizing = { .width = CLAY_SIZING_PERCENT(0.20), .height = CLAY_SIZING_GROW() },
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16,
            .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = darkgray,
        .scroll = { .vertical = true }
    };
    constexpr Clay_ElementDeclaration songView {
        .layout = {
            .sizing = growAll,
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16,
            .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = darkgray,
        .scroll = { .vertical = true }
    };
    constexpr Clay_ElementDeclaration nowPlaying{
        .layout = {
            .sizing = growAll,
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16 },
        .backgroundColor = darkergray
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
    static constexpr auto MakeImageConfig = [](const Texture& tex) {
        return Clay_ElementDeclaration{
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
    static constexpr auto MakeButton = [](Clay_ElementId id) {
        return Clay_ElementDeclaration{
            .id = id,
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT() },
                .padding = CLAY_PADDING_ALL(16),
                .childAlignment = centered },
            .backgroundColor = lightgray,
            .cornerRadius = rounding
        };
    };

    LayoutInfo ret;
    ret.hoveredSongId = -1;
    ret.hoveredCollectionId = -1;

    static auto MakeCollectionEntry = [&ret, &pool](int collectionIndex, const CollectionEntry& collection) {
        const Texture& tex = pool.GetTexture(collection.uiName);
        CLAY(MakeButton(CLAY_IDI("Collection", collectionIndex))) {
            CLAY(MakeImageConfig(tex)) {}
            if (Clay_Hovered())
                ret.hoveredCollectionId = collection.id;
        }
    };

    static auto MakeSongEntry = [&ret, &pool](int songIndex, const SongEntry& song) {
        const Texture& tex = pool.GetTexture(song.uiName);
        CLAY(MakeButton(CLAY_IDI("Song", songIndex))) {
            CLAY(MakeImageConfig(tex)) {}
            if (Clay_Hovered())
                ret.hoveredSongId = song.id;
        }
    };

    static constexpr auto MakeProgressBar = [](float currTime, float duration) {
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

        CLAY({ .layout = { .sizing = fullBar }, .backgroundColor = black }) {
            CLAY({ .layout = { .sizing = partialBar }, .backgroundColor = white }) {}
        }
    };

    Clay_BeginLayout();

    CLAY(root) {
        CLAY(navigation) {
            CLAY(collectionView) {
                for (auto [i, c] : std::views::enumerate(collections))
                    MakeCollectionEntry(i, c);
            }
            CLAY(songView) {
                for (auto [i, s] : std::views::enumerate(songs))
                  MakeSongEntry(i, s);
            }
        }
        CLAY(nowPlaying) {
            CLAY({ .layout = { .sizing = growAll, .childAlignment = centered, .layoutDirection = CLAY_TOP_TO_BOTTOM }}) {
                CLAY_TEXT(CLAY_STRING("title"), CLAY_TEXT_CONFIG({}));
                CLAY_TEXT(CLAY_STRING("artist"), CLAY_TEXT_CONFIG({}));
                CLAY_TEXT(CLAY_STRING("album"), CLAY_TEXT_CONFIG({}));
            }
            CLAY(timeContainer) {
                TimeFormat(state.currTime, playbackTime);
                CLAY_TEXT(casts::clay::String(playbackTime), CLAY_TEXT_CONFIG({}));
            }
            CLAY(progressBar) {
                MakeProgressBar(state.currTime, state.duration);
            }
            CLAY(timeContainer) {
                TimeFormat(state.duration, trackLength);
                CLAY_TEXT(casts::clay::String(trackLength), CLAY_TEXT_CONFIG({}));
            }
        }
    }

    ret.renderCommands = Clay_EndLayout();
    return ret;
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

void LogSQLiteCallback(void*, int errCode, const char* msg) {
    std::string log = std::format("{}: {}\n", sqlite3_errstr(errCode), msg);
    printf(log.c_str());
}

int DeferredReleaser_counter = 0;

template <typename Lambda>
class DeferredReleaser {
 public:
    DeferredReleaser(Lambda lambda) : m_lambda(lambda) { DeferredReleaser_counter++; }
    ~DeferredReleaser() { m_lambda(); DeferredReleaser_counter--; std::printf("Releaser %d executed.\n", DeferredReleaser_counter); }
 private:
    Lambda m_lambda;
};

template <typename Lambda>
DeferredReleaser<Lambda> Defer(Lambda lambda) {
    return DeferredReleaser(lambda);
}

#define EXIT_ON_FT_ERR(err) if (err) { FTPrintError(err); return 1; }

int main() {
    // Init Raylib
    // SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(960, 540, "[Riff Man]");
    SetTargetFPS(60);
    InitAudioDevice();
    const auto raylibReleaser = Defer([](){
        std::printf("Deferred release for raylib...\n");
        CloseAudioDevice();
        CloseWindow();
    });

    // Init Clay
    const uint64_t clayArenaSize = Clay_MinMemorySize();
    Clay_Arena clayArena = Clay_CreateArenaWithCapacityAndMemory(clayArenaSize, malloc(clayArenaSize));
    Clay_ErrorHandler clayErr{
        .errorHandlerFunction = ClayError,
        .userData = nullptr
    };
    Clay_Initialize(clayArena, GetScreenDimensions(), clayErr);

    // Init sqlite
    sqlite3_config(SQLITE_CONFIG_LOG, LogSQLiteCallback, nullptr);
 
    constexpr int dbFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    sqlite3* db = nullptr;
    int err = sqlite3_open_v2("riff-man.db", &db, dbFlags, nullptr);
    if (err != SQLITE_OK) return 1;

    const auto sqliteReleaser([&db](){
        std::printf("Deferred release for sqlite...\n");
        sqlite3_close(db);
    });

    err = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS songs(filename TEXT, fileFormat TEXT, name TEXT, byArtist TEXT);", nullptr, nullptr, nullptr);
    if (err != SQLITE_OK) return 1;

    err = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS collections(name TEXT);", nullptr, nullptr, nullptr);
    if (err != SQLITE_OK) return 1;

    err = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS collections_contents(collectionId INTEGER, songId INTEGER);", nullptr, nullptr, nullptr);
    if (err != SQLITE_OK) return 1;

    // Init FreeType
    FT_Library ft;
    err = FT_Init_FreeType(&ft);
    EXIT_ON_FT_ERR(err);

    const auto freetypeReleaser = Defer([&ft](){
        std::printf("Deferred release for FreeType...\n");
        FT_Done_FreeType(ft);
    });

    // Init text rendering utilities
    TextRenderContext textCtx;
    err = FT_New_Face(ft, "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0, &textCtx.face);
    EXIT_ON_FT_ERR(err);

    const auto faceReleaser = Defer([&textCtx](){
        std::printf("Deferred release for FreeType face...\n");
        FT_Done_Face(textCtx.face);
    });

    // TODO: this should not be hardcoded
    err = FT_Set_Pixel_Sizes(textCtx.face, 20, 20);
    EXIT_ON_FT_ERR(err);

    textCtx.atlas.LoadGlyphs(textCtx.face);
    textCtx.rq = raqm_create();
    const auto raqmReleaser = Defer([&textCtx](){
        std::printf("Deferred release for raqm...\n");
        raqm_destroy(textCtx.rq);
    });

    playbackTime.buffer = new char[10];
    playbackTime.capacity = 10;
    playbackTime.size = 0;

    trackLength.buffer = new char[10];
    trackLength.capacity = 10;
    trackLength.size = 0;
    
    const auto stringBufferReleaser = Defer([](){
        std::printf("Deferred releaser for string buffers...\n");
        delete[] playbackTime.buffer;
        delete[] trackLength.buffer;
    });

    static constexpr auto ToString = [](const unsigned char* str) {
        return std::string(reinterpret_cast<const char*>(str));
    };

    static constexpr auto PrepareQuery = [](sqlite3* db, sqlite3_stmt** stmt, std::string_view query) {
        return sqlite3_prepare_v2(db, query.data(), query.size() + 1, stmt, nullptr);
    }; 

    UIStringPool pool;

    sqlite3_stmt* stmt;
    err = PrepareQuery(db, &stmt, "SELECT count(*) FROM collections;");
    if (err != SQLITE_OK)
        return 1;

    err = sqlite3_step(stmt);
    if (err != SQLITE_ROW)
        return 1;

    const long collCount = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    
    // TODO: needs to be a map: id -> entry
    std::vector<CollectionEntry> collections(collCount);
    std::unordered_map<long int, size_t> collectionsById;
    err = PrepareQuery(db, &stmt, "SELECT rowid, * FROM collections;");
    for (int i = 0; i < collCount; i++) {
        err = sqlite3_step(stmt);
        if (err != SQLITE_ROW) return 1;

        long int id = sqlite3_column_int64(stmt, 0);
        collections[i].id = id;
        collections[i].uiName = pool.Register(sqlite3_column_text(stmt, 1), textCtx);
        collectionsById.emplace(id, i);

    }
    sqlite3_finalize(stmt);

    std::unordered_map<long int, std::vector<SongEntry>> songsByCollection;
    std::unordered_map<long int, std::pair<long int, size_t>> songsById;
    for (const auto& [collId, collIndex] : collections) {
        const std::string sizeQuery = std::format("SELECT count(*) FROM collections_contents INNER JOIN songs ON collections_contents.songId = songs.rowid WHERE collections_contents.collectionId = {};", collId);
        const std::string contentQuery = std::format("SELECT songs.rowid, songs.* FROM collections_contents INNER JOIN songs ON collections_contents.songId = songs.rowid WHERE collections_contents.collectionId = {};", collId);

        err = PrepareQuery(db, &stmt, sizeQuery);
        if (err != SQLITE_OK) return 1;

        err = sqlite3_step(stmt);
        if (err != SQLITE_ROW) return 1;

        const long songsCount = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);

        songsByCollection.emplace(collId, std::vector<SongEntry>(songsCount));

        err = PrepareQuery(db, &stmt, contentQuery);
        if (err != SQLITE_OK) return 1;

        // idk if you need to, but do in fact remember anyway that SQLite tables are 1-indexed
        for (int j = 0; j < songsCount; j++) {
            if (sqlite3_step(stmt) != SQLITE_ROW) return 1;

            long int songId = sqlite3_column_int64(stmt, 0);
            songsByCollection[collId][j].id = songId;
            songsByCollection[collId][j].filename = ToString(sqlite3_column_text(stmt, 1));
            songsByCollection[collId][j].fileFormat = AudioFormat::MP3;  // TODO: bad
            songsByCollection[collId][j].uiName = pool.Register(sqlite3_column_text(stmt, 3), textCtx);
            songsByCollection[collId][j].uiByArtist = pool.Register(sqlite3_column_text(stmt, 4), textCtx);

            songsById.emplace(songId, std::make_pair(collId, j));
        }
        sqlite3_finalize(stmt);
    }
    
    PlaybackState state{
        .audioBuffer = {},
        .metadata = nullptr,
        .duration = 0.0f,
        .currTime = 0.0f
    };

    LayoutInfo layoutInfo;
    layoutInfo.hoveredSongId = -1;
    layoutInfo.hoveredCollectionId = -1;

    Clay_SetMeasureTextFunction(MeasureText, &textCtx);

    const std::vector<SongEntry> emptySongList;
    long int selectedCollection = -1;
    bool clayDebugEnabled = false;
    while (!WindowShouldClose()) {
        // Phase 1: input state updates
        if (IsKeyPressed(KEY_D)) {
            clayDebugEnabled = !clayDebugEnabled;
            Clay_SetDebugModeEnabled(clayDebugEnabled);
        }

        const auto mousePosition = casts::clay::Vector2(GetMousePosition());
        const auto mouseWheelDelta = casts::clay::Vector2(GetMouseWheelMoveV());

        // the example code is wrong
        // this function only cares about the up/down state,
        // not the release/press state
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0));

        // Parameter 1: enableDragScrolling
        // When this is enabled, it will probably cause weird issues
        // where songs are selected by releasing the touch scroll.
        Clay_UpdateScrollContainers(false, mouseWheelDelta, GetFrameTime());
        
        // Phase 2: application state updates
        if (layoutInfo.hoveredCollectionId > -1 && IsMouseButtonReleased(0))
            selectedCollection = layoutInfo.hoveredCollectionId;

        if (layoutInfo.hoveredSongId > -1 && IsMouseButtonReleased(0)) {
            const auto [collId, index] = songsById[layoutInfo.hoveredSongId];
            LoadSong(state, songsByCollection[collId][index]);
        }

        if (IsMusicValid(state.audioBuffer)) {
            UpdateMusicStream(state.audioBuffer);
            state.currTime = GetMusicTimePlayed(state.audioBuffer);
        }

        // Phase 3: layout
        // MakeLayout will implicitly update input state.
        // We consider this to be part of next frame's phase 1.
        Clay_SetLayoutDimensions(GetScreenDimensions());
        const std::vector<SongEntry>& songList =
            selectedCollection != -1 ? songsByCollection[selectedCollection] : emptySongList;
        layoutInfo = MakeLayout(state, collections, songList, pool);

        // Phase 4: render
        BeginDrawing();
        ClearBackground(BLACK);
        RenderFrame(layoutInfo.renderCommands, textCtx);
        EndDrawing();
    }

    return 0;
}
