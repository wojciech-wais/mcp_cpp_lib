#include <gtest/gtest.h>
#include "mcp/types.hpp"

using namespace mcp;

// Test the PaginatedResult template
TEST(Pagination, EmptyResult) {
    PaginatedResult<ToolDefinition> result;
    EXPECT_TRUE(result.items.empty());
    EXPECT_FALSE(result.next_cursor.has_value());
}

TEST(Pagination, ResultWithCursor) {
    PaginatedResult<ResourceDefinition> result;
    ResourceDefinition rd;
    rd.uri = "file:///test";
    rd.name = "Test";
    result.items.push_back(rd);
    result.next_cursor = "50";

    EXPECT_EQ(result.items.size(), 1u);
    ASSERT_TRUE(result.next_cursor.has_value());
    EXPECT_EQ(*result.next_cursor, "50");
}

TEST(Pagination, Equality) {
    PaginatedResult<ToolDefinition> r1, r2;
    ToolDefinition td;
    td.name = "tool";
    td.input_schema = nlohmann::json::object();
    r1.items.push_back(td);
    r2.items.push_back(td);
    EXPECT_EQ(r1, r2);

    r1.next_cursor = "50";
    EXPECT_NE(r1, r2);
}
