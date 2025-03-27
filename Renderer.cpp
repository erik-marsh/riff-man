// Some notes from the FreeType "Glyph Conventions" article:
// 
// "points" are a physical unit, not a logical one
// one point = 1/72 inches
// pixel size = point size * dpi / 72
//     these may be different for the x and y axis, too
// so, right of the bat, measuring text is not an easy task necessarily
// 
// outline => the scalable version of a glyph
//            outlines are defined as points on a discrete grid
//            this grid is large enough that you can assume it is continuous
//
// glyphs have their own coordinate system:
//   x axis: L to R
//   y axis: bottom to top
// the "em square" is a virtual canvas on which a glyph is drawn
// it is useful for scaling text
// fonts can indeed draw glyph segments outside of the em square 
//
// grid fitting is a thing
// basically: there's a lot of transformations between coord systems going on
// this causes issues, because screens are discrete pixel grids,
// while fonts are typically curves on a continuous coordinate system
//
// BASICS OF LAYOUT
// i will only be considering horizontal layouts here.
//     I have never seen a UI in a vertical layout. sorry mongolia
// baseline => a guide for glyph placement
//             in horizontal layouts, glyphs sit on the baseline
// pen position/origin => a virtual point on the baseline used to render a glyph
// advance width => distance between two successive pen positions
//                  ALWAYS POSITIVE, even in R->L scripts
//                  there is such a thing as advance height, but that is only for vertical scripts
// ascent => distance between baseline and highest outline point of a glyph
//           always positive (Y IS UP)
// descent => distance between baseline and lowest outline point of a glyph
//           always negative (Y IS UP)
// linegap => distance between two lines of text
//            the proper baseline-to-baseline distance is (ascent - descent + linegap)
// bounding box => of a glyph. self-explanatory
// internal leading => space taken by stuff outside the em square (ascent - descent - em size)
// external leading => linegap
//
// bearings =>
//   left side => distance from pen pos to glyph's left bbox edge
//   top side => distance from pen pos to glyph's top bbox edge
//   right side => distance from right bbox edge to the advance width
// glyph width => self-explanatory. can be derived from bbox values
// glyph height => self-explanatory. can be derived from bbox values
//
// 26.6 refers to a fixed point encoding used by FreeType
//     26 bit integer part, 6 bit fractional part
//     hence the value of smallest magnitude is 1/(2^6) = 1/64
//     therefore, any 26.6 value is interpreted as n/64ths of some unit
//     this unit is not necessarily pixels... be vigilant

#include "Renderer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <raqm.h>

void FTPrintError(FT_Error error) {
#undef FTERRORS_H_
#define FT_ERRORDEF(err, errInt, str) case err: printf("FreeType: %s\n", str); break;
#define FT_ERROR_START_LIST switch(error) {
#define FT_ERROR_END_LIST default: printf("FreeType: Unknown error.\n"); break; }
#include FT_ERRORS_H
}

// NOTE: Interestingly, Clay determines wrapping, not us.
//       Thus the burden falls on us to know when Clay will wrap
//       and we must adjust our measure accordingly.
Clay_Dimensions FTMeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) {
    const char* lang = "jp";
    Clay_Dimensions ret{ .width = 0.0f, .height = 0.0f };

    FT_Face face = reinterpret_cast<FT_Face>(userData);

    raqm_t* rq = raqm_create();
    raqm_set_text_utf8(rq, text.chars, text.length);  // hides a call to realloc 
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, lang, 0, text.length);
    raqm_layout(rq);

    size_t count;
    raqm_glyph_t* glyphs = raqm_get_glyphs(rq, &count);

    // TODO: I'm not sure if we need to know the pen's initial position when
    //       measuring text, or if wa just need to consider only the texture's
    //       width and height.
    // TODO: we might be able to get away with setting the text height to the font size in pixels
    unsigned int width = 0;
    FT_Pos yLo = std::numeric_limits<FT_Pos>::max(); // TODO: see font descent maybe?
    FT_Pos yHi = std::numeric_limits<FT_Pos>::min(); // TODO: similarly, ascent?
    for (size_t i = 0; i < count; i++) {
        FT_Load_Glyph(face, glyphs[i].index, FT_LOAD_DEFAULT);
        yHi = std::max(yHi, face->glyph->metrics.horiBearingY);
        yLo = std::min(yLo, face->glyph->metrics.horiBearingY - face->glyph->metrics.height);
        
        // TODO: how should negative left bearings be handled?
        //       maybe just ignore it until it becomes a problem?
        // TODO: you could add only the glyph width on the last iteration,
        //              and that would reduce the size of the texture,
        //              but for some incomprehensible reason that fucked up the whole loop
        //              I still have no idea what went wrong.
        if (i == 0)
            width += face->glyph->metrics.horiBearingX;
        width += glyphs[i].x_advance;
    }

    raqm_destroy(rq);

    // metrics need to be converted from 26.6 format
    ret.width = static_cast<float>(width / 64);
    ret.height = static_cast<float>((yHi - yLo) / 64);
    return ret;
}

