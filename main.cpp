#include <raylib.h>
#define CLAY_IMPLEMENTATION
#include <clay.h>
#include <cstdio>
#include <cmath>
#include <cstring>

////////////////////////////////////////////////////////////////////////////////
// Custom raylib functions for string view text buffers 
// Raysan has explicitly stated he will not be supporting these.
// While I can understand that from an API perspective,
// you're not gonna believe how stupidly easy these are to implement...
////////////////////////////////////////////////////////////////////////////////

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

Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    // Measure string size for Font
    Clay_Dimensions textSize = { 0 };

    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;

    float textHeight = config->fontSize;
    Font* fonts = (Font*)userData;
    Font fontToUse = fonts[config->fontId];
    // Font failed to load, likely the fonts are in the wrong place relative to the execution dir
    if (!fontToUse.glyphs) return textSize;

    float scaleFactor = config->fontSize/static_cast<float>(fontToUse.baseSize);

    for (int i = 0; i < text.length; ++i)
    {
        if (text.chars[i] == '\n') {
            maxTextWidth = fmax(maxTextWidth, lineTextWidth);
            lineTextWidth = 0;
            continue;
        }
        int index = text.chars[i] - 32;
        if (fontToUse.glyphs[index].advanceX != 0) lineTextWidth += fontToUse.glyphs[index].advanceX;
        else lineTextWidth += (fontToUse.recs[index].width + fontToUse.glyphs[index].offsetX);
    }

    maxTextWidth = fmax(maxTextWidth, lineTextWidth);

    textSize.width = maxTextWidth * scaleFactor;
    textSize.height = textHeight;

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
    float scaleFactor = fontSize / font.baseSize;

    for (int i = 0; i < textLen; ) {
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') {
            textOffsetX = 0.0f;
            textOffsetY += (fontSize + textLineSpacing);
        } else {
            if ((codepoint != ' ') && (codepoint != '\t')) {
                Vector2 glyphPos{ position.x + textOffsetX, position.y + textOffsetY };
                DrawTextCodepoint(font, codepoint, glyphPos, fontSize, tint);
            }

            if (font.glyphs[index].advanceX == 0)
                textOffsetX += static_cast<float>(font.recs[index].width * scaleFactor + spacing);
            else
                textOffsetX += static_cast<float>(font.glyphs[index].advanceX * scaleFactor + spacing);
        }

        i += codepointByteCount;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Rendering Commands
////////////////////////////////////////////////////////////////////////////////

constexpr Color ClayToRaylibColor(Clay_Color& color) {
    return Color{
        .r = static_cast<unsigned char>(std::roundf(color.r)),
        .g = static_cast<unsigned char>(std::roundf(color.g)),
        .b = static_cast<unsigned char>(std::roundf(color.b)),
        .a = static_cast<unsigned char>(std::roundf(color.a))
    };
};

constexpr Clay_Vector2 RaylibToClayVector2(Vector2 vector) {
    return Clay_Vector2{
        .x = vector.x,
        .y = vector.y
    };
}

typedef struct
{
    Clay_Vector2 clickOrigin;
    Clay_Vector2 positionOrigin;
    bool mouseDown;
} ScrollbarData;

ScrollbarData scrollbarData = {0};

bool debugEnabled = false;
constexpr uint32_t FONT_ID_BODY_24 = 0;
constexpr uint32_t FONT_ID_BODY_16 = 1;
constexpr Clay_Color COLOR_ORANGE{225, 138, 50, 255};
constexpr Clay_Color COLOR_BLUE{111, 173, 162, 255};

Texture2D profilePicture;

Clay_String profileText = CLAY_STRING_CONST("Profile Page one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen");
Clay_TextElementConfig headerTextConfig = { .textColor = {0,0,0,255}, .fontId = 1, .fontSize = 16 };

void HandleHeaderButtonInteraction(Clay_ElementId, Clay_PointerData pointerData, intptr_t) {
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        // Do some click handling
    }
}

Clay_ElementDeclaration HeaderButtonStyle(bool hovered) {
    return Clay_ElementDeclaration{
        .layout = {.padding = {16, 16, 8, 8}},
        .backgroundColor = hovered ? COLOR_ORANGE : COLOR_BLUE,
    };
}

// Examples of re-usable "Components"
void RenderHeaderButton(Clay_String text) {
    CLAY(HeaderButtonStyle(Clay_Hovered())) {
        CLAY_TEXT(text, CLAY_TEXT_CONFIG(headerTextConfig));
    }
}

Clay_LayoutConfig dropdownTextItemLayout = { .padding = {8, 8, 4, 4} };
Clay_TextElementConfig dropdownTextElementConfig = { .textColor = {255,255,255,255}, .fontId = 0, .fontSize = 24 };

void RenderDropdownTextItem(int) {
    CLAY({ .layout = dropdownTextItemLayout, .backgroundColor = {180, 180, 180, 255} }) {
        CLAY_TEXT(CLAY_STRING("I'm a text field in a scroll container."), &dropdownTextElementConfig);
    }
}

