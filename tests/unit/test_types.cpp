#include <gtest/gtest.h>
#include "mcp/types.hpp"
#include <nlohmann/json.hpp>

using namespace mcp;

// ---- TextContent ----

TEST(TextContent, SerializeDeserialize) {
    TextContent tc;
    tc.text = "Hello, world!";

    nlohmann::json j;
    to_json(j, tc);
    EXPECT_EQ(j["type"], "text");
    EXPECT_EQ(j["text"], "Hello, world!");

    TextContent tc2;
    from_json(j, tc2);
    EXPECT_EQ(tc2.text, "Hello, world!");
}

TEST(TextContent, WithAnnotations) {
    TextContent tc;
    tc.text = "annotated";
    tc.annotations = Annotations{};
    tc.annotations->priority = 0.8;
    tc.annotations->audience = {"user"};

    nlohmann::json j;
    to_json(j, tc);
    EXPECT_TRUE(j.contains("annotations"));
    EXPECT_DOUBLE_EQ(j["annotations"]["priority"], 0.8);
}

// ---- ImageContent ----

TEST(ImageContent, SerializeDeserialize) {
    ImageContent ic;
    ic.data = "base64data==";
    ic.mime_type = "image/png";

    nlohmann::json j;
    to_json(j, ic);
    EXPECT_EQ(j["type"], "image");
    EXPECT_EQ(j["data"], "base64data==");
    EXPECT_EQ(j["mimeType"], "image/png");

    ImageContent ic2;
    from_json(j, ic2);
    EXPECT_EQ(ic2, ic);
}

// ---- AudioContent ----

TEST(AudioContent, SerializeDeserialize) {
    AudioContent ac;
    ac.data = "audiodata==";
    ac.mime_type = "audio/wav";

    nlohmann::json j;
    to_json(j, ac);
    EXPECT_EQ(j["type"], "audio");

    AudioContent ac2;
    from_json(j, ac2);
    EXPECT_EQ(ac2, ac);
}

// ---- ResourceLink ----

TEST(ResourceLink, SerializeDeserialize) {
    ResourceLink rl;
    rl.uri = "file:///data/file.txt";
    rl.name = "My File";
    rl.description = "A text file";

    nlohmann::json j;
    to_json(j, rl);
    EXPECT_EQ(j["type"], "resource_link");
    EXPECT_EQ(j["uri"], "file:///data/file.txt");

    ResourceLink rl2;
    from_json(j, rl2);
    EXPECT_EQ(rl2, rl);
}

// ---- EmbeddedResource ----

TEST(EmbeddedResource, TextResource) {
    EmbeddedResource er;
    er.uri = "file:///data/config.json";
    er.mime_type = "application/json";
    er.text = "{\"key\": \"value\"}";

    nlohmann::json j;
    to_json(j, er);
    EXPECT_EQ(j["type"], "resource");
    EXPECT_EQ(j["resource"]["uri"], er.uri);
    EXPECT_EQ(j["resource"]["text"], *er.text);

    EmbeddedResource er2;
    from_json(j, er2);
    EXPECT_EQ(er2, er);
}

// ---- Content variant ----

TEST(Content, TextVariant) {
    Content c = TextContent{"hello", std::nullopt};
    nlohmann::json j;
    to_json(j, c);
    EXPECT_EQ(j["type"], "text");

    Content c2;
    from_json(j, c2);
    ASSERT_TRUE(std::holds_alternative<TextContent>(c2));
    EXPECT_EQ(std::get<TextContent>(c2).text, "hello");
}

TEST(Content, ImageVariant) {
    Content c = ImageContent{"data==", "image/jpeg", std::nullopt};
    nlohmann::json j;
    to_json(j, c);
    EXPECT_EQ(j["type"], "image");

    Content c2;
    from_json(j, c2);
    ASSERT_TRUE(std::holds_alternative<ImageContent>(c2));
}

