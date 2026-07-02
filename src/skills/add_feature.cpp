#include "skills/add_feature.h"

std::string AddFeatureSkill::name() const { return "add_feature"; }

std::string AddFeatureSkill::description() const
{
    return "Add a new feature to the codebase. Understands the existing code structure and generates appropriate code.";
}

json AddFeatureSkill::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"description", {
                {"type", "string"},
                {"description", "Description of the feature to add"}
            }},
            {"location_hint", {
                {"type", "string"},
                {"description", "Optional hint about where to add the feature"}
            }}
        }},
        {"required", {"description"}}
    };
}

ToolResult AddFeatureSkill::execute(const json& params)
{
    return ToolResult::ok(
        "The add_feature skill is implemented as a multi-step prompt strategy. "
        "The controller explores the codebase, reads relevant files, sends context to the LLM, "
        "and generates new code. This tool definition exists for skill discovery."
    );
}
