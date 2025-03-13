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

// Custom raylib functions for string view text buffers 
// Raysan has explicitly stated he will not be supporting these.
// While I can understand that from an API perspective,
// you're not gonna believe how stupidly easy these are to implement...
// 
// In order to create functions that accept a string length (in bytes),
// we need to provide new implementations of DrawText, DrawTextEx, and DrawTextPro.
// Luckily, DrawText and DrawTextPro just call DrawTextEx internally.
// 
// The main offender is a function called TextLength.
// This is LITERALLY just std::strlen.
// This function is a problem because it is called internally by DrawTextEx,
// meaning we have to re-write the WHOLE FAMILY of functions just to support
// passing in a buffer size.
// That is literally the only change we need to make.
//
// Literally. The. Only. One.
//
// Our Clay renderer only needs to use DrawTextEx, fortunately.

// TODO: I don't think this implementation matches the one in DrawTextEx{N}
Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    Clay_Dimensions textSize{ .width = 0.0f, .height = 0.0f };

    Font* fonts = reinterpret_cast<Font*>(userData);
    Font fontToUse = fonts[config->fontId];

    // Font failed to load, likely the fonts are in the wrong place relative to the execution dir
    if (!fontToUse.glyphs)
        return textSize;

    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;

    for (int i = 0; i < text.length; ++i) {
        if (text.chars[i] == '\n') {
            maxTextWidth = std::fmax(maxTextWidth, lineTextWidth);
            lineTextWidth = 0;
            continue;
        }

        const int index = text.chars[i] - 32;
        const auto& glyph = fontToUse.glyphs[index];
        if (glyph.advanceX != 0)
            lineTextWidth += glyph.advanceX;
        else
            lineTextWidth += fontToUse.recs[index].width + glyph.offsetX;
    }

    const float scaleFactor = config->fontSize / static_cast<float>(fontToUse.baseSize);
    maxTextWidth = std::fmax(maxTextWidth, lineTextWidth);
    textSize.width = maxTextWidth * scaleFactor;
    textSize.height = config->fontSize;
    return textSize;
}

void DrawTextExN(Font font, const char* text, int textLen,
                 Vector2 position, float fontSize, float spacing, Color tint) {
    // standin for a global inside raylib
    // not sure how to get that value internally
    static int textLineSpacing = 2;

    if (font.texture.id == 0)
        font = GetFontDefault();

    float textOffsetX = 0.0f;
    float textOffsetY = 0.0f;
    const float scaleFactor = fontSize / font.baseSize;

    for (int i = 0; i < textLen; ) {
        int codepointByteCount = 0;
        const int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        const int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') {
            textOffsetX = 0.0f;
            textOffsetY += fontSize + textLineSpacing;
        } else {
            if ((codepoint != ' ') && (codepoint != '\t')) {
                Vector2 glyphPos{ position.x + textOffsetX, position.y + textOffsetY };
                DrawTextCodepoint(font, codepoint, glyphPos, fontSize, tint);
            }

            float baseOffset;
            if (font.glyphs[index].advanceX != 0)
                baseOffset = static_cast<float>(font.glyphs[index].advanceX);
            else
                baseOffset = font.recs[index].width;
            textOffsetX += baseOffset * scaleFactor + spacing;
        }

        i += codepointByteCount;
    }
}

constexpr Color ClayToRaylibColor(const Clay_Color& color) {
    static constexpr auto Transform = [](float value) -> unsigned char {
        if (value > 255.0f) return static_cast<unsigned char>(255);
        if (value < 0.0f) return static_cast<unsigned char>(0);
        return static_cast<unsigned char>(std::roundf(value));
    };

    return Color{
        .r = Transform(color.r),
        .g = Transform(color.g),
        .b = Transform(color.b),
        .a = Transform(color.a)
    };
};

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

ScrollbarData scrollbar = {0};

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

