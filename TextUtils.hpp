#pragma once

#include <string_view>
#include <vector>

// Ignores a warning thrown by some clay internal workings that are irrelevant here
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <clay.h>
#pragma GCC diagnostic pop
#include <raqm.h>
#include <raylib.h>

void FTPrintError(int error);

struct TGAImage {
    TGAImage(int width, int height);

    int OffsetOf(int x, int y) const;

    std::vector<uint8_t> buffer;
    int width;
    int height;
};

class DynamicText {
 public:
    DynamicText();
    DynamicText(const DynamicText& other);
    DynamicText(DynamicText&& other);
    DynamicText& operator=(const DynamicText& other);
    DynamicText& operator=(DynamicText&& other);
    ~DynamicText();

    void LoadText(std::string_view str, FT_Face face, raqm_t* rq,
                  const char* langHint = nullptr);

    int Width() const;
    int Height() const;
    // const std::vector<uint8_t>& Bitmap() const;
    Clay_Dimensions ClayDimensions() const;
    Texture& RaylibTexture();

 private:
    // std::string text;
    int m_width;
    int m_height;
    // std::vector<uint8_t> m_bitmap;
    Texture m_texture;
};

struct AtlasGlyph {
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    unsigned int penOffsetX;
    unsigned int penOffsetY;
};

class ASCIIFontAtlas {
 public:
    ASCIIFontAtlas();
    ASCIIFontAtlas(const ASCIIFontAtlas& other);
    ASCIIFontAtlas(ASCIIFontAtlas&& other);
    ASCIIFontAtlas& operator=(const ASCIIFontAtlas& other);
    ASCIIFontAtlas& operator=(ASCIIFontAtlas&& other);
    ~ASCIIFontAtlas();

    bool LoadGlyphs(FT_Face face);

    Texture& RaylibTexture();
    int GetMaxAscent() const;
    const AtlasGlyph& GetGlyphLocation(char ch) const;

 private:
    Texture m_texture;
    int m_maxAscent;  // used to find the baseline
    std::vector<AtlasGlyph> m_glyphLocs;
};
