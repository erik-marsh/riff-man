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

#include "TextUtils.hpp"

#include <cstring>
#include <limits>

void FTPrintError(FT_Error error) {
#undef FTERRORS_H_
#define FT_ERRORDEF(err, errInt, str) case err: printf("FreeType: %s\n", str); break;
#define FT_ERROR_START_LIST switch(error) {
#define FT_ERROR_END_LIST default: printf("FreeType: Unknown error.\n"); break; }
#include FT_ERRORS_H
}

constexpr bool DRAW_DEBUG = 
#ifdef RIFF_MAN_DEBUG_FONTS
    true;
#else
    false;
#endif


////////////////////////////////////////////////////////////////////////////////
// TGAImage
////////////////////////////////////////////////////////////////////////////////
TGAImage::TGAImage(int w, int h)
    : width(w), height(h) {
    buffer = std::vector<uint8_t>(4 * width * height + 18, 0);
    buffer[2]  = 2;                      // Non-compressed true color
    buffer[12] = width & 0x00FF;         // TGA is a little-endian format
    buffer[13] = (width & 0xFF00) >> 8;
    buffer[14] = height & 0x00FF;
    buffer[15] = (height & 0xFF00) >> 8;    
    buffer[16] = 32;                     // 32 bits per pixel (BGRA)
}

int TGAImage::OffsetOf(int x, int y) const {
    return (4 * y * width) + (4 * x) + 18;
}

