#pragma once

#include <string>

class TokenCounter {
public:
    static int estimate_for_model(const std::string& text, const std::string& model);
};
