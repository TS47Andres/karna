#include "token/counter.h"

int TokenCounter::estimate_for_model(const std::string& text, const std::string& /*model*/)
{
    int tokens = 0;
    bool prev_space = true;

    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\t') {
            prev_space = true;
        } else {
            if (prev_space) {
                ++tokens;
            }
            prev_space = false;
        }
    }

    return std::max(1, static_cast<int>(tokens * 1.3));
}
