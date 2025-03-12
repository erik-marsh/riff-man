#include <raylib.h>
#define CLAY_IMPLEMENTATION
#include <clay.h>

#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

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

Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    // Measure string size for Font
    Clay_Dimensions textSize = { 0 };

    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;

    float textHeight = config->fontSize;
    Font* fonts = (Font*)userData;
    Font fontToUse = fonts[config->fontId];
    // Font failed to load, likely the fonts are in the wrong place relative to the execution dir
    if (!fontToUse.glyphs) return textSize;

    float scaleFactor = config->fontSize/static_cast<float>(fontToUse.baseSize);

    for (int i = 0; i < text.length; ++i)
    {
        if (text.chars[i] == '\n') {
            maxTextWidth = fmax(maxTextWidth, lineTextWidth);
            lineTextWidth = 0;
            continue;
        }
        int index = text.chars[i] - 32;
        if (fontToUse.glyphs[index].advanceX != 0) lineTextWidth += fontToUse.glyphs[index].advanceX;
        else lineTextWidth += (fontToUse.recs[index].width + fontToUse.glyphs[index].offsetX);
    }

    maxTextWidth = fmax(maxTextWidth, lineTextWidth);

    textSize.width = maxTextWidth * scaleFactor;
    textSize.height = textHeight;

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
    float scaleFactor = fontSize / font.baseSize;

    for (int i = 0; i < textLen; ) {
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') {
            textOffsetX = 0.0f;
            textOffsetY += (fontSize + textLineSpacing);
        } else {
            if ((codepoint != ' ') && (codepoint != '\t')) {
                Vector2 glyphPos{ position.x + textOffsetX, position.y + textOffsetY };
                DrawTextCodepoint(font, codepoint, glyphPos, fontSize, tint);
            }

            if (font.glyphs[index].advanceX == 0)
                textOffsetX += static_cast<float>(font.recs[index].width * scaleFactor + spacing);
            else
                textOffsetX += static_cast<float>(font.glyphs[index].advanceX * scaleFactor + spacing);
        }

        i += codepointByteCount;
    }
}

constexpr Color ClayToRaylibColor(Clay_Color& color) {
    return Color{
        .r = static_cast<unsigned char>(std::roundf(color.r)),
        .g = static_cast<unsigned char>(std::roundf(color.g)),
        .b = static_cast<unsigned char>(std::roundf(color.b)),
        .a = static_cast<unsigned char>(std::roundf(color.a))
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

// TODO: write into a static string buffer using a printf or format_to_n
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

ScrollbarData scrollbarData = {0};

bool debugEnabled = false;

struct PlaybackState {
    float currTime;
    float length;
};

Clay_RenderCommandArray TestLayout(PlaybackState& state) {
    static constexpr Clay_ElementDeclaration songEntry{
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIXED(75) },
            .padding = CLAY_PADDING_ALL(16),
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                .y = CLAY_ALIGN_Y_CENTER } },
        .backgroundColor = { 100, 100, 100, 255 },
    };

    // Clay internally caches font configs, so this isn't quite the best approach
    static Clay_TextElementConfig bodyText{
        .textColor = { 255, 255, 255, 255 },
        .fontSize = 24
    };

    using namespace std::literals::string_view_literals;
    static constexpr std::array titles = {
        "Foreground Eclipse - White Wind"sv,
        "Zazen Boys - Riff Man"sv,
        "Cult of Fire - Kali Ma"sv,
        "Foreground Eclipse - White Wind"sv,
        "Zazen Boys - Riff Man"sv,
        "Cult of Fire - Kali Ma"sv,
        "Foreground Eclipse - White Wind"sv,
        "Zazen Boys - Riff Man"sv,
        "Cult of Fire - Kali Ma"sv,
        "Foreground Eclipse - White Wind"sv,
        "Zazen Boys - Riff Man"sv,
        "Cult of Fire - Kali Ma"sv,
        "end of container"sv,
        // TODO: need CJK font
        // "銀杏BOYZ - あの娘に1ミリでもちょっかいかけたら殺す"
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

    Clay_BeginLayout();
    // TODO: eventually I will want a small gap between the two panels
    CLAY(rootLayout) {
        CLAY(navigationLayout) {
            for (auto str : titles) {
                Clay_String clayStr = ToClayString(str);
                CLAY(songEntry) { CLAY_TEXT(clayStr, &bodyText); }
            }
        }
        CLAY(controlLayout) {
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                   .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                       .y = CLAY_ALIGN_Y_CENTER } },
                   /*.backgroundColor = { 255, 0, 0, 255 }*/ }) {
                TimeFormat(state.currTime, playbackTime);
                CLAY_TEXT(ToClayString(playbackTime), &bodyText);
            }
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.65f), .height = CLAY_SIZING_GROW(0) },
                   .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                       .y = CLAY_ALIGN_Y_CENTER } },
                   /*.backgroundColor = { 0, 255, 0, 255 }*/ }) {
                CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(0.95f), .height = CLAY_SIZING_FIXED(25) } },
                       .backgroundColor = { 0, 0, 0, 255 }, }) {
                    float playbackTimePercent = state.currTime / state.length;
                    CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(playbackTimePercent), .height = CLAY_SIZING_GROW(0) } },
                           .backgroundColor = { 255, 255, 255, 255 }, }) {}
                }
            }
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                   .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                       .y = CLAY_ALIGN_Y_CENTER } },
                   /*.backgroundColor = { 0, 0, 255, 255 }*/ }) {
                TimeFormat(state.length, trackLength);
                CLAY_TEXT(ToClayString(trackLength), &bodyText);
            }
        }
    }
    return Clay_EndLayout();
}

