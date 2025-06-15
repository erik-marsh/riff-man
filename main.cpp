#include <raylib.h>
#define CLAY_IMPLEMENTATION
#include <clay.h>
#include <sqlite3.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Allocators.hpp"
#include "Casts.hpp"
#include "Data.hpp"
#include "Defer.hpp"
#include "Layout.hpp"
#include "Renderer.hpp"
#include "TextUtils.hpp"

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

void ClayError(Clay_ErrorData errorData) {
    std::printf("[CLAY ERROR] %s\n", errorData.errorText.chars);
}

void LogSQLiteCallback(void*, int errCode, const char* msg) {
    std::printf("[SQLITE] %s: %s\n", sqlite3_errstr(errCode), msg);
}

std::string ToString(const unsigned char* str) {
    return std::string(reinterpret_cast<const char*>(str));
};

int PrepareQuery(sqlite3* db, sqlite3_stmt** stmt, std::string_view query) {
    return sqlite3_prepare_v2(db, query.data(), query.size() + 1, stmt, nullptr);
}; 

#define EXIT_ON_FT_ERR(err) if (err) { FTPrintError(err); return 1; }

Arena<CollectionEntry> collections;
Arena<SongEntry> collectionSongs;

Arena<SongEntry> queueSongs;

int main() {
    collections.Reserve(1024);
    collectionSongs.Reserve(512);
    queueSongs.Reserve(512);
    
    // Init Raylib
    // SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(960, 540, "[Riff Man]");
    SetTargetFPS(60);
    InitAudioDevice();
    const auto raylibReleaser = Defer([](){
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

    const auto sqliteReleaser([&db](){ sqlite3_close(db); });

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

    const auto freetypeReleaser = Defer([&ft](){ FT_Done_FreeType(ft); });

    // Init text rendering utilities
    TextRenderContext textCtx;
    err = FT_New_Face(ft, "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0, &textCtx.face);
    EXIT_ON_FT_ERR(err);

    const auto faceReleaser = Defer([&textCtx](){ FT_Done_Face(textCtx.face); });

    // TODO: this should not be hardcoded
    err = FT_Set_Pixel_Sizes(textCtx.face, 20, 20);
    EXIT_ON_FT_ERR(err);

    textCtx.atlas.LoadGlyphs(textCtx.face);
    textCtx.rq = raqm_create();
    const auto raqmReleaser = Defer([&textCtx](){ raqm_destroy(textCtx.rq); });

    Clay_SetMeasureTextFunction(MeasureText, &textCtx);

    UIStringPool pool;

    sqlite3_stmt* stmt;
    err = PrepareQuery(db, &stmt, "SELECT rowid, * FROM collections;");
    if (err != SQLITE_OK) return 1;

    while ((err = sqlite3_step(stmt)) == SQLITE_ROW) {
        int i = collections.Allocate();
        EntityId id = sqlite3_column_int64(stmt, 0);

        collections.arr[i].id = id;
        collections.arr[i].uiName = pool.Register(sqlite3_column_text(stmt, 1), textCtx);
    }

    sqlite3_finalize(stmt);

    PlaybackState state{
        .audioBuffer = {},
        .metadata = nullptr,
        .duration = 0.0f,
        .currTime = 0.0f
    };

    LayoutInput inputNm0{
        .songIndex = -1,
        .collectionIndex = -1
    };
    LayoutInput inputNm1{
        .songIndex = -1,
        .collectionIndex = -1
    };

    int selectedCollectionIndex = -1;

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
        // if (layoutInfo.hoveredSongId > -1 && IsMouseButtonReleased(0)) {
        //     const int i = songsById[layoutInfo.hoveredSongId];
        //     LoadSong(state, songArena.arr[i]);
        // }

        if (IsMouseButtonReleased(0) &&
                inputNm0.collectionIndex != -1 &&
                inputNm0.collectionIndex != selectedCollectionIndex) {
            selectedCollectionIndex = inputNm0.collectionIndex;
            collectionSongs.Reset();
            int collId = collections.arr[selectedCollectionIndex].id;

            err = PrepareQuery(db, &stmt, "SELECT songs.rowid, songs.* FROM collections_contents INNER JOIN songs ON collections_contents.songId = songs.rowid WHERE collections_contents.collectionId = ?;");
            if (err != SQLITE_OK) return 1;

            err = sqlite3_bind_int64(stmt, 1, collId);
            if (err != SQLITE_OK) return 1;

            while ((err = sqlite3_step(stmt)) == SQLITE_ROW) {
                int i = collectionSongs.Allocate();
                // note that SQLite tables are 1-indexed
                EntityId id = sqlite3_column_int64(stmt, 0);

                collectionSongs.arr[i].id = id;
                collectionSongs.arr[i].filename = ToString(sqlite3_column_text(stmt, 1));
                collectionSongs.arr[i].fileFormat = AudioFormat::MP3;  // TODO: bad
                collectionSongs.arr[i].uiName = pool.Register(sqlite3_column_text(stmt, 3), textCtx);
                collectionSongs.arr[i].uiByArtist = pool.Register(sqlite3_column_text(stmt, 4), textCtx);
            }

            sqlite3_finalize(stmt);
        }

        if (IsMusicValid(state.audioBuffer)) {
            UpdateMusicStream(state.audioBuffer);
            state.currTime = GetMusicTimePlayed(state.audioBuffer);
        }

        // Phase 3: layout
        // MakeLayout will implicitly update input state.
        // We consider this to be part of next frame's phase 1.
        Clay_SetLayoutDimensions(GetScreenDimensions());
        const LayoutResult layout = MakeLayout(state,
                                               collectionSongs.Span(),
                                               collections.Span(),
                                               pool);

        inputNm1 = inputNm0;
        inputNm0 = layout.input;

        // Phase 4: render
        BeginDrawing();
        ClearBackground(BLACK);
        RenderFrame(layout.renderCommands, textCtx);
        // DrawTextureEx(textCtx.atlas.RaylibTexture(), {0, 0}, 0, 1, WHITE);
        EndDrawing();
    }

    return 0;
}