// Mostly just copied from the example
void Clay_Raylib_Render(Clay_RenderCommandArray renderCommands, Font* fonts) {
    for (int j = 0; j < renderCommands.length; j++) {
        const Clay_RenderCommand* const cmd = Clay_RenderCommandArray_Get(&renderCommands, j);
        const Clay_BoundingBox& bb = cmd->boundingBox;

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData& text = cmd->renderData.text;
                DrawTextExN(fonts[text.fontId],
                            text.stringContents.chars,
                            text.stringContents.length,
                            Vector2{bb.x, bb.y},
                            static_cast<float>(text.fontSize),
                            static_cast<float>(text.letterSpacing),
                            ClayToRaylibColor(text.textColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const auto& image = cmd->renderData.image;
                const Vector2 origin{ bb.x, bb.y };
                const Texture2D imageTexture = *reinterpret_cast<Texture2D*>(image.imageData);
                const float scale = bb.width / static_cast<float>(imageTexture.width);
                Color tint = ClayToRaylibColor(image.backgroundColor);
                if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0)
                    tint = Color{ 255, 255, 255, 255 };
                DrawTextureEx(imageTexture, origin, 0, scale, tint);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                // omitting the rounding does in fact cause issues here
                const int bbx = static_cast<int>(std::roundf(bb.x));
                const int bby = static_cast<int>(std::roundf(bb.y));
                const int bbw = static_cast<int>(std::roundf(bb.width));
                const int bbh = static_cast<int>(std::roundf(bb.height));
                BeginScissorMode(bbx, bby, bbw, bbh); 
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                EndScissorMode();
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                const auto& config = cmd->renderData.rectangle;
                const Rectangle rect{ bb.x, bb.y, bb.width, bb.height };
                const Color color = ClayToRaylibColor(config.backgroundColor);

                if (config.cornerRadius.topLeft > 0) {
                    const float minDim = bb.width > bb.height ? bb.height : bb.width;
                    const float radius = (config.cornerRadius.topLeft * 2) / minDim;
                    DrawRectangleRounded(rect, radius, 10, color);
                } else {
                    DrawRectangleRec(rect, color);
                }
                break;
            }
            // NOTE: borders are drawn IN the main rect, not outside of it
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const auto& border = cmd->renderData.border;
                const Color color = ClayToRaylibColor(border.color);

                if (border.width.left > 0) {
                    const Rectangle rect{
                        bb.x,
                        bb.y + border.cornerRadius.topLeft,
                        static_cast<float>(border.width.left),
                        bb.height - border.cornerRadius.topLeft - border.cornerRadius.bottomLeft };
                    DrawRectangleRec(rect, color);
                }
                if (border.width.right > 0) {
                    const Rectangle rect{
                        bb.x + bb.width - border.width.right,
                        bb.y + border.cornerRadius.topRight,
                        static_cast<float>(border.width.right),
                        bb.height - border.cornerRadius.topRight - border.cornerRadius.bottomRight };
                    DrawRectangleRec(rect, color);
                }
                if (border.width.top > 0) {
                    const Rectangle rect{
                        bb.x + border.cornerRadius.topLeft,
                        bb.y,
                        bb.width - border.cornerRadius.topLeft - border.cornerRadius.topRight,
                        static_cast<float>(border.width.top) };
                    DrawRectangleRec(rect, color);
                }
                if (border.width.bottom > 0) {
                    const Rectangle rect{
                        bb.x + border.cornerRadius.bottomLeft,
                        bb.y + bb.height - border.width.bottom,
                        bb.width - border.cornerRadius.bottomLeft - border.cornerRadius.bottomRight,
                        static_cast<float>(border.width.bottom) };
                    DrawRectangleRec(rect, color);
                }
                if (border.cornerRadius.topLeft > 0) {
                    const Vector2 center{
                        bb.x + border.cornerRadius.topLeft,
                        bb.y + border.cornerRadius.topLeft };
                    const float innerRadius = border.cornerRadius.topLeft - border.width.top;
                    const float outerRadius = border.cornerRadius.topLeft;
                    DrawRing(center, innerRadius, outerRadius, 180.0f, 270.0f, 10, color);
                }
                if (border.cornerRadius.topRight > 0) {
                    const Vector2 center{
                        bb.x + bb.width - border.cornerRadius.topRight,
                        bb.y + border.cornerRadius.topRight };
                    const float innerRadius = border.cornerRadius.topRight - border.width.top;
                    const float outerRadius = border.cornerRadius.topRight;
                    DrawRing(center, innerRadius, outerRadius, 270.0f, 360.0f, 10, color);
                }
                if (border.cornerRadius.bottomLeft > 0) {
                    const Vector2 center{
                        bb.x + border.cornerRadius.bottomLeft,
                        bb.y + bb.height - border.cornerRadius.bottomLeft };
                    const float innerRadius = border.cornerRadius.bottomLeft - border.width.top;
                    const float outerRadius = border.cornerRadius.bottomLeft;
                    DrawRing(center, innerRadius, outerRadius, 90.0f, 180.0f, 10, color);
                }
                if (border.cornerRadius.bottomRight > 0) {
                    const Vector2 center{
                        bb.x + bb.width - border.cornerRadius.bottomRight,
                        bb.y + bb.height - border.cornerRadius.bottomRight };
                    const float innerRadius = border.cornerRadius.bottomRight - border.width.bottom;
                    const float outerRadius = border.cornerRadius.bottomRight;
                    DrawRing(center, innerRadius, outerRadius, 0.1f, 90.0f, 10, color);
                }
                break;
            }
            default: {
                printf("Unhandled render command.\n");
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Main Function
////////////////////////////////////////////////////////////////////////////////

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

    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

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
        Clay_Raylib_Render(renderCommands, fonts);
        EndDrawing();
    }

    UnloadMusicStream(songs[0].buffer);
    UnloadMusicStream(songs[1].buffer);
    UnloadMusicStream(songs[2].buffer);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