// Mostly just copied from the example
void Clay_Raylib_Render(Clay_RenderCommandArray renderCommands, Font* fonts) {
    for (int j = 0; j < renderCommands.length; j++) {
        Clay_RenderCommand* renderCommand = Clay_RenderCommandArray_Get(&renderCommands, j);
        const Clay_BoundingBox& bb = renderCommand->boundingBox;
        switch (renderCommand->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData* textData = &renderCommand->renderData.text;
                DrawTextExN(fonts[textData->fontId],
                            textData->stringContents.chars,
                            textData->stringContents.length,
                            Vector2{bb.x, bb.y},
                            static_cast<float>(textData->fontSize),
                            static_cast<float>(textData->letterSpacing),
                            ClayToRaylibColor(textData->textColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                Texture2D imageTexture = *reinterpret_cast<Texture2D*>(renderCommand->renderData.image.imageData);
                Clay_Color tintColor = renderCommand->renderData.image.backgroundColor;
                if (tintColor.r == 0 && tintColor.g == 0 && tintColor.b == 0 && tintColor.a == 0) {
                    tintColor = Clay_Color{ 255, 255, 255, 255 };
                }
                DrawTextureEx(
                    imageTexture,
                    Vector2{bb.x, bb.y},
                    0,
                    bb.width / static_cast<float>(imageTexture.width),
                    ClayToRaylibColor(tintColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                BeginScissorMode(static_cast<int>(std::roundf(bb.x)), static_cast<int>(std::roundf(bb.y)), static_cast<int>(std::roundf(bb.width)), static_cast<int>(std::roundf(bb.height)));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                EndScissorMode();
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *config = &renderCommand->renderData.rectangle;
                if (config->cornerRadius.topLeft > 0) {
                    float radius = (config->cornerRadius.topLeft * 2) / static_cast<float>((bb.width > bb.height) ? bb.height : bb.width);
                    DrawRectangleRounded(Rectangle{ bb.x, bb.y, bb.width, bb.height }, radius, 8, ClayToRaylibColor(config->backgroundColor));
                } else {
                    DrawRectangle(bb.x, bb.y, bb.width, bb.height, ClayToRaylibColor(config->backgroundColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *config = &renderCommand->renderData.border;
                // Left border
                if (config->width.left > 0) {
                    DrawRectangle(
                        static_cast<int>(std::roundf(bb.x)),
                        static_cast<int>(std::roundf(bb.y + config->cornerRadius.topLeft)),
                        static_cast<int>(config->width.left),
                        static_cast<int>(std::roundf(bb.height - config->cornerRadius.topLeft - config->cornerRadius.bottomLeft)),
                        ClayToRaylibColor(config->color));
                }
                // Right border
                if (config->width.right > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + bb.width - config->width.right)),
                                  static_cast<int>(std::roundf(bb.y + config->cornerRadius.topRight)),
                                  static_cast<int>(config->width.right),
                                  static_cast<int>(std::roundf(bb.height - config->cornerRadius.topRight - config->cornerRadius.bottomRight)),
                                  ClayToRaylibColor(config->color));
                }
                // Top border
                if (config->width.top > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + config->cornerRadius.topLeft)),
                                  static_cast<int>(std::roundf(bb.y)),
                                  static_cast<int>(std::roundf(bb.width - config->cornerRadius.topLeft - config->cornerRadius.topRight)),
                                  static_cast<int>(config->width.top),
                                  ClayToRaylibColor(config->color));
                }
                // Bottom border
                if (config->width.bottom > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + config->cornerRadius.bottomLeft)),
                                  static_cast<int>(std::roundf(bb.y + bb.height - config->width.bottom)),
                                  static_cast<int>(std::roundf(bb.width - config->cornerRadius.bottomLeft - config->cornerRadius.bottomRight)),
                                  static_cast<int>(config->width.bottom),
                                  ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.topLeft > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + config->cornerRadius.topLeft), std::roundf(bb.y + config->cornerRadius.topLeft) },
                             std::roundf(config->cornerRadius.topLeft - config->width.top),
                             config->cornerRadius.topLeft,
                             180,
                             270,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.topRight > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + bb.width - config->cornerRadius.topRight), std::roundf(bb.y + config->cornerRadius.topRight) },
                             std::roundf(config->cornerRadius.topRight - config->width.top),
                             config->cornerRadius.topRight,
                             270,
                             360,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.bottomLeft > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + config->cornerRadius.bottomLeft), std::roundf(bb.y + bb.height - config->cornerRadius.bottomLeft) },
                             std::roundf(config->cornerRadius.bottomLeft - config->width.top),
                             config->cornerRadius.bottomLeft,
                             90,
                             180,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.bottomRight > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + bb.width - config->cornerRadius.bottomRight), std::roundf(bb.y + bb.height - config->cornerRadius.bottomRight) },
                             std::roundf(config->cornerRadius.bottomRight - config->width.bottom),
                             config->cornerRadius.bottomRight,
                             0.1,
                             90,
                             10,
                             ClayToRaylibColor(config->color));
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
    printf("%s", errorData.errorText.chars);
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

    Music music = LoadMusicStream("test.mp3");
    PlayMusicStream(music);
    SetTargetFPS(60);

    Font fonts[1];
    fonts[0] = LoadFontEx("resources/Roboto-Regular.ttf", 48, 0, 400);
    SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);
    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    double totalLayoutTime = 0.0;
    int totalIterations = 0;

    PlaybackState state;
    state.currTime = 0.0f;
    state.length = GetMusicTimeLength(music);

    while (!WindowShouldClose()) {
        UpdateMusicStream(music);

        state.currTime = GetMusicTimePlayed(music);

        Vector2 mouseWheelDelta = GetMouseWheelMoveV();
        float mouseWheelX = mouseWheelDelta.x;
        float mouseWheelY = mouseWheelDelta.y;

        if (IsKeyPressed(KEY_D)) {
            debugEnabled = !debugEnabled;
            Clay_SetDebugModeEnabled(debugEnabled);
        }
        //----------------------------------------------------------------------------------
        // Handle scroll containers
        Clay_Vector2 mousePosition = RaylibToClayVector2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0) && !scrollbarData.mouseDown);
        Clay_SetLayoutDimensions(Clay_Dimensions{
                static_cast<float>(GetScreenWidth()),
                static_cast<float>(GetScreenHeight()) });
        if (!IsMouseButtonDown(0)) {
            scrollbarData.mouseDown = false;
        }

        if (IsMouseButtonDown(0) &&
                !scrollbarData.mouseDown &&
                Clay_PointerOver(Clay__HashString(CLAY_STRING("ScrollBar"), 0, 0))) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay__HashString(CLAY_STRING("MainContent"), 0, 0));
            scrollbarData.clickOrigin = mousePosition;
            scrollbarData.positionOrigin = *scrollContainerData.scrollPosition;
            scrollbarData.mouseDown = true;
        } else if (scrollbarData.mouseDown) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay__HashString(CLAY_STRING("MainContent"), 0, 0));
            if (scrollContainerData.contentDimensions.height > 0) {
                Clay_Vector2 ratio{
                    scrollContainerData.contentDimensions.width / scrollContainerData.scrollContainerDimensions.width,
                    scrollContainerData.contentDimensions.height / scrollContainerData.scrollContainerDimensions.height,
                };
                if (scrollContainerData.config.vertical) {
                    scrollContainerData.scrollPosition->y = scrollbarData.positionOrigin.y + (scrollbarData.clickOrigin.y - mousePosition.y) * ratio.y;
                }
                if (scrollContainerData.config.horizontal) {
                    scrollContainerData.scrollPosition->x = scrollbarData.positionOrigin.x + (scrollbarData.clickOrigin.x - mousePosition.x) * ratio.x;
                }
            }
        }

        Clay_UpdateScrollContainers(true, Clay_Vector2{mouseWheelX, mouseWheelY}, GetFrameTime());

        double currentTime = GetTime();
        Clay_RenderCommandArray renderCommands = TestLayout(state);
        double layoutTime = (GetTime() - currentTime) * 1000.0 * 1000.0;
        totalLayoutTime += layoutTime;
        totalIterations++;
        printf("layout time: %f us\n", layoutTime); 

        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(renderCommands, fonts);
        EndDrawing();
    }

    printf("Avg layout time: %f us\n", totalLayoutTime / totalIterations);

    UnloadMusicStream(music);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