TGAImage RenderText(std::string_view str, const TextRenderContext& textCtx,
                   const char* langHint) {
    // TODO: we could add some introspection here to detect text language
    //       i think harfbuzz can do that directly
    if (!langHint)
        langHint = "jp";

    raqm_clear_contents(textCtx.rq);
    raqm_set_text_utf8(textCtx.rq, str.data(), str.size());
    raqm_set_freetype_face(textCtx.rq, textCtx.face);
    raqm_set_par_direction(textCtx.rq, RAQM_DIRECTION_LTR);
    raqm_set_language(textCtx.rq, langHint, 0, str.size());
    raqm_layout(textCtx.rq);

    size_t glyphCount;
    raqm_glyph_t* glyphs = raqm_get_glyphs(textCtx.rq, &glyphCount);

    // TODO: two things:
    //       * should we consider (negative) left bearings yet?
    //         will it be a problem if we ignore it?
    //       * should the last iteration use c_advance or glyph width?
    //         (though this caused me grief for some inexplicable reason)
    FT_Pos yLo = std::numeric_limits<FT_Pos>::max();
    FT_Pos yHi = std::numeric_limits<FT_Pos>::min();
    FT_Pos yBaseline = std::numeric_limits<FT_Pos>::max();
    int width = 0;
    int height = 0;
    for (size_t i = 0; i < glyphCount; i++) {
        FT_Load_Glyph(textCtx.face, glyphs[i].index, FT_LOAD_DEFAULT);
        const auto& metrics = textCtx.face->glyph->metrics;

        // shift here because we shift once per iteration of the rendering loop
        yHi = std::max(yHi, metrics.horiBearingY >> 6);
        yLo = std::min(yLo, (metrics.horiBearingY - metrics.height) >> 6);
        yBaseline = std::min(yBaseline, (metrics.horiBearingY - metrics.height) >> 6);
        
        width += glyphs[i].x_advance >> 6;
    }

    height = yHi - yLo;

    // on my machine, textures that do not have an even number width render
    // a bit skewed. seems to be a texture coordinate rounding issue.
    // this could be a general OpenGL thing on many machines, but idk yet
    if (width % 2 != 0)
        width++;

    TGAImage bmpOut(width, height);

    // TODO: add debug info back in

    int penX = 0;
    int penY = std::abs(yBaseline);

    for (size_t i = 0; i < glyphCount; i++) {
        FT_Load_Glyph(textCtx.face, glyphs[i].index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(textCtx.face->glyph, FT_RENDER_MODE_NORMAL);
        const FT_GlyphSlot glyph = textCtx.face->glyph;

        const int xOrigin = penX + glyph->bitmap_left;
        const int yOrigin = penY + glyph->bitmap_top - glyph->bitmap.rows;
        const FT_Bitmap& bmpIn = glyph->bitmap;
        
        for (unsigned int bx = 0; bx < bmpIn.width; bx++) {
            for (unsigned int by = 0; by < bmpIn.rows; by++) {
                const int x = xOrigin + bx;
                if (x > width || x < 0) continue;

                // the TGA image is a "y-down" system
                // whereas the FreeType bitmap is a "y-up" system
                const int y = yOrigin + (bmpIn.rows - by - 1);
                if (y > height || y < 0) continue;

                const int pxOut = bmpOut.OffsetOf(x, y);
                const int pxIn = bmpIn.width * by + bx;

                // I'm 90% sure the bitmap is an alpha channel, nothing else
                bmpOut.buffer[pxOut + 0] |= 0xFF; 
                bmpOut.buffer[pxOut + 1] |= 0xFF; 
                bmpOut.buffer[pxOut + 2] |= 0xFF; 
                bmpOut.buffer[pxOut + 3] |= bmpIn.buffer[pxIn];
            }
        }

        penX += glyphs[i].x_advance >> 6;
    }

    // Image image = LoadImageFromMemory(".tga", bmpOut.buffer.data(), bmpOut.buffer.size());
    // Texture texture = LoadTextureFromImage(image);
    // UnloadImage(image);
    // return texture;
    return bmpOut;
}


////////////////////////////////////////////////////////////////////////////////
// ASCIIAtlas
////////////////////////////////////////////////////////////////////////////////

// Some helpers for this class that don't need to be exposed anywhere
constexpr char charMin = 0x20;
constexpr char charMax = 0x7E;
constexpr char fallback = '?' - charMin;

constexpr char ASCIIToGlyph(char ch) {
    if (ch < charMin) return fallback;
    if (ch > charMax) return fallback;
    return ch - charMin;
}

ASCIIAtlas::ASCIIAtlas() 
    : m_maxAscent(-1),
      m_maxHeight(-1) {
    std::memset(&m_texture, 0, sizeof(m_texture));
}

ASCIIAtlas::ASCIIAtlas(const ASCIIAtlas& other)
    : m_maxAscent(other.m_maxAscent),
      m_maxHeight(other.m_maxHeight),
      m_glyphLocs(other.m_glyphLocs) {
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    UnloadImage(copy);
}

ASCIIAtlas::ASCIIAtlas(ASCIIAtlas&& other)
    : m_texture(other.m_texture),
      m_maxAscent(other.m_maxAscent),
      m_maxHeight(other.m_maxHeight),
      m_glyphLocs(std::move(other.m_glyphLocs)) {}

ASCIIAtlas& ASCIIAtlas::operator=(const ASCIIAtlas& other) {
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    m_maxAscent = other.m_maxAscent;
    m_maxHeight = other.m_maxHeight;
    m_glyphLocs = other.m_glyphLocs;
    UnloadImage(copy);
    return *this;
}

ASCIIAtlas& ASCIIAtlas::operator=(ASCIIAtlas&& other) {
    m_texture = other.m_texture;
    m_maxAscent = other.m_maxAscent;
    m_maxHeight = other.m_maxHeight;
    m_glyphLocs = std::move(other.m_glyphLocs);
    return *this;
}

// TODO: is it kosher for this to occur after CloseWindow() is called?
ASCIIAtlas::~ASCIIAtlas() {
    if (IsTextureValid(m_texture))
        UnloadTexture(m_texture);
}

bool ASCIIAtlas::LoadGlyphs(FT_Face face) {
    // create ASCII-ish font atlas as a default rendering fallback
    // characters of interest are 0x20-0x7E
    // question mark will be the "unknown character marker" (0x3F)
    // cols and rows are pretty much an arbitrary decision
    constexpr char numCols = 16;
    constexpr char numRows = 6;

    FT_Pos maxWidth = 0;
    FT_Pos maxHeight = 0;

    for (char i = charMin; i <= charMax; i++) {
        const FT_UInt glyphIndex = FT_Get_Char_Index(face, i);
        FT_Error err = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (err) {
            FTPrintError(err);
            return false;
        }
        const auto& metrics = face->glyph->metrics;

        maxWidth = std::max(maxWidth, metrics.width);
        maxHeight = std::max(maxHeight, metrics.height);
    }

    maxWidth >>= 6;
    maxHeight >>= 6;

    const FT_Pos atlasWidth = maxWidth * numCols;
    const FT_Pos atlasHeight = maxHeight * numRows;

    TGAImage bmpOut(atlasWidth, atlasHeight);

    // 2. draw freetype bitmaps and mark their positions
    m_glyphLocs.resize(charMax - charMin + 1);

    // i hate warnings sometimes...
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    using uint = unsigned int;
#pragma GCC diagnostic pop

    for (char i = charMin ; i <= charMax; i++) {
        const char ch = ASCIIToGlyph(i);
        const uint row = ch / numCols;
        const uint col = ch % numCols;
        const uint xOrigin = col * maxWidth;
        const uint yOrigin = row * maxHeight;

        const FT_UInt glyphIndex = FT_Get_Char_Index(face, i);
        FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        const FT_GlyphSlot glyph = face->glyph;
        const FT_Bitmap& bmpIn = glyph->bitmap;

        for (uint bx = 0; bx < bmpIn.width; bx++) {
             for (uint by = 0; by < bmpIn.rows; by++) {
                const uint x = xOrigin + bx;
                // the TGA image is a "y-down" system
                // whereas the FreeType bitmap is a "y-up" system
                const uint y = yOrigin + (bmpIn.rows - by - 1);
                const uint pxOut = bmpOut.OffsetOf(x, y);
                const uint pxIn = bmpIn.width * by + bx;

                bmpOut.buffer[pxOut + 0] |= 0xFF; 
                bmpOut.buffer[pxOut + 1] |= 0xFF; 
                bmpOut.buffer[pxOut + 2] |= 0xFF; 
                bmpOut.buffer[pxOut + 3] |= bmpIn.buffer[pxIn];
            }
        }

        m_glyphLocs[ch].x = xOrigin;
        m_glyphLocs[ch].y = yOrigin;
        m_glyphLocs[ch].width = bmpIn.width;
        m_glyphLocs[ch].height = bmpIn.rows;
        m_glyphLocs[ch].penOffsetX = glyph->bitmap_left;
        m_glyphLocs[ch].penOffsetY = bmpIn.rows - glyph->bitmap_top;

        m_maxAscent = std::max(m_maxAscent, glyph->bitmap_top);
        m_maxHeight = std::max(m_maxHeight, static_cast<int>(bmpIn.rows));

        if constexpr (DRAW_DEBUG) {
            // [green] draw baseline of each glyph
            for (uint j = m_glyphLocs[ch].penOffsetX; j < maxWidth; j++) {
                const uint baselineY = m_glyphLocs[ch].y + m_glyphLocs[ch].penOffsetY;
                const uint baselineX = m_glyphLocs[ch].x + j;
                const uint offset = bmpOut.OffsetOf(baselineX, baselineY);
                if (offset >= bmpOut.buffer.size())
                    break;

                bmpOut.buffer[offset + 1] = 0xFF;
                bmpOut.buffer[offset + 3] = 0xFF;
            }

            // [green, dotted] draw x = left bearing
            for (uint j = m_glyphLocs[ch].y; j < m_glyphLocs[ch].y + maxHeight; j += 2) {
                const uint x = m_glyphLocs[ch].x + m_glyphLocs[ch].penOffsetX;
                const uint offset = bmpOut.OffsetOf(x, j);
                if (offset >= bmpOut.buffer.size())
                    break;

                bmpOut.buffer[offset + 0] = 0x00;
                bmpOut.buffer[offset + 1] = 0xFF;
                bmpOut.buffer[offset + 2] = 0x00;
                bmpOut.buffer[offset + 3] = 0xFF;
            }

            // [red] draw bounding box (?)
            for (uint j = m_glyphLocs[ch].x; j < m_glyphLocs[ch].x + m_glyphLocs[ch].width; j++) {
                const uint yLo = m_glyphLocs[ch].y;
                const uint yHi = m_glyphLocs[ch].y + m_glyphLocs[ch].height - 1;
                const uint offsetLo = bmpOut.OffsetOf(j, yLo);
                const uint offsetHi = bmpOut.OffsetOf(j, yHi);
                if (offsetLo >= bmpOut.buffer.size() || offsetHi >= bmpOut.buffer.size())
                    break;

                bmpOut.buffer[offsetLo + 0] = 0x00;
                bmpOut.buffer[offsetLo + 1] = 0x00;
                bmpOut.buffer[offsetLo + 2] = 0xFF;
                bmpOut.buffer[offsetLo + 3] = 0xFF;
                bmpOut.buffer[offsetHi + 0] = 0x00;
                bmpOut.buffer[offsetHi + 1] = 0x00;
                bmpOut.buffer[offsetHi + 2] = 0xFF;
                bmpOut.buffer[offsetHi + 3] = 0xFF;
            }
            for (uint j = m_glyphLocs[ch].y; j < m_glyphLocs[ch].y + m_glyphLocs[ch].height; j++) {
                const uint xLo = m_glyphLocs[ch].x;
                const uint xHi = m_glyphLocs[ch].x + m_glyphLocs[ch].width - 1;
                const uint offsetLo = bmpOut.OffsetOf(xLo, j);
                const uint offsetHi = bmpOut.OffsetOf(xHi, j);
                if (offsetLo >= bmpOut.buffer.size() || offsetHi >= bmpOut.buffer.size())
                    break;

                bmpOut.buffer[offsetLo + 0] = 0x00;
                bmpOut.buffer[offsetLo + 1] = 0x00;
                bmpOut.buffer[offsetLo + 2] = 0xFF;
                bmpOut.buffer[offsetLo + 3] = 0xFF;
                bmpOut.buffer[offsetHi + 0] = 0x00;
                bmpOut.buffer[offsetHi + 1] = 0x00;
                bmpOut.buffer[offsetHi + 2] = 0xFF;
                bmpOut.buffer[offsetHi + 3] = 0xFF;
            }
        }
    }

    Image image = LoadImageFromMemory(".tga", bmpOut.buffer.data(), bmpOut.buffer.size());
    m_texture = LoadTextureFromImage(image);
    UnloadImage(image);

    return true;
}

Texture& ASCIIAtlas::RaylibTexture() { return m_texture; }

int ASCIIAtlas::GetMaxAscent() const { return m_maxAscent; }

int ASCIIAtlas::GetMaxHeight() const { return m_maxHeight; }

const ASCIIAtlas::GlyphInfo& ASCIIAtlas::GetGlyphLocation(char ch) const {
    const char offset = ASCIIToGlyph(ch);
    return m_glyphLocs[offset];
}

