#include "TextUtils.hpp"

#include <array>
#include <cstring>
#include <limits>

constexpr bool DRAW_DEBUG = 
#ifdef RIFF_MAN_DEBUG_FONTS
    true;
#else
    false;
#endif

DynamicText::DynamicText()
    : m_width(0), m_height(0) {
    std::memset(&m_texture, 0, sizeof(m_texture));
}

DynamicText::DynamicText(const DynamicText& other)
    : m_width(other.m_width), m_height(other.m_height) {
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    UnloadImage(copy);
}

DynamicText::DynamicText(DynamicText&& other)
    : m_width(other.m_width), m_height(other.m_height),
      m_texture(other.m_texture) { }

DynamicText& DynamicText::operator=(const DynamicText& other) {
    m_width = other.m_width;
    m_height = other.m_height;
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    UnloadImage(copy);
    return *this;
}

DynamicText& DynamicText::operator=(DynamicText&& other) {
    m_width = other.m_width;
    m_height = other.m_height;
    m_texture = other.m_texture;
    return *this;
}

void DynamicText::LoadText(std::string_view str, FT_Face face, raqm_t* rq,
                           const char* langHint) {
    // TODO: we could add some introspetion here to detect text layout
    //       i think harfbuzz can do that directly

    // raqm does some malloc/calloc/realloc business internally,
    // so it would probably be best to keep raqm out of hot loops
    // TODO: this can fail, so it might be good to move it out of the ctor
    raqm_set_text_utf8(rq, str.data(), str.size());
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);

    // TODO: defaulting to japanese is specifically useful to me and me alone
    if (!langHint)
        langHint = "jp";
    raqm_set_language(rq, langHint, 0, str.size());
    raqm_layout(rq);

    size_t glyphCount;
    raqm_glyph_t* glyphs = raqm_get_glyphs(rq, &glyphCount);

    // TODO: I'm not sure if we need to know the pen's initial position when
    //       measuring text, or if wa just need to consider only the texture's
    //       width and height.
    // TODO: we might be able to get away with setting the text height to the font size in pixels
    FT_Pos yLo = std::numeric_limits<FT_Pos>::max(); // TODO: see font descent maybe?
    FT_Pos yHi = std::numeric_limits<FT_Pos>::min(); // TODO: similarly, ascent?
    FT_Pos yBaseline = std::numeric_limits<FT_Pos>::max();
    for (size_t i = 0; i < glyphCount; i++) {
        FT_Load_Glyph(face, glyphs[i].index, FT_LOAD_DEFAULT);
        const auto& metrics = face->glyph->metrics;

        yHi = std::max(yHi, metrics.horiBearingY);
        yLo = std::min(yLo, metrics.horiBearingY - metrics.height);
        yBaseline = std::min(yBaseline, metrics.horiBearingY - metrics.height);
        
        // TODO: how should negative left bearings be handled?
        //       maybe just ignore it until it becomes a problem?
        // TODO: you could add only the glyph width on the last iteration,
        //              and that would reduce the size of the texture,
        //              but for some incomprehensible reason that fucked up the whole loop
        //              I still have no idea what went wrong.
        if (i == 0)
            m_width += metrics.horiBearingX;
        m_width += glyphs[i].x_advance;
    }

    yBaseline >>= 6;
    m_width >>= 6;
    m_height = (yHi - yLo) >> 6;
    auto m_bitmap = std::vector<uint8_t>(4 * m_width * m_height + 18, 0);
    const auto OffsetOf = [this](int x, int y) {
        return (4 * y * m_width) + (4 * x) + 18;
    };

    // Set TGA header information
    m_bitmap[2] = 2;                        // Non-compressed true color
    m_bitmap[12] = m_width & 0x00FF;        // TGA is a little-endian format
    m_bitmap[13] = (m_width & 0xFF00) >> 8;
    m_bitmap[14] = m_height & 0x00FF;
    m_bitmap[15] = (m_height & 0xFF00) >> 8;    
    m_bitmap[16] = 32;                      // 32 bits per pixel (BGRA)

    // TODO: add debug info back in

    int penX = 0;
    int penY = std::abs(yBaseline);

    for (size_t i = 0; i < glyphCount; i++) {
        FT_Load_Glyph(face, glyphs[i].index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        const FT_GlyphSlot glyph = face->glyph;

        const int xOrigin = penX + glyph->bitmap_left;
        const int yOrigin = penY + glyph->bitmap_top - glyph->bitmap.rows;
        const FT_Bitmap& bmp = glyph->bitmap;

        for (unsigned int bx = 0; bx < bmp.width; bx++) {
             for (unsigned int by = 0; by < bmp.rows; by++) {
                const int x = xOrigin + bx;
                if (x >= m_width || x < 0) continue;

                // the TGA image is a "y-down" system
                // whereas the FreeType bitmap is a "y-up" system
                const int y = yOrigin + (bmp.rows - by - 1);
                if (y >= m_height || y < 0) continue;

                // TODO: could we just set the alpha to the bitmap value?
                const int outOffset = OffsetOf(x, y);
                const int bitmapOffset = bmp.width * by + bx;

                // NOTE: the difference between these two implmentations
                //       actually changes the look and feel substantially.
                //       first is thinner, second is stockier
                // m_bitmap[outOffset + 0] |= bmp.buffer[bitmapOffset];
                // m_bitmap[outOffset + 1] |= bmp.buffer[bitmapOffset];
                // m_bitmap[outOffset + 2] |= bmp.buffer[bitmapOffset];
                // m_bitmap[outOffset + 3] |= bmp.buffer[bitmapOffset];
                m_bitmap[outOffset + 0] |= 0xFF; 
                m_bitmap[outOffset + 1] |= 0xFF; 
                m_bitmap[outOffset + 2] |= 0xFF; 
                m_bitmap[outOffset + 3] |= bmp.buffer[bitmapOffset];
            }
        }

        penX += glyphs[i].x_advance >> 6;
    }

    // TODO: raylib lets you draw to Images directly
    //       could replace the need for tga encoding maybe?
    Image image = LoadImageFromMemory(".tga", m_bitmap.data(), m_bitmap.size());
    m_texture = LoadTextureFromImage(image);
    UnloadImage(image);

    raqm_clear_contents(rq);
}

