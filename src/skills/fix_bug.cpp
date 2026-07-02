#include "skills/fix_bug.h"

std::string FixBugSkill::name() const { return "fix_bug"; }

std::string FixBugSkill::description() const
{
    return "Diagnose and fix a bug in the codebase. Searches for relevant code, reads it, and applies a fix.";
}

json FixBugSkill::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"description", {
                {"type", "string"},
                {"description", "Description of the bug or unexpected behavior"}
            }},
            {"file_hint", {
                {"type", "string"},
                {"description", "Optional hint about which file(s) may contain the bug"}
            }}
        }},
        {"required", {"description"}}
    };
}

ToolResult FixBugSkill::execute(const json& params)
{
    return ToolResult::ok(
        "The fix_bug skill is implemented as a multi-step prompt strategy. "
        "The controller searches for relevant code, reads it, sends context to the LLM, "
        "and applies the suggested fix. This tool definition exists for skill discovery."
    );
}