void DrawGlyph(std::vector<uint8_t>& image, int imageWidth, int imageHeight,
               FT_GlyphSlot glyph, int penX, int penY, bool debug) {
    const int xOrigin = penX + glyph->bitmap_left;
    const int yOrigin = penY + glyph->bitmap_top - glyph->bitmap.rows;
    const FT_Bitmap& bmp = glyph->bitmap;

    for (unsigned int bx = 0; bx < bmp.width; bx++) {
         for (unsigned int by = 0; by < bmp.rows; by++) {
            const int x = xOrigin + bx;
            if (x >= imageWidth || x < 0) continue;

            // the TGA image is a "y-down" system
            // whereas the FreeType bitmat is a "y-up" system
            const int y = yOrigin + (bmp.rows - by - 1);
            if (y >= imageHeight || y < 0) continue;

            const int outOffset = (4 * y * imageWidth) + (4 * x) + 18;
            const int bitmapOffset = bmp.width * by + bx;
            // a 32-bit depth TGA's colors are BGRA
            image[outOffset + 0] |= bmp.buffer[bitmapOffset];
            image[outOffset + 1] |= bmp.buffer[bitmapOffset];
            image[outOffset + 2] |= bmp.buffer[bitmapOffset];
            image[outOffset + 3] |= bmp.buffer[bitmapOffset];

            // show's character's bounding box in blue
            if (debug && (bx == 0 || by == 0 || bx == bmp.width - 1 || by == bmp.rows - 1)) {
                image[outOffset + 0] = 0xFF;
                image[outOffset + 3] = 0xFF;
            }
        }
    }
}

