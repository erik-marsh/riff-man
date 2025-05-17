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

#include "TextUtils.hpp"
#include "Casts.hpp"

// NOTE: Interestingly, Clay determines wrapping, not us.
//       Thus the burden falls on us to know when Clay will wrap
//       and we must adjust our measure accordingly.
Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig*, void* userData) {
    const char* lang = "en";
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

void DrawTextASCII(ASCIIFontAtlas& atlas, FT_Face face, const char* text, int len, int x, int y) {
    const Texture& tex = atlas.RaylibTexture();

    raqm_t* rq = raqm_create();
    raqm_set_text_utf8(rq, text, len);  // hides a call to realloc 
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, "en", 0, len);
    raqm_layout(rq);

    size_t count;
    raqm_glyph_t* glyphs = raqm_get_glyphs(rq, &count);

    for (size_t i = 0; i < count; i++) {
        const AtlasGlyph& loc = atlas.GetGlyphLocation(text[i]);
        const int glyphAscent = loc.height - loc.penOffsetY;

        Rectangle glyphSlice{
            .x = static_cast<float>(loc.x),
            .y = static_cast<float>(tex.height - (loc.y + loc.height)),
            .width = static_cast<float>(loc.width),
            .height = static_cast<float>(loc.height)
        };
        Vector2 pos{
            .x = static_cast<float>(x + loc.penOffsetX),
            .y = static_cast<float>(y + (atlas.GetMaxAscent() - glyphAscent))
        };
        DrawTextureRec(tex, glyphSlice, pos, RAYWHITE);

        x += glyphs[i].x_advance / 64;
    }
}

// I really don't foresee the bounds-checked get being necessary here.
// (If I become a Rust dev in the next 5 years I'll eat my Suisei plushie)
void RenderFrame(Clay_RenderCommandArray cmds, FT_Face face, ASCIIFontAtlas& atlas) {
    for (int i = 0; i < cmds.length; i++) { 
        const Clay_RenderCommand& cmd = *(Clay_RenderCommandArray_Get(&cmds, i));
        const Clay_BoundingBox& bb = cmd.boundingBox;

        switch (cmd.commandType) {
         case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            // const Clay_TextRenderData& text = cmd.renderData.text;
            const auto& str = cmd.renderData.text.stringContents;
            DrawRectangle(bb.x, bb.y, bb.width, bb.height, RED);
            DrawTextASCII(atlas, face, str.chars, str.length, bb.x, bb.y);
            break;
         }
         case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            const auto& image = cmd.renderData.image;
            const Vector2 origin{ bb.x, bb.y };
            const Texture2D imageTexture = *reinterpret_cast<Texture2D*>(image.imageData);
            const float scale = bb.width / static_cast<float>(imageTexture.width);
            Color tint = casts::raylib::Color(image.backgroundColor);
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
            const Color color = casts::raylib::Color(config.backgroundColor);

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
            const Color color = casts::raylib::Color(border.color);

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
