#pragma once

#include <string>

// Ignores a warning thrown by some clay internal workings that are irrelevant here
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <clay.h>
#pragma GCC diagnostic pop

#include <raylib.h>


namespace casts {
namespace clay {
namespace {

constexpr Clay_Vector2 Vector2(Vector2 v) {
    return { .x = v.x, .y = v.y };
}

constexpr Clay_String String(const std::string& str) {
    return {
        .length = static_cast<int32_t>(str.size()),
        .chars = str.data() };
}
constexpr Clay_String String(std::string_view str) {
    return {
        .length = static_cast<int32_t>(str.size()),
        .chars = str.data() };
}

}
}

namespace raylib {
namespace {

constexpr Color Color(const Clay_Color& color) {
    static constexpr auto Transform = [](float value) -> unsigned char {
        if (value >= 255.0f) return 255;
        if (value <= 0.0f) return 0;
        return static_cast<unsigned char>(std::roundf(value));
    };
    return {
        .r = Transform(color.r),
        .g = Transform(color.g),
        .b = Transform(color.b),
        .a = Transform(color.a) };
}

}
}
}

