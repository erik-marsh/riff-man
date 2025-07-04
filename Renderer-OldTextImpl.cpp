void FTPrintError(FT_Error error) {
#undef FTERRORS_H_
#define FT_ERRORDEF(err, errInt, str) case err: printf("FreeType: %s\n", str); break;
#define FT_ERROR_START_LIST switch(error) {
#define FT_ERROR_END_LIST default: printf("FreeType: Unknown error.\n"); break; }
#include FT_ERRORS_H
}

// DrawTextExN(fonts[text.fontId],
//             text.stringContents.chars,
//             text.stringContents.length,
//             Vector2{bb.x, bb.y},
//             static_cast<float>(text.fontSize),
//             static_cast<float>(text.letterSpacing),
//             casts::raylib::Color(text.textColor));
// FTDrawText(face,
//            text.stringContents.chars,
//            text.stringContents.length,
//            "jp",  // TODO: yeah this can't just be like that lol
//            bb.x,
//            bb.y);

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
