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
    unsigned int width;
    unsigned int height;
};

struct TextRenderContext;
Texture RenderText(std::string_view str, const TextRenderContext& textCtx,
                   const char* langHint = nullptr);

class ASCIIAtlas {
 public:
    struct GlyphInfo {
        unsigned int x;
        unsigned int y;
        unsigned int width;
        unsigned int height;
        unsigned int penOffsetX;
        unsigned int penOffsetY;
    };

    ASCIIAtlas();
    ASCIIAtlas(const ASCIIAtlas& other);
    ASCIIAtlas(ASCIIAtlas&& other);
    ASCIIAtlas& operator=(const ASCIIAtlas& other);
    ASCIIAtlas& operator=(ASCIIAtlas&& other);
    ~ASCIIAtlas();

    bool LoadGlyphs(FT_Face face);

    Texture& RaylibTexture();
    int GetMaxAscent() const;
    int GetMaxHeight() const;
    const GlyphInfo& GetGlyphLocation(char ch) const;

 private:
    Texture m_texture;
    int m_maxAscent;  // used to find the baseline (in pixels)
    int m_maxHeight;  // max glyph height (in pixels)
    std::vector<GlyphInfo> m_glyphLocs;
};

struct TextRenderContext {
    FT_Face face;  // FT_Face is a typedef of a pointer
    ASCIIAtlas atlas;
    raqm_t* rq;
};

