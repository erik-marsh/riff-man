#include "Renderer.hpp"

#include <cmath>
#include <cstdio>

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
Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
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

// I really don't foresee the bounds-checked get being necessary here.
// (If I become a Rust dev in the next 5 years I'll eat my Suisei plushie)
void RenderFrame(Clay_RenderCommandArray cmds, Font* fonts) {
    for (int i = 0; i < cmds.length; i++) { 
        const Clay_RenderCommand& cmd = *(Clay_RenderCommandArray_Get(&cmds, i));
        const Clay_BoundingBox& bb = cmd.boundingBox;

        switch (cmd.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData& text = cmd.renderData.text;
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
                const auto& image = cmd.renderData.image;
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
                const auto& config = cmd.renderData.rectangle;
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
                const auto& border = cmd.renderData.border;
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