// TODO: is it kosher for this to occur after CloseWindow() is called?
DynamicText::~DynamicText() {
    if (IsTextureValid(m_texture))
        UnloadTexture(m_texture);
}

int DynamicText::Width() const { return m_width; }
int DynamicText::Height() const { return m_height; }
// const std::vector<uint8_t>& DynamicText::Bitmap() const { return m_bitmap; }
Clay_Dimensions DynamicText::ClayDimensions() const {
    return {
        .width = static_cast<float>(m_width),
        .height = static_cast<float>(m_height)
    };
}
Texture& DynamicText::RaylibTexture() { return m_texture; }

ASCIIFontAtlas::ASCIIFontAtlas() {
    std::memset(&m_texture, 0, sizeof(m_texture));
}

ASCIIFontAtlas::ASCIIFontAtlas(const ASCIIFontAtlas& other) {
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    UnloadImage(copy);
}

ASCIIFontAtlas::ASCIIFontAtlas(ASCIIFontAtlas&& other)
    : m_texture(other.m_texture) {}

ASCIIFontAtlas& ASCIIFontAtlas::operator=(const ASCIIFontAtlas& other) {
    Image copy = LoadImageFromTexture(other.m_texture);
    m_texture = LoadTextureFromImage(copy);
    UnloadImage(copy);
    return *this;
}

ASCIIFontAtlas& ASCIIFontAtlas::operator=(ASCIIFontAtlas&& other) {
    m_texture = other.m_texture;
    return *this;
}

// TODO: see DynamicText::~DynamicText()
ASCIIFontAtlas::~ASCIIFontAtlas() {
    if (IsTextureValid(m_texture))
        UnloadTexture(m_texture);
}