void FTDrawText(FT_Face face, const char* text, int textLen, const char* lang, int xPos, int yPos) {
    const auto sizes = FTMeasureText(Clay_StringSlice{.length = textLen, .chars = text}, nullptr, face);
    const int width = static_cast<int>(sizes.width);
    const int height = static_cast<int>(sizes.height);

    std::vector<uint8_t> image(4 * width * height + 18, 0);
    const auto OffsetOf = [&width](int x, int y) {
        return (4 * y * width) + (4 * x) + 18;
    };

    image[2] = 2;
    image[12] = width & 0x00FF;
    image[13] = (width & 0xFF00) >> 8;
    image[14] = height & 0x00FF;
    image[15] = (height & 0xFF00) >> 8;
    image[16] = 32;

    raqm_t* rq = raqm_create();
    raqm_set_text_utf8(rq, text, textLen);  // hides a call to realloc 
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, lang, 0, textLen);
    raqm_layout(rq);

    constexpr bool debug = false;
    int yLo = std::numeric_limits<int>::max(); 
    int yHi = std::numeric_limits<int>::min(); 

    FT_Error error;
    size_t count;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &count);
    // printf("glyph count: %zu\n", count);
    // for proper placement on the texture we need to iterate over the glyphs 
    // and determine how low below the baseline we need to render
    // TODO: merge this step and the texture size calculation
    // TODO: we need to do something similar for negative left bearings of first chars too
    FT_Pos pxBelowBaseline = std::numeric_limits<int>::max();
    for (size_t i = 0; i < count; i++) {
        error = FT_Load_Glyph(face, glyphs[i].index, FT_LOAD_DEFAULT);
        if (error) FTPrintError(error);
        pxBelowBaseline = std::min(pxBelowBaseline, face->glyph->metrics.horiBearingY - face->glyph->metrics.height);
    }

    pxBelowBaseline /= 64;

    int penX = 0;
    int penY = pxBelowBaseline < 0 ? 0 - pxBelowBaseline : 0;

    for (size_t i = 0; i < count; i++)
    {
        // printf ("gid#%d\toff: (%d, %d) adv: (%d, %d) idx: %d\n",
        //         glyphs[i].index,
        //         glyphs[i].x_offset,
        //         glyphs[i].y_offset,
        //         glyphs[i].x_advance,
        //         glyphs[i].y_advance,
        //         glyphs[i].cluster);

        error = FT_Load_Glyph(face, glyphs[i].index, FT_LOAD_DEFAULT);
        if (error)
            FTPrintError(error);

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (error)
            FTPrintError(error);

        DrawGlyph(image, width, height, face->glyph, penX, penY, debug);

        if (debug) {
            // draw baseline in green
            const int offset = OffsetOf(penX, penY);
            int nPixels = glyphs[i].x_advance / 64;
            if (penX + nPixels > width)
                nPixels = width - penX;

            for (int i = 0; i < nPixels; i++) {
                image[offset + (4 * i) + 1] = 0xFF;
                image[offset + (4 * i) + 3] = 0xFF;
            }

            const int iTop = static_cast<int>(face->glyph->bitmap_top);
            const int iRows = static_cast<int>(face->glyph->bitmap.rows);
            yHi = std::max(yHi, iTop);
            yLo = std::min(yLo, iTop - iRows);  
        }
            
        // I'm pretty sure this is using the same 26.6 format that FreeType uses
        penX += (glyphs[i].x_advance / 64);
    }

    if (debug) {
        // draw whole text bbox in red
        for (int i = 0; i < width; i++) {
            const int offsetLower = OffsetOf(i, 0);
            const int offsetUpper = OffsetOf(i, height - 1);
            image[offsetLower + 2] = 0xFF;
            image[offsetLower + 3] = 0xFF;
            image[offsetUpper + 2] = 0xFF;
            image[offsetUpper + 3] = 0xFF;
        }

        for (int i = 1; i < height - 1; i++) {
            const int offsetLeft = OffsetOf(0, i);
            const int offsetRight = OffsetOf(width - 1, i);
            image[offsetLeft + 2] = 0xFF;
            image[offsetLeft + 3] = 0xFF;
            image[offsetRight + 2] = 0xFF;
            image[offsetRight + 3] = 0xFF;
        }
    }

    // FILE* file = std::fopen("out.tga", "w");
    // for (uint8_t i : image)
    //     std::putc(i, file);
    // std::fclose(file);

    raqm_destroy(rq);

    Image rlImage = LoadImageFromMemory(".tga", image.data(), image.size());
    Texture rlTexture = LoadTextureFromImage(rlImage);

    DrawTexture(rlTexture, xPos, yPos, Color{255,255,255,255});
}

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
void RenderFrame(Clay_RenderCommandArray cmds, FT_Face face) {
    for (int i = 0; i < cmds.length; i++) { 
        const Clay_RenderCommand& cmd = *(Clay_RenderCommandArray_Get(&cmds, i));
        const Clay_BoundingBox& bb = cmd.boundingBox;

        switch (cmd.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData& text = cmd.renderData.text;
                // DrawTextExN(fonts[text.fontId],
                //             text.stringContents.chars,
                //             text.stringContents.length,
                //             Vector2{bb.x, bb.y},
                //             static_cast<float>(text.fontSize),
                //             static_cast<float>(text.letterSpacing),
                //             ClayToRaylibColor(text.textColor));
                FTDrawText(face,
                           text.stringContents.chars,
                           text.stringContents.length,
                           "jp",  // TODO: yeah this can't just be like that lol
                           bb.x,
                           bb.y);
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
