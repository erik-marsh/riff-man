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
    auto& textCtx = *reinterpret_cast<TextRenderContext*>(userData);

    raqm_clear_contents(textCtx.rq);
    raqm_set_text_utf8(textCtx.rq, text.chars, text.length);
    raqm_set_freetype_face(textCtx.rq, textCtx.face);
    raqm_set_par_direction(textCtx.rq, RAQM_DIRECTION_LTR);
    raqm_set_language(textCtx.rq, "en", 0, text.length);
    raqm_layout(textCtx.rq);

    size_t count;
    raqm_glyph_t* glyphs = raqm_get_glyphs(textCtx.rq, &count);
    if (count == 0)
        return { 0.0f, 0.0f };

    // TODO: two things
    //       * should we consider negative left bearings?
    //         furthermore, what about the left bearing of glyph 0 in the first place?
    //       * should the last loop iteration use the x_advance or the glyph width?
    //         (i got a really nasty, incomprehensible bug last time i tried the latter, though...)

    unsigned int width = 0;
    for (size_t i = 0; i < count; i++)
        width += glyphs[i].x_advance >> 6; // shifting here matches the implementation in DrawTextASCII

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
    return {
        .width = width,
        .height = textCtx.atlas.GetMaxHeight()  // this is already in pixels
    };
#pragma GCC diagnostic pop
}

void DrawTextASCII(TextRenderContext& textCtx, const char* text, int len, int x, int y) {
    const Texture& tex = textCtx.atlas.RaylibTexture();

    raqm_clear_contents(textCtx.rq);
    raqm_set_text_utf8(textCtx.rq, text, len);
    raqm_set_freetype_face(textCtx.rq, textCtx.face);
    raqm_set_par_direction(textCtx.rq, RAQM_DIRECTION_LTR);
    raqm_set_language(textCtx.rq, "en", 0, len);
    raqm_layout(textCtx.rq);

    size_t count;
    raqm_glyph_t* glyphs = raqm_get_glyphs(textCtx.rq, &count);
    if (count == 0)
        return;

    for (size_t i = 0; i < count; i++) {
        const AtlasGlyph& loc = textCtx.atlas.GetGlyphLocation(text[i]);
        const int glyphAscent = loc.height - loc.penOffsetY;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
        const Rectangle glyphSlice{
            .x = loc.x,
            .y = tex.height - loc.y - loc.height,
            .width = loc.width,
            .height = loc.height
        };
        const Vector2 pos{
            .x = x + loc.penOffsetX,
            .y = y + (textCtx.atlas.GetMaxAscent() - glyphAscent)
        };
#pragma GCC diagnostic pop

        DrawTextureRec(tex, glyphSlice, pos, WHITE);
        x += glyphs[i].x_advance >> 6; 
    }
}

// I really don't foresee the bounds-checked get being necessary here.
// (If I become a Rust dev in the next 5 years I'll eat my Suisei plushie)
void RenderFrame(Clay_RenderCommandArray cmds, TextRenderContext& textCtx) {
    for (int i = 0; i < cmds.length; i++) { 
        const Clay_RenderCommand& cmd = *(Clay_RenderCommandArray_Get(&cmds, i));
        const Clay_BoundingBox& bb = cmd.boundingBox;

        switch (cmd.commandType) {
         case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const auto& str = cmd.renderData.text.stringContents;
            // DrawRectangle(bb.x, bb.y, bb.width, bb.height, RED);
            DrawTextASCII(textCtx, str.chars, str.length, bb.x, bb.y);
         } break;

         case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            const auto& image = cmd.renderData.image;
            const auto& imageTexture = *reinterpret_cast<Texture2D*>(image.imageData);

            const Vector2 origin{ bb.x, bb.y };
            const float scale = bb.width / static_cast<float>(imageTexture.width);
            Color tint = casts::raylib::Color(image.backgroundColor);
            if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0)
                tint = Color{ 255, 255, 255, 255 };
            // DrawRectangle(bb.x, bb.y, bb.width, bb.height, RED);
            DrawTextureEx(imageTexture, origin, 0, scale, tint);
         } break;

         case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
            // omitting the rounding does in fact cause issues here
            BeginScissorMode(std::roundf(bb.x), std::roundf(bb.y),
                             std::roundf(bb.width), std::roundf(bb.height)); 
         } break;
                                                      
         case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
            EndScissorMode();
         } break;

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
         } break;

         // NOTE: borders are drawn IN the main rect, not outside of it
         case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const auto& border = cmd.renderData.border;
            const auto& corner = border.cornerRadius;
            const auto& width = border.width;
            const auto color = casts::raylib::Color(border.color);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
            const auto left = Rectangle{
                .x = bb.x,
                .y = bb.y + corner.topLeft,
                .width = width.left,
                .height = bb.height - corner.topLeft - corner.bottomLeft };
            const auto right = Rectangle{
                .x = bb.x + bb.width - width.right,
                .y = bb.y + corner.topRight,
                .width = width.right,
                .height = bb.height - corner.topRight - corner.bottomRight };
            const auto top = Rectangle{
                .x = bb.x + corner.topLeft,
                .y = bb.y,
                .width = bb.width - corner.topLeft - corner.topRight,
                .height = width.top };
            const auto bottom = Rectangle{
                .x = bb.x + corner.bottomLeft,
                .y = bb.y + bb.height - width.bottom,
                .width = bb.width - corner.bottomLeft - corner.bottomRight,
                .height = width.bottom };
#pragma GCC diagnostic pop

            DrawRectangleRec(left, color);
            DrawRectangleRec(right, color);
            DrawRectangleRec(top, color);
            DrawRectangleRec(bottom, color);

            const Vector2 topLeft{
                bb.x + corner.topLeft,
                bb.y + corner.topLeft };
            const Vector2 topRight{
                bb.x + bb.width - corner.topRight,
                bb.y + corner.topRight };
            const Vector2 bottomLeft{
                bb.x + corner.bottomLeft,
                bb.y + bb.height - corner.bottomLeft };
            const Vector2 bottomRight{
                bb.x + bb.width - corner.bottomRight,
                bb.y + bb.height - corner.bottomRight };

            DrawRing(topLeft, corner.topLeft - width.top, corner.topLeft,
                     180.0f, 270.0f, 10, color);
            DrawRing(topRight, corner.topRight - width.top, corner.topRight,
                     270.0f, 360.0f, 10, color);
            DrawRing(bottomLeft, corner.bottomLeft - width.bottom, corner.bottomLeft,
                     90.0f, 180.0f, 10, color);
            DrawRing(bottomRight, corner.bottomRight - width.bottom, corner.bottomRight,
                     0.1f, 90.0f, 10, color);
         } break;

         default: {
            printf("Unhandled render command.\n");
         }
        }
    }
}
