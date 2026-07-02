#include "skills/explain.h"

std::string ExplainSkill::name() const { return "explain"; }

std::string ExplainSkill::description() const
{
    return "Explain a code file or snippet in detail. Understands code structure, patterns, and purpose.";
}

json ExplainSkill::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to explain"}
            }},
            {"detail", {
                {"type", "string"},
                {"description", "Detail level: 'high' for overview, 'low' for line-by-line"},
                {"enum", {"high", "low"}},
                {"default", "high"}
            }}
        }},
        {"required", {"path"}}
    };
}

ToolResult ExplainSkill::execute(const json& params)
{
    return ToolResult::ok(
        "The explain skill is implemented as a prompt strategy. "
        "The controller reads the file, sends it to the LLM with an explanation prompt, "
        "and returns the result. This tool definition exists for skill discovery."
    );
}