bool ASCIIFontAtlas::LoadGlyphs(FT_Face face) {
    // create ASCII-ish font atlas as a default rendering fallback
    // characters of interest are 0x20-0x7E
    // question mark will be the "unknown character marker" (0x3F)
    // 1. figure out texture size
    // idk lets do rows of 16 or something
    // i *suppose* the closer to square a texture is the better it is for sampling
    constexpr char charMin = 0x20;
    constexpr char charMax = 0x7E;
    constexpr char numCols = 16;
    constexpr char numRows = 6;

    FT_Pos maxWidth = 0;
    FT_Pos maxHeight = 0;

    for (char i = charMin; i <= charMax; i++) {
        const FT_UInt glyphIndex = FT_Get_Char_Index(face, i);
        FT_Error err = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (err) {
            // FTPrintError(err);
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

    std::vector<uint8_t> bitmap(4 * atlasWidth * atlasHeight + 18, 0);
    const auto OffsetOf = [atlasWidth](int x, int y) {
        return (4 * y * atlasWidth) + (4 * x) + 18;
    };
    bitmap[2] = 2;                              // Non-compressed true color
    bitmap[12] = atlasWidth & 0x00FF;           // TGA is a little-endian format
    bitmap[13] = (atlasWidth & 0xFF00) >> 8;
    bitmap[14] = atlasHeight & 0x00FF;
    bitmap[15] = (atlasHeight & 0xFF00) >> 8;    
    bitmap[16] = 32;                            // 32 bits per pixel (BGRA)

    // 2. draw freetype bitmaps and mark their positions
    constexpr auto ASCIIToGlyph = [](char ch) -> char {
        constexpr char fallback = '?' - charMin;
        if (ch < charMin) return fallback;
        if (ch > charMax) return fallback;
        return ch - charMin;
    };
    std::array<AtlasGlyph, charMax - charMin + 1> glyphLocs;
    using uint = unsigned int;
    for (char i = charMin ; i <= charMax; i++) {
        const char ch = ASCIIToGlyph(i);
        const uint row = ch / numCols;
        const uint col = ch % numCols;
        const uint xOrigin = col * maxWidth;
        const uint yOrigin = (numRows - row - 1) * maxHeight;

        const FT_UInt glyphIndex = FT_Get_Char_Index(face, i);
        FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        const FT_GlyphSlot glyph = face->glyph;
        const FT_Bitmap& bmp = glyph->bitmap;

        for (uint bx = 0; bx < bmp.width; bx++) {
             for (uint by = 0; by < bmp.rows; by++) {
                const uint x = xOrigin + bx;
                // the TGA image is a "y-down" system
                // whereas the FreeType bitmap is a "y-up" system
                const uint y = yOrigin + (bmp.rows - by - 1);
                const uint outOffset = OffsetOf(x, y);
                const uint bitmapOffset = bmp.width * by + bx;

                bitmap[outOffset + 0] |= 0xFF; 
                bitmap[outOffset + 1] |= 0xFF; 
                bitmap[outOffset + 2] |= 0xFF; 
                bitmap[outOffset + 3] |= bmp.buffer[bitmapOffset];
            }
        }

        glyphLocs[ch].x = xOrigin;
        glyphLocs[ch].y = yOrigin;
        glyphLocs[ch].width = bmp.width;
        glyphLocs[ch].height = bmp.rows;
        glyphLocs[ch].penOffsetX = glyph->bitmap_left;
        glyphLocs[ch].penOffsetY = bmp.rows - glyph->bitmap_top;

        if constexpr (DRAW_DEBUG) {
            // [green] draw baseline of each glyph
            for (uint j = glyphLocs[ch].penOffsetX; j < maxWidth; j++) {
                const uint baselineY = glyphLocs[ch].y + glyphLocs[ch].penOffsetY;
                const uint baselineX = glyphLocs[ch].x + j;
                const uint tgaOffset = OffsetOf(baselineX, baselineY);
                bitmap[tgaOffset + 1] = 0xFF;
                bitmap[tgaOffset + 3] = 0xFF;
            }

            // [green, dotted] draw x = left bearing
            for (uint j = glyphLocs[ch].y; j < glyphLocs[ch].y + maxHeight; j += 2) {
                const uint x = glyphLocs[ch].x + glyphLocs[ch].penOffsetX;
                const uint offset = OffsetOf(x, j);
                bitmap[offset + 0] = 0x00;
                bitmap[offset + 1] = 0xFF;
                bitmap[offset + 2] = 0x00;
                bitmap[offset + 3] = 0xFF;
            }

            // [red] draw bounding box (?)
            for (uint j = glyphLocs[ch].x; j < glyphLocs[ch].x + glyphLocs[ch].width; j++) {
                const uint yLo = glyphLocs[ch].y;
                const uint yHi = glyphLocs[ch].y + glyphLocs[ch].height - 1;
                const uint offsetLo = OffsetOf(j, yLo);
                const uint offsetHi = OffsetOf(j, yHi);
                bitmap[offsetLo + 0] = 0x00;
                bitmap[offsetLo + 1] = 0x00;
                bitmap[offsetLo + 2] = 0xFF;
                bitmap[offsetLo + 3] = 0xFF;
                bitmap[offsetHi + 0] = 0x00;
                bitmap[offsetHi + 1] = 0x00;
                bitmap[offsetHi + 2] = 0xFF;
                bitmap[offsetHi + 3] = 0xFF;
            }
            for (uint j = glyphLocs[ch].y; j < glyphLocs[ch].y + glyphLocs[ch].height; j++) {
                const uint xLo = glyphLocs[ch].x;
                const uint xHi = glyphLocs[ch].x + glyphLocs[ch].width - 1;
                const uint offsetLo = OffsetOf(xLo, j);
                const uint offsetHi = OffsetOf(xHi, j);
                bitmap[offsetLo + 0] = 0x00;
                bitmap[offsetLo + 1] = 0x00;
                bitmap[offsetLo + 2] = 0xFF;
                bitmap[offsetLo + 3] = 0xFF;
                bitmap[offsetHi + 0] = 0x00;
                bitmap[offsetHi + 1] = 0x00;
                bitmap[offsetHi + 2] = 0xFF;
                bitmap[offsetHi + 3] = 0xFF;
            }
        }
    }

    Image image = LoadImageFromMemory(".tga", bitmap.data(), bitmap.size());
    m_texture = LoadTextureFromImage(image);
    UnloadImage(image);

    return true;
}

void ASCIIFontAtlas::DrawString(std::string_view str, raqm_t* rq) const {}
Texture& ASCIIFontAtlas::RaylibTexture() { return m_texture; }