Clay_RenderCommandArray CreateLayout() {
    Clay_BeginLayout();
    CLAY({ .id = CLAY_ID("OuterContainer"),
           .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                   .height = CLAY_SIZING_GROW(0) },
                       .padding = { 16, 16, 16, 16 },
                       .childGap = 16 },
           .backgroundColor = {200, 200, 200, 255} }) {
        CLAY({ .id = CLAY_ID("SideBar"),
               .layout = { .sizing = { .width = CLAY_SIZING_FIXED(300),
                                       .height = CLAY_SIZING_GROW(0) },
                           .padding = {16, 16, 16, 16 },
                           .childGap = 16,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM },
               .backgroundColor = {150, 150, 255, 255} }) {
            CLAY({ .id = CLAY_ID("ProfilePictureOuter"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                               .padding = { 8, 8, 8, 8 },
                               .childGap = 8,
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = {130, 130, 255, 255} }) {
                CLAY({ .id = CLAY_ID("ProfilePicture"),
                       .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60),
                                   .height = CLAY_SIZING_FIXED(60) } },
                       .image = { .imageData = &profilePicture,
                       .sourceDimensions = {60, 60} }}) {}
                CLAY_TEXT(profileText, CLAY_TEXT_CONFIG({ .textColor = {0, 0, 0, 255},
                                                          .fontSize = 24,
                                                          .textAlignment = CLAY_TEXT_ALIGN_RIGHT }));
            }
            CLAY({ .id = CLAY_ID("SidebarBlob1"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_FIXED(50) }},
                   .backgroundColor = {110, 110, 255, 255} }) {}
            CLAY({ .id = CLAY_ID("SidebarBlob2"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_FIXED(50) }},
                   .backgroundColor = {110, 110, 255, 255} }) {}
            CLAY({ .id = CLAY_ID("SidebarBlob3"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_FIXED(50) }},
                   .backgroundColor = {110, 110, 255, 255} }) {}
            CLAY({ .id = CLAY_ID("SidebarBlob4"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                           .height = CLAY_SIZING_FIXED(50) }},
                   .backgroundColor = {110, 110, 255, 255} }) {}
        }
        CLAY({ .id = CLAY_ID("RightPanel"),
               .layout = { .sizing = { .width = CLAY_SIZING_GROW(0),
                                       .height = CLAY_SIZING_GROW(0) },
                           .childGap = 16,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                               .padding = {8, 8, 8, 8 },
                               .childGap = 8,
                               .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } },
                   .backgroundColor =  {180, 180, 180, 255} }) {
                RenderHeaderButton(CLAY_STRING("Header Item 1"));
                RenderHeaderButton(CLAY_STRING("Header Item 2"));
                RenderHeaderButton(CLAY_STRING("Header Item 3"));
            }
            CLAY({ .id = CLAY_ID("MainContent"),
                   .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                               .padding = {16, 16, 16, 16},
                               .childGap = 16,
                               .layoutDirection = CLAY_TOP_TO_BOTTOM },
                   .backgroundColor = {200, 200, 255, 255},
                   .scroll = { .vertical = true },
            })
            {
                 CLAY({ .id = CLAY_ID("FloatingContainer"),
                        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(300),
                                                .height = CLAY_SIZING_FIXED(300) },
                                    .padding = { 16, 16, 16, 16 } },
                        .backgroundColor = { 140, 80, 200, 200 },
                        .floating = { .offset = {0, 0},
                                      .zIndex = 1,
                                      .attachPoints = { CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP },
                                      .attachTo = CLAY_ATTACH_TO_PARENT, },
                        .border = { .color = {80, 80, 80, 255},
                                    .width = CLAY_BORDER_OUTSIDE(2) },
                 }) {
                     CLAY_TEXT(CLAY_STRING("I'm an inline floating container."),
                               CLAY_TEXT_CONFIG({ .textColor = {255,255,255,255},
                                                  .fontSize = 24 }));
                 }

                 CLAY_TEXT(CLAY_STRING("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt."),
                           CLAY_TEXT_CONFIG({ .textColor = {0,0,0,255},
                                              .fontId = FONT_ID_BODY_24,
                                              .fontSize = 24  }));

                 CLAY({ .id = CLAY_ID("Photos2"),
                        .layout = { .padding = { 16, 16, 16, 16 },
                                    .childGap = 16 },
                        .backgroundColor = {180, 180, 220, 255} }) {
                     CLAY({ .id = CLAY_ID("Picture4"),
                            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120),
                                                    .height = CLAY_SIZING_FIXED(120) } },
                            .image = { .imageData = &profilePicture,
                                       .sourceDimensions = {120, 120} }}) {}
                     CLAY({ .id = CLAY_ID("Picture5"),
                            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120),
                                                    .height = CLAY_SIZING_FIXED(120) }},
                            .image = { .imageData = &profilePicture,
                                       .sourceDimensions = {120, 120} }}) {}
                     CLAY({ .id = CLAY_ID("Picture6"),
                            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120),
                                        .height = CLAY_SIZING_FIXED(120) }},
                            .image = { .imageData = &profilePicture,
                                       .sourceDimensions = {120, 120} }}) {}
                 }

                 CLAY_TEXT(CLAY_STRING("Faucibus purus in massa tempor nec. Nec ullamcorper sit amet risus nullam eget felis eget nunc. Diam vulputate ut pharetra sit amet aliquam id diam. Lacus suspendisse faucibus interdum posuere lorem. A diam sollicitudin tempor id. Amet massa vitae tortor condimentum lacinia. Aliquet nibh praesent tristique magna."),
                           CLAY_TEXT_CONFIG({ .textColor = {0,0,0,255},
                                              .fontSize = 24,
                                              .lineHeight = 60,
                                              .textAlignment = CLAY_TEXT_ALIGN_CENTER }));

                 CLAY_TEXT(CLAY_STRING("Suspendisse in est ante in nibh. Amet venenatis urna cursus eget nunc scelerisque viverra. Elementum sagittis vitae et leo duis ut diam quam nulla. Enim nulla aliquet porttitor lacus. Pellentesque habitant morbi tristique senectus et. Facilisi nullam vehicula ipsum a arcu cursus vitae.\nSem fringilla ut morbi tincidunt. Euismod quis viverra nibh cras pulvinar mattis nunc sed. Velit sed ullamcorper morbi tincidunt ornare massa. Varius quam quisque id diam vel quam. Nulla pellentesque dignissim enim sit amet venenatis. Enim lobortis scelerisque fermentum dui faucibus in. Pretium viverra suspendisse potenti nullam ac tortor vitae. Lectus vestibulum mattis ullamcorper velit sed. Eget mauris pharetra et ultrices neque ornare aenean euismod elementum. Habitant morbi tristique senectus et. Integer vitae justo eget magna fermentum iaculis eu. Semper quis lectus nulla at volutpat diam. Enim praesent elementum facilisis leo. Massa vitae tortor condimentum lacinia quis vel."),
                           CLAY_TEXT_CONFIG({ .textColor = {0,0,0,255},
                                              .fontSize = 24, }));
                 CLAY({ .id = CLAY_ID("Photos"),
                        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) },
                        .padding = {16, 16, 16, 16},
                        .childGap = 16,
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                        .y = CLAY_ALIGN_Y_CENTER },   },
                        .backgroundColor = {180, 180, 220, 255} }) {
                     CLAY({ .id = CLAY_ID("Picture2"),
                            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120),
                            .height = CLAY_SIZING_FIXED(120) }},
                            .image = { .imageData = &profilePicture,
                            .sourceDimensions = {120, 120} }}) {}
                     CLAY({ .id = CLAY_ID("Picture1"),
                            .layout = { .padding = {8, 8, 8, 8},
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = {170, 170, 220, 255} }) {
                         CLAY({ .id = CLAY_ID("ProfilePicture2"),
                                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60),
                                .height = CLAY_SIZING_FIXED(60) }},
                                .image = { .imageData = &profilePicture,
                                .sourceDimensions = {60, 60} }}) {}
                         CLAY_TEXT(CLAY_STRING("Image caption below"), CLAY_TEXT_CONFIG({ .textColor = {0,0,0,255}, .fontSize = 24 }));
                     }
                     CLAY({ .id = CLAY_ID("Picture3"),
                            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120),
                            .height = CLAY_SIZING_FIXED(120) }},
                            .image = { .imageData = &profilePicture,
                            .sourceDimensions = {120, 120} }}) {}
                 }

                 CLAY_TEXT(CLAY_STRING("Amet cursus sit amet dictum sit amet justo donec. Et malesuada fames ac turpis egestas maecenas. A lacus vestibulum sed arcu non odio euismod lacinia. Gravida neque convallis a cras. Dui nunc mattis enim ut tellus elementum sagittis vitae et. Orci sagittis eu volutpat odio facilisis mauris. Neque gravida in fermentum et sollicitudin ac orci. Ultrices dui sapien eget mi proin sed libero. Euismod quis viverra nibh cras pulvinar mattis. Diam volutpat commodo sed egestas egestas. In fermentum posuere urna nec tincidunt praesent semper. Integer eget aliquet nibh praesent tristique magna.\nId cursus metus aliquam eleifend mi in. Sed pulvinar proin gravida hendrerit lectus a. Etiam tempor orci eu lobortis elementum nibh tellus. Nullam vehicula ipsum a arcu cursus vitae. Elit scelerisque mauris pellentesque pulvinar pellentesque habitant morbi tristique senectus. Condimentum lacinia quis vel eros donec ac odio. Mattis pellentesque id nibh tortor id aliquet lectus. Turpis egestas integer eget aliquet nibh praesent tristique. Porttitor massa id neque aliquam vestibulum morbi. Mauris commodo quis imperdiet massa tincidunt nunc pulvinar sapien et. Nunc scelerisque viverra mauris in aliquam sem fringilla. Suspendisse ultrices gravida dictum fusce ut placerat orci nulla.\nLacus laoreet non curabitur gravida arcu ac tortor dignissim. Urna nec tincidunt praesent semper feugiat nibh sed pulvinar. Tristique senectus et netus et malesuada fames ac. Nunc aliquet bibendum enim facilisis gravida. Egestas maecenas pharetra convallis posuere morbi leo urna molestie. Sapien nec sagittis aliquam malesuada bibendum arcu vitae elementum curabitur. Ac turpis egestas maecenas pharetra convallis posuere morbi leo urna. Viverra vitae congue eu consequat. Aliquet enim tortor at auctor urna. Ornare massa eget egestas purus viverra accumsan in nisl nisi. Elit pellentesque habitant morbi tristique senectus et netus et malesuada.\nSuspendisse ultrices gravida dictum fusce ut placerat orci nulla pellentesque. Lobortis feugiat vivamus at augue eget arcu. Vitae justo eget magna fermentum iaculis eu. Gravida rutrum quisque non tellus orci. Ipsum faucibus vitae aliquet nec. Nullam non nisi est sit amet. Nunc consequat interdum varius sit amet mattis vulputate enim. Sem fringilla ut morbi tincidunt augue interdum. Vitae purus faucibus ornare suspendisse. Massa tincidunt nunc pulvinar sapien et. Fringilla ut morbi tincidunt augue interdum velit euismod in. Donec massa sapien faucibus et. Est placerat in egestas erat imperdiet. Gravida rutrum quisque non tellus. Morbi non arcu risus quis varius quam quisque id diam. Habitant morbi tristique senectus et netus et malesuada fames ac. Eget lorem dolor sed viverra.\nOrnare massa eget egestas purus viverra. Varius vel pharetra vel turpis nunc eget lorem. Consectetur purus ut faucibus pulvinar elementum. Placerat in egestas erat imperdiet sed euismod nisi. Interdum velit euismod in pellentesque massa placerat duis ultricies lacus. Aliquam nulla facilisi cras fermentum odio eu. Est pellentesque elit ullamcorper dignissim cras tincidunt. Nunc sed id semper risus in hendrerit gravida rutrum. A pellentesque sit amet porttitor eget dolor morbi. Pellentesque habitant morbi tristique senectus et netus et malesuada fames. Nisl nunc mi ipsum faucibus vitae aliquet nec ullamcorper. Sed id semper risus in hendrerit gravida. Tincidunt praesent semper feugiat nibh. Aliquet lectus proin nibh nisl condimentum id venenatis a. Enim sit amet venenatis urna cursus eget. In egestas erat imperdiet sed euismod nisi porta lorem mollis. Lacinia quis vel eros donec ac odio tempor orci. Donec pretium vulputate sapien nec sagittis aliquam malesuada bibendum arcu. Erat pellentesque adipiscing commodo elit at.\nEgestas sed sed risus pretium quam vulputate. Vitae congue mauris rhoncus aenean vel elit scelerisque mauris pellentesque. Aliquam malesuada bibendum arcu vitae elementum. Congue mauris rhoncus aenean vel elit scelerisque mauris. Pellentesque dignissim enim sit amet venenatis urna cursus. Et malesuada fames ac turpis egestas sed tempus urna. Vel fringilla est ullamcorper eget nulla facilisi etiam dignissim. Nibh cras pulvinar mattis nunc sed blandit libero. Fringilla est ullamcorper eget nulla facilisi etiam dignissim. Aenean euismod elementum nisi quis eleifend quam adipiscing vitae proin. Mauris pharetra et ultrices neque ornare aenean euismod elementum. Ornare quam viverra orci sagittis eu. Odio ut sem nulla pharetra diam sit amet nisl suscipit. Ornare lectus sit amet est. Ullamcorper sit amet risus nullam eget. Tincidunt lobortis feugiat vivamus at augue eget arcu dictum.\nUrna nec tincidunt praesent semper feugiat nibh. Ut venenatis tellus in metus vulputate eu scelerisque felis. Cursus risus at ultrices mi tempus. In pellentesque massa placerat duis ultricies lacus sed turpis. Platea dictumst quisque sagittis purus. Cras adipiscing enim eu turpis egestas. Egestas sed tempus urna et pharetra pharetra. Netus et malesuada fames ac turpis egestas integer eget aliquet. Ac turpis egestas sed tempus. Sed lectus vestibulum mattis ullamcorper velit sed. Ante metus dictum at tempor commodo ullamcorper a. Augue neque gravida in fermentum et sollicitudin ac. Praesent semper feugiat nibh sed pulvinar proin gravida. Metus aliquam eleifend mi in nulla posuere sollicitudin aliquam ultrices. Neque gravida in fermentum et sollicitudin ac orci phasellus egestas.\nRidiculus mus mauris vitae ultricies. Morbi quis commodo odio aenean. Duis ultricies lacus sed turpis. Non pulvinar neque laoreet suspendisse interdum consectetur. Scelerisque eleifend donec pretium vulputate sapien nec sagittis aliquam. Volutpat est velit egestas dui id ornare arcu odio ut. Viverra tellus in hac habitasse platea dictumst vestibulum rhoncus est. Vestibulum lectus mauris ultrices eros. Sed blandit libero volutpat sed cras ornare. Id leo in vitae turpis massa sed elementum tempus. Gravida dictum fusce ut placerat orci nulla pellentesque. Pretium quam vulputate dignissim suspendisse in. Nisl suscipit adipiscing bibendum est ultricies integer quis auctor. Risus viverra adipiscing at in tellus. Turpis nunc eget lorem dolor sed viverra ipsum. Senectus et netus et malesuada fames ac. Habitasse platea dictumst vestibulum rhoncus est. Nunc sed id semper risus in hendrerit gravida. Felis eget velit aliquet sagittis id. Eget felis eget nunc lobortis.\nMaecenas pharetra convallis posuere morbi leo. Maecenas volutpat blandit aliquam etiam. A condimentum vitae sapien pellentesque habitant morbi tristique senectus et. Pulvinar mattis nunc sed blandit libero volutpat sed. Feugiat in ante metus dictum at tempor commodo ullamcorper. Vel pharetra vel turpis nunc eget lorem dolor. Est placerat in egestas erat imperdiet sed euismod. Quisque non tellus orci ac auctor augue mauris augue. Placerat vestibulum lectus mauris ultrices eros in cursus turpis. Enim nunc faucibus a pellentesque sit. Adipiscing vitae proin sagittis nisl. Iaculis at erat pellentesque adipiscing commodo elit at imperdiet. Aliquam sem fringilla ut morbi.\nArcu odio ut sem nulla pharetra diam sit amet nisl. Non diam phasellus vestibulum lorem sed. At erat pellentesque adipiscing commodo elit at. Lacus luctus accumsan tortor posuere ac ut consequat. Et malesuada fames ac turpis egestas integer. Tristique magna sit amet purus. A condimentum vitae sapien pellentesque habitant. Quis varius quam quisque id diam vel quam. Est ullamcorper eget nulla facilisi etiam dignissim diam quis. Augue interdum velit euismod in pellentesque massa. Elit scelerisque mauris pellentesque pulvinar pellentesque habitant. Vulputate eu scelerisque felis imperdiet. Nibh tellus molestie nunc non blandit massa. Velit euismod in pellentesque massa placerat. Sed cras ornare arcu dui. Ut sem viverra aliquet eget sit. Eu lobortis elementum nibh tellus molestie nunc non. Blandit libero volutpat sed cras ornare arcu dui vivamus.\nSit amet aliquam id diam maecenas. Amet risus nullam eget felis eget nunc lobortis mattis aliquam. Magna sit amet purus gravida. Egestas purus viverra accumsan in nisl nisi. Leo duis ut diam quam. Ante metus dictum at tempor commodo ullamcorper. Ac turpis egestas integer eget. Fames ac turpis egestas integer eget aliquet nibh. Sem integer vitae justo eget magna fermentum. Semper auctor neque vitae tempus quam pellentesque nec nam aliquam. Vestibulum mattis ullamcorper velit sed. Consectetur adipiscing elit duis tristique sollicitudin nibh. Massa id neque aliquam vestibulum morbi blandit cursus risus.\nCursus sit amet dictum sit amet justo donec enim diam. Egestas erat imperdiet sed euismod. Nullam vehicula ipsum a arcu cursus vitae congue mauris. Habitasse platea dictumst vestibulum rhoncus est pellentesque elit. Duis ultricies lacus sed turpis tincidunt id aliquet risus feugiat. Faucibus ornare suspendisse sed nisi lacus sed viverra. Pretium fusce id velit ut tortor pretium viverra. Fermentum odio eu feugiat pretium nibh ipsum consequat nisl vel. Senectus et netus et malesuada. Tellus pellentesque eu tincidunt tortor aliquam. Aenean sed adipiscing diam donec adipiscing tristique risus nec feugiat. Quis vel eros donec ac odio. Id interdum velit laoreet id donec ultrices tincidunt.\nMassa id neque aliquam vestibulum morbi blandit cursus risus at. Enim tortor at auctor urna nunc id cursus metus. Lorem ipsum dolor sit amet consectetur. At quis risus sed vulputate odio. Facilisis mauris sit amet massa vitae tortor condimentum lacinia quis. Et malesuada fames ac turpis egestas maecenas. Bibendum arcu vitae elementum curabitur vitae nunc sed velit dignissim. Viverra orci sagittis eu volutpat odio facilisis mauris. Adipiscing bibendum est ultricies integer quis auctor elit sed. Neque viverra justo nec ultrices dui sapien. Elementum nibh tellus molestie nunc non blandit massa enim. Euismod elementum nisi quis eleifend quam adipiscing vitae proin sagittis. Faucibus ornare suspendisse sed nisi. Quis viverra nibh cras pulvinar mattis nunc sed blandit. Tristique senectus et netus et. Magnis dis parturient montes nascetur ridiculus mus.\nDolor magna eget est lorem ipsum dolor. Nibh sit amet commodo nulla. Donec pretium vulputate sapien nec sagittis aliquam malesuada. Cras adipiscing enim eu turpis egestas pretium. Cras ornare arcu dui vivamus arcu felis bibendum ut tristique. Mus mauris vitae ultricies leo integer. In nulla posuere sollicitudin aliquam ultrices sagittis orci. Quis hendrerit dolor magna eget. Nisl tincidunt eget nullam non. Vitae congue eu consequat ac felis donec et odio. Vivamus at augue eget arcu dictum varius duis at. Ornare quam viverra orci sagittis.\nErat nam at lectus urna duis convallis. Massa placerat duis ultricies lacus sed turpis tincidunt id aliquet. Est ullamcorper eget nulla facilisi etiam dignissim diam. Arcu vitae elementum curabitur vitae nunc sed velit dignissim sodales. Tortor vitae purus faucibus ornare suspendisse sed nisi lacus. Neque viverra justo nec ultrices dui sapien eget mi proin. Viverra accumsan in nisl nisi scelerisque eu ultrices. Consequat interdum varius sit amet mattis. In aliquam sem fringilla ut morbi. Eget arcu dictum varius duis at. Nulla aliquet porttitor lacus luctus accumsan tortor posuere. Arcu bibendum at varius vel pharetra vel turpis. Hac habitasse platea dictumst quisque sagittis purus sit amet. Sapien eget mi proin sed libero enim sed. Quam elementum pulvinar etiam non quam lacus suspendisse faucibus interdum. Semper viverra nam libero justo. Fusce ut placerat orci nulla pellentesque dignissim enim sit amet. Et malesuada fames ac turpis egestas maecenas pharetra convallis posuere.\nTurpis egestas sed tempus urna et pharetra pharetra massa. Gravida in fermentum et sollicitudin ac orci phasellus. Ornare suspendisse sed nisi lacus sed viverra tellus in. Fames ac turpis egestas maecenas pharetra convallis posuere. Mi proin sed libero enim sed faucibus turpis. Sit amet mauris commodo quis imperdiet massa tincidunt nunc. Ut etiam sit amet nisl purus in mollis nunc. Habitasse platea dictumst quisque sagittis purus sit amet volutpat consequat. Eget aliquet nibh praesent tristique magna. Sit amet est placerat in egestas erat. Commodo sed egestas egestas fringilla. Enim nulla aliquet porttitor lacus luctus accumsan tortor posuere ac. Et molestie ac feugiat sed lectus vestibulum mattis ullamcorper. Dignissim convallis aenean et tortor at risus viverra. Morbi blandit cursus risus at ultrices mi. Ac turpis egestas integer eget aliquet nibh praesent tristique magna.\nVolutpat sed cras ornare arcu dui. Egestas erat imperdiet sed euismod nisi porta lorem mollis aliquam. Viverra justo nec ultrices dui sapien. Amet risus nullam eget felis eget nunc lobortis. Metus aliquam eleifend mi in. Ut eu sem integer vitae. Auctor elit sed vulputate mi sit amet. Nisl nisi scelerisque eu ultrices. Dictum fusce ut placerat orci nulla. Pellentesque habitant morbi tristique senectus et. Auctor elit sed vulputate mi sit. Tincidunt arcu non sodales neque. Mi in nulla posuere sollicitudin aliquam. Morbi non arcu risus quis varius quam quisque id diam. Cras adipiscing enim eu turpis egestas pretium aenean pharetra magna. At auctor urna nunc id cursus metus aliquam. Mauris a diam maecenas sed enim ut sem viverra. Nunc scelerisque viverra mauris in. In iaculis nunc sed augue lacus viverra vitae congue eu. Volutpat blandit aliquam etiam erat velit scelerisque in dictum non."),
                     CLAY_TEXT_CONFIG({ .textColor = {0,0,0,255}, .fontSize = 24  }));
            }
        }

        CLAY({ .id = CLAY_ID("Blob4Floating2"),
               .floating = {  .parentId = Clay_GetElementId(CLAY_STRING("SidebarBlob4")).id ,
               .zIndex = 1,
               .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,} }) {
            CLAY({ .id = CLAY_ID("ScrollContainer"),
                   .layout = { .sizing = { .height = CLAY_SIZING_FIXED(200) },
                   .childGap = 2 },
                   .scroll = { .vertical = true } }) {
                CLAY({ .id = CLAY_ID("FloatingContainer2"),
                       .floating = { .zIndex = 1,
                       .attachTo = CLAY_ATTACH_TO_PARENT} }) {
                    CLAY({ .id = CLAY_ID("FloatingContainerInner"),
                           .layout = { .sizing = { .width = CLAY_SIZING_FIXED(300),
                           .height = CLAY_SIZING_FIXED(300) },
                           .padding = {16, 16, 16, 16} },
                           .backgroundColor = {140,80, 200, 200} }) {
                        CLAY_TEXT(CLAY_STRING("I'm an inline floating container."), CLAY_TEXT_CONFIG({ .textColor = {255,255,255,255}, .fontSize = 24  }));
                    }
                }
                CLAY({ .id = CLAY_ID("ScrollContainerInner"),
                       .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM },
                       .backgroundColor = {160, 160, 160, 255} }) {
                    for (int i = 0; i < 100; i++) {
                        RenderDropdownTextItem(i);
                    }
                }
            }
        }
        Clay_ScrollContainerData scrollData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("MainContent")));
        if (scrollData.found) {
            CLAY({ .id = CLAY_ID("ScrollBar"),
                .floating = {
                    .offset = { .y = -(scrollData.scrollPosition->y / scrollData.contentDimensions.height) * scrollData.scrollContainerDimensions.height },
                    .parentId = Clay_GetElementId(CLAY_STRING("MainContent")).id,
                    .zIndex = 1,
                    .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                    .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID
                }
            }) {
                CLAY({ .id = CLAY_ID("ScrollBarButton"),
                    .layout = { .sizing = {CLAY_SIZING_FIXED(12), CLAY_SIZING_FIXED((scrollData.scrollContainerDimensions.height / scrollData.contentDimensions.height) * scrollData.scrollContainerDimensions.height) }},
                    .backgroundColor = Clay_PointerOver(Clay__HashString(CLAY_STRING("ScrollBar"), 0, 0)) ? Clay_Color{100, 100, 140, 150} : Clay_Color{120, 120, 160, 150} ,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {}
            }
        }
    }
    return Clay_EndLayout();
}

