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

class TGAImage {
 public:
    //
 private:
    //
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

    void DrawString(std::string_view str, raqm_t* rq) const;
    Texture& RaylibTexture();

 private:
    Texture m_texture;
};