TEST(Content, UnknownType) {
    nlohmann::json j = {{"type", "unknown_type"}};
    Content c;
    EXPECT_THROW(from_json(j, c), std::invalid_argument);
}

// ---- ToolDefinition ----

TEST(ToolDefinition, SerializeDeserialize) {
    ToolDefinition td;
    td.name = "get_weather";
    td.description = "Get weather for a location";
    td.input_schema = {
        {"type", "object"},
        {"properties", {{"location", {{"type", "string"}}}}},
        {"required", {"location"}}
    };

    nlohmann::json j;
    to_json(j, td);
    EXPECT_EQ(j["name"], "get_weather");
    EXPECT_TRUE(j.contains("inputSchema"));

    ToolDefinition td2;
    from_json(j, td2);
    EXPECT_EQ(td2, td);
}

// ---- ResourceDefinition ----

TEST(ResourceDefinition, SerializeDeserialize) {
    ResourceDefinition rd;
    rd.uri = "file:///data/file.txt";
    rd.name = "Config File";
    rd.mime_type = "text/plain";
    rd.size = 1024;

    nlohmann::json j;
    to_json(j, rd);
    EXPECT_EQ(j["uri"], rd.uri);
    EXPECT_EQ(j["size"], 1024);

    ResourceDefinition rd2;
    from_json(j, rd2);
    EXPECT_EQ(rd2, rd);
}

// ---- PromptDefinition ----

TEST(PromptDefinition, WithArguments) {
    PromptDefinition pd;
    pd.name = "code_review";
    pd.description = "Review code";
    pd.arguments = {
        {"code", "The code to review", true},
        {"language", "Programming language", false}
    };

    nlohmann::json j;
    to_json(j, pd);
    EXPECT_EQ(j["name"], "code_review");
    ASSERT_EQ(j["arguments"].size(), 2u);

    PromptDefinition pd2;
    from_json(j, pd2);
    EXPECT_EQ(pd2, pd);
}

// ---- LogLevel ----

TEST(LogLevel, RoundTrip) {
    for (auto level : {LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error}) {
        nlohmann::json j;
        to_json(j, level);
        LogLevel level2;
        from_json(j, level2);
        EXPECT_EQ(level, level2);
    }
}

TEST(LogLevel, UnknownLevel) {
    nlohmann::json j = "unknown";
    LogLevel level;
    EXPECT_THROW(from_json(j, level), std::invalid_argument);
}

// ---- Capabilities ----

TEST(ServerCapabilities, SerializeDeserialize) {
    ServerCapabilities caps;
    caps.tools = nlohmann::json{{"listChanged", true}};
    caps.resources = nlohmann::json{{"subscribe", true}};

    nlohmann::json j;
    to_json(j, caps);
    EXPECT_TRUE(j.contains("tools"));
    EXPECT_TRUE(j.contains("resources"));
    EXPECT_FALSE(j.contains("prompts"));

    ServerCapabilities caps2;
    from_json(j, caps2);
    EXPECT_EQ(caps2, caps);
}

TEST(ClientCapabilities, SerializeDeserialize) {
    ClientCapabilities caps;
    caps.sampling = nlohmann::json::object();

    nlohmann::json j;
    to_json(j, caps);
    EXPECT_TRUE(j.contains("sampling"));

    ClientCapabilities caps2;
    from_json(j, caps2);
    EXPECT_EQ(caps2, caps);
}

// ---- PaginatedResult ----

TEST(PaginatedResult, WithNextCursor) {
    PaginatedResult<ToolDefinition> result;
    result.items.push_back(ToolDefinition{"tool1", std::nullopt, std::nullopt,
                                          nlohmann::json::object(), std::nullopt, std::nullopt});
    result.next_cursor = "50";

    EXPECT_EQ(result.items.size(), 1u);
    EXPECT_EQ(*result.next_cursor, "50");
}
