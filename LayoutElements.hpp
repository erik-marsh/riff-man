#pragma once

#include <string_view>

struct CustomElement {
    enum class Type {
        UTF8_TEXT_SCISSOR
    };

    Type type;
    // union {
    //     // no data required for UTF8_TEXT_SCISSOR
    // };
};