// Mostly just copied from the example
void Clay_Raylib_Render(Clay_RenderCommandArray renderCommands, Font* fonts) {
    for (int j = 0; j < renderCommands.length; j++) {
        Clay_RenderCommand* renderCommand = Clay_RenderCommandArray_Get(&renderCommands, j);
        const Clay_BoundingBox& bb = renderCommand->boundingBox;
        switch (renderCommand->commandType) {
            // case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            //     // Raylib uses standard C strings so isn't compatible with cheap slices, we need to clone the string to append null terminator
            //     Clay_TextRenderData *textData = &renderCommand->renderData.text;
            //     char *cloned = (char *)malloc(textData->stringContents.length + 1);
            //     memcpy(cloned, textData->stringContents.chars, textData->stringContents.length);
            //     cloned[textData->stringContents.length] = '\0';
            //     Font fontToUse = fonts[textData->fontId];
            //     DrawTextEx(fontToUse, cloned, (Vector2){bb.x, bb.y}, (float)textData->fontSize, (float)textData->letterSpacing, ClayToRaylibColor(textData->textColor));
            //     free(cloned);
            //     break;
            // }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData* textData = &renderCommand->renderData.text;
                DrawTextExN(fonts[textData->fontId],
                            textData->stringContents.chars,
                            textData->stringContents.length,
                            Vector2{bb.x, bb.y},
                            static_cast<float>(textData->fontSize),
                            static_cast<float>(textData->letterSpacing),
                            ClayToRaylibColor(textData->textColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                Texture2D imageTexture = *reinterpret_cast<Texture2D*>(renderCommand->renderData.image.imageData);
                Clay_Color tintColor = renderCommand->renderData.image.backgroundColor;
                if (tintColor.r == 0 && tintColor.g == 0 && tintColor.b == 0 && tintColor.a == 0) {
                    tintColor = Clay_Color{ 255, 255, 255, 255 };
                }
                DrawTextureEx(
                    imageTexture,
                    Vector2{bb.x, bb.y},
                    0,
                    bb.width / static_cast<float>(imageTexture.width),
                    ClayToRaylibColor(tintColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                BeginScissorMode(static_cast<int>(std::roundf(bb.x)), static_cast<int>(std::roundf(bb.y)), static_cast<int>(std::roundf(bb.width)), static_cast<int>(std::roundf(bb.height)));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                EndScissorMode();
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *config = &renderCommand->renderData.rectangle;
                if (config->cornerRadius.topLeft > 0) {
                    float radius = (config->cornerRadius.topLeft * 2) / static_cast<float>((bb.width > bb.height) ? bb.height : bb.width);
                    DrawRectangleRounded(Rectangle{ bb.x, bb.y, bb.width, bb.height }, radius, 8, ClayToRaylibColor(config->backgroundColor));
                } else {
                    DrawRectangle(bb.x, bb.y, bb.width, bb.height, ClayToRaylibColor(config->backgroundColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *config = &renderCommand->renderData.border;
                // Left border
                if (config->width.left > 0) {
                    DrawRectangle(
                        static_cast<int>(std::roundf(bb.x)),
                        static_cast<int>(std::roundf(bb.y + config->cornerRadius.topLeft)),
                        static_cast<int>(config->width.left),
                        static_cast<int>(std::roundf(bb.height - config->cornerRadius.topLeft - config->cornerRadius.bottomLeft)),
                        ClayToRaylibColor(config->color));
                }
                // Right border
                if (config->width.right > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + bb.width - config->width.right)),
                                  static_cast<int>(std::roundf(bb.y + config->cornerRadius.topRight)),
                                  static_cast<int>(config->width.right),
                                  static_cast<int>(std::roundf(bb.height - config->cornerRadius.topRight - config->cornerRadius.bottomRight)),
                                  ClayToRaylibColor(config->color));
                }
                // Top border
                if (config->width.top > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + config->cornerRadius.topLeft)),
                                  static_cast<int>(std::roundf(bb.y)),
                                  static_cast<int>(std::roundf(bb.width - config->cornerRadius.topLeft - config->cornerRadius.topRight)),
                                  static_cast<int>(config->width.top),
                                  ClayToRaylibColor(config->color));
                }
                // Bottom border
                if (config->width.bottom > 0) {
                    DrawRectangle(static_cast<int>(std::roundf(bb.x + config->cornerRadius.bottomLeft)),
                                  static_cast<int>(std::roundf(bb.y + bb.height - config->width.bottom)),
                                  static_cast<int>(std::roundf(bb.width - config->cornerRadius.bottomLeft - config->cornerRadius.bottomRight)),
                                  static_cast<int>(config->width.bottom),
                                  ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.topLeft > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + config->cornerRadius.topLeft), std::roundf(bb.y + config->cornerRadius.topLeft) },
                             std::roundf(config->cornerRadius.topLeft - config->width.top),
                             config->cornerRadius.topLeft,
                             180,
                             270,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.topRight > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + bb.width - config->cornerRadius.topRight), std::roundf(bb.y + config->cornerRadius.topRight) },
                             std::roundf(config->cornerRadius.topRight - config->width.top),
                             config->cornerRadius.topRight,
                             270,
                             360,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.bottomLeft > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + config->cornerRadius.bottomLeft), std::roundf(bb.y + bb.height - config->cornerRadius.bottomLeft) },
                             std::roundf(config->cornerRadius.bottomLeft - config->width.top),
                             config->cornerRadius.bottomLeft,
                             90,
                             180,
                             10,
                             ClayToRaylibColor(config->color));
                }
                if (config->cornerRadius.bottomRight > 0) {
                    DrawRing(Vector2{std::roundf(bb.x + bb.width - config->cornerRadius.bottomRight), std::roundf(bb.y + bb.height - config->cornerRadius.bottomRight) },
                             std::roundf(config->cornerRadius.bottomRight - config->width.bottom),
                             config->cornerRadius.bottomRight,
                             0.1,
                             90,
                             10,
                             ClayToRaylibColor(config->color));
                }
                break;
            }
            default: {
                printf("Unhandled render command.\n");
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Main Function
////////////////////////////////////////////////////////////////////////////////

void ClayError(Clay_ErrorData errorData) {
    printf("%s", errorData.errorText.chars);
}

int main() {
    const uint64_t clayArenaSize = Clay_MinMemorySize();
    Clay_Arena clayArena = Clay_CreateArenaWithCapacityAndMemory(clayArenaSize, malloc(clayArenaSize));
    Clay_Dimensions clayDim{
        .width = static_cast<float>(GetScreenWidth()),
        .height = static_cast<float>(GetScreenHeight())
    };
    Clay_ErrorHandler clayErr{
        .errorHandlerFunction = ClayError,
        .userData = nullptr
    };
    Clay_Initialize(clayArena, clayDim, clayErr);

    // SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(1024, 768, "[Riff Man]");
    InitAudioDevice();

    Music music = LoadMusicStream("test.mp3");
    PlayMusicStream(music);
    SetTargetFPS(60);

    profilePicture = LoadTextureFromImage(LoadImage("resources/profile-picture.png"));

    Font fonts[2];
    fonts[0] = LoadFontEx("resources/Roboto-Regular.ttf", 48, 0, 400);
    SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);
    fonts[1] = LoadFontEx("resources/Roboto-Regular.ttf", 32, 0, 400);
    SetTextureFilter(fonts[1].texture, TEXTURE_FILTER_BILINEAR);
    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    double totalLayoutTime = 0.0;
    int totalIterations = 0;

    while (!WindowShouldClose()) {
        UpdateMusicStream(music);

        Vector2 mouseWheelDelta = GetMouseWheelMoveV();
        float mouseWheelX = mouseWheelDelta.x;
        float mouseWheelY = mouseWheelDelta.y;

        if (IsKeyPressed(KEY_D)) {
            debugEnabled = !debugEnabled;
            Clay_SetDebugModeEnabled(debugEnabled);
        }
        //----------------------------------------------------------------------------------
        // Handle scroll containers
        Clay_Vector2 mousePosition = RaylibToClayVector2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0) && !scrollbarData.mouseDown);
        Clay_SetLayoutDimensions(Clay_Dimensions{ static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) });
        if (!IsMouseButtonDown(0)) {
            scrollbarData.mouseDown = false;
        }

        if (IsMouseButtonDown(0) && !scrollbarData.mouseDown && Clay_PointerOver(Clay__HashString(CLAY_STRING("ScrollBar"), 0, 0))) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay__HashString(CLAY_STRING("MainContent"), 0, 0));
            scrollbarData.clickOrigin = mousePosition;
            scrollbarData.positionOrigin = *scrollContainerData.scrollPosition;
            scrollbarData.mouseDown = true;
        } else if (scrollbarData.mouseDown) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay__HashString(CLAY_STRING("MainContent"), 0, 0));
            if (scrollContainerData.contentDimensions.height > 0) {
                Clay_Vector2 ratio{
                    scrollContainerData.contentDimensions.width / scrollContainerData.scrollContainerDimensions.width,
                    scrollContainerData.contentDimensions.height / scrollContainerData.scrollContainerDimensions.height,
                };
                if (scrollContainerData.config.vertical) {
                    scrollContainerData.scrollPosition->y = scrollbarData.positionOrigin.y + (scrollbarData.clickOrigin.y - mousePosition.y) * ratio.y;
                }
                if (scrollContainerData.config.horizontal) {
                    scrollContainerData.scrollPosition->x = scrollbarData.positionOrigin.x + (scrollbarData.clickOrigin.x - mousePosition.x) * ratio.x;
                }
            }
        }

        Clay_UpdateScrollContainers(true, Clay_Vector2{mouseWheelX, mouseWheelY}, GetFrameTime());

        double currentTime = GetTime();
        Clay_RenderCommandArray renderCommands = CreateLayout();
        double layoutTime = (GetTime() - currentTime) * 1000.0 * 1000.0;
        totalLayoutTime += layoutTime;
        totalIterations++;
        printf("layout time: %f us\n", layoutTime); 

        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(renderCommands, fonts);
        EndDrawing();
    }

    printf("Avg layout time: %f us", totalLayoutTime / totalIterations);

    UnloadMusicStream(music);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
