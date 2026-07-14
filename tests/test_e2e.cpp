#include <gtest/gtest.h>
#include "config/config.h"
#include "io/file_ops.h"
#include "project/context.h"
#include "tools/registry.h"
#include "tools/read.h"
#include "tools/write.h"
#include <filesystem>

// Test config loading and roundtrip
TEST(KarnaE2ETest, ConfigRoundtrip) {
    Config cfg;
    cfg.openrouter.api_key = "gtest-key";
    cfg.openrouter.default_model = "gtest-model";
    cfg.save("gtest_config.toml");

    Config loaded = Config::load_from_file("gtest_config.toml");
    EXPECT_EQ(loaded.openrouter.api_key, "gtest-key");
    EXPECT_EQ(loaded.openrouter.default_model, "gtest-model");

    std::filesystem::remove("gtest_config.toml");
}

// Test file operations
TEST(KarnaE2ETest, FileOpsWriteRead) {
    std::string test_file = "gtest_temp_file.txt";
    std::string content = "Hello Google Test E2E";
    
    bool write_ok = FileOps::write_file(test_file, content);
    ASSERT_TRUE(write_ok);
    
    auto read_content = FileOps::read_file(test_file);
    ASSERT_TRUE(read_content.has_value());
    EXPECT_EQ(*read_content, content);
    
    std::filesystem::remove(test_file);
}

// Test Project Context discovery
TEST(KarnaE2ETest, ProjectContextDiscovery) {
    auto ctx = ProjectContext::discover(".");
    EXPECT_FALSE(ctx.root_path.empty());
}

// Test Tool Registry and tool execution
TEST(KarnaE2ETest, ToolRegistryAndExecution) {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<WriteTool>());
    registry.register_tool(std::make_unique<ReadTool>());
    
    Tool* write_tool = registry.find("write");
    ASSERT_NE(write_tool, nullptr);
    EXPECT_EQ(write_tool->name(), "write");
    
    Tool* read_tool = registry.find("read");
    ASSERT_NE(read_tool, nullptr);
    EXPECT_EQ(read_tool->name(), "read");
    
    // Execute write tool
    json write_params = {
        {"path", "gtest_tool_temp.txt"},
        {"content", "tool execution test"}
    };
    ToolResult write_res = write_tool->execute(write_params);
    EXPECT_TRUE(write_res.success);
    
    // Execute read tool
    json read_params = {
        {"path", "gtest_tool_temp.txt"}
    };
    ToolResult read_res = read_tool->execute(read_params);
    EXPECT_TRUE(read_res.success);
    EXPECT_TRUE(read_res.output.find("tool execution test") != std::string::npos);
    
    std::filesystem::remove("gtest_tool_temp.txt");
}
