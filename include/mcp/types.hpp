#pragma once
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <map>
#include <nlohmann/json.hpp>

namespace mcp {

// ---------- Annotations (defined first so content types can use it) ----------

struct Annotations {
    std::optional<std::vector<std::string>> audience; // "user", "assistant"
    std::optional<double> priority;                   // 0.0 .. 1.0
    std::optional<std::string> last_modified;         // ISO 8601

    bool operator==(const Annotations& o) const {
        return audience == o.audience && priority == o.priority
               && last_modified == o.last_modified;
    }
};

// ---------- Content types ----------

struct TextContent {
    std::string text;
    std::optional<Annotations> annotations;

    bool operator==(const TextContent& o) const {
        return text == o.text && annotations == o.annotations;
    }
};

struct ImageContent {
    std::string data;       // base64
    std::string mime_type;
    std::optional<Annotations> annotations;

    bool operator==(const ImageContent& o) const {
        return data == o.data && mime_type == o.mime_type && annotations == o.annotations;
    }
};

struct AudioContent {
    std::string data;       // base64
    std::string mime_type;
    std::optional<Annotations> annotations;

    bool operator==(const AudioContent& o) const {
        return data == o.data && mime_type == o.mime_type && annotations == o.annotations;
    }
};

struct ResourceLink {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<Annotations> annotations;

    bool operator==(const ResourceLink& o) const {
        return uri == o.uri && name == o.name && description == o.description
               && mime_type == o.mime_type && annotations == o.annotations;
    }
};

struct EmbeddedResource {
    std::string uri;
    std::optional<std::string> mime_type;
    std::optional<std::string> text;
    std::optional<std::string> blob;  // base64
    std::optional<Annotations> annotations;

    bool operator==(const EmbeddedResource& o) const {
        return uri == o.uri && mime_type == o.mime_type && text == o.text
               && blob == o.blob && annotations == o.annotations;
    }
};

using Content = std::variant<TextContent, ImageContent, AudioContent,
                             ResourceLink, EmbeddedResource>;

// ---------- Tool ----------

struct ToolDefinition {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    nlohmann::json input_schema;
    std::optional<nlohmann::json> output_schema;
    std::optional<nlohmann::json> annotations;

    bool operator==(const ToolDefinition& o) const {
        return name == o.name && title == o.title && description == o.description
               && input_schema == o.input_schema && output_schema == o.output_schema
               && annotations == o.annotations;
    }
};

struct CallToolResult {
    std::vector<Content> content;
    std::optional<nlohmann::json> structured_content;
    bool is_error = false;

    bool operator==(const CallToolResult& o) const {
        return content == o.content && structured_content == o.structured_content
               && is_error == o.is_error;
    }
};

// ---------- Resource ----------

struct ResourceDefinition {
    std::string uri;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<size_t> size;
    std::optional<Annotations> annotations;

    bool operator==(const ResourceDefinition& o) const {
        return uri == o.uri && name == o.name && title == o.title
               && description == o.description && mime_type == o.mime_type
               && size == o.size && annotations == o.annotations;
    }
};

struct ResourceContent {
    std::string uri;
    std::optional<std::string> mime_type;
    std::optional<std::string> text;
    std::optional<std::string> blob;  // base64

    bool operator==(const ResourceContent& o) const {
        return uri == o.uri && mime_type == o.mime_type && text == o.text
               && blob == o.blob;
    }
};

struct ResourceTemplate {
    std::string uri_template;
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<Annotations> annotations;

    bool operator==(const ResourceTemplate& o) const {
        return uri_template == o.uri_template && name == o.name && title == o.title
               && description == o.description && mime_type == o.mime_type
               && annotations == o.annotations;
    }
};

// ---------- Prompt ----------

struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;

    bool operator==(const PromptArgument& o) const {
        return name == o.name && description == o.description && required == o.required;
    }
};

struct PromptDefinition {
    std::string name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;

    bool operator==(const PromptDefinition& o) const {
        return name == o.name && title == o.title && description == o.description
               && arguments == o.arguments;
    }
};

struct PromptMessage {
    std::string role;  // "user" or "assistant"
    Content content;

    bool operator==(const PromptMessage& o) const {
        return role == o.role && content == o.content;
    }
};

struct GetPromptResult {
    std::optional<std::string> description;
    std::vector<PromptMessage> messages;

    bool operator==(const GetPromptResult& o) const {
        return description == o.description && messages == o.messages;
    }
};

// ---------- Sampling ----------

struct ModelHint {
    std::string name;

    bool operator==(const ModelHint& o) const { return name == o.name; }
};

struct ModelPreferences {
    std::vector<ModelHint> hints;
    std::optional<double> cost_priority;
    std::optional<double> speed_priority;
    std::optional<double> intelligence_priority;

    bool operator==(const ModelPreferences& o) const {
        return hints == o.hints && cost_priority == o.cost_priority
               && speed_priority == o.speed_priority
               && intelligence_priority == o.intelligence_priority;
    }
};

struct SamplingRequest {
    std::vector<PromptMessage> messages;
    std::optional<ModelPreferences> model_preferences;
    std::optional<std::string> system_prompt;
    std::optional<int> max_tokens;

    bool operator==(const SamplingRequest& o) const {
        return messages == o.messages && model_preferences == o.model_preferences
               && system_prompt == o.system_prompt && max_tokens == o.max_tokens;
    }
};

struct SamplingResult {
    std::string role;
    Content content;
    std::string model;
    std::optional<std::string> stop_reason;

    bool operator==(const SamplingResult& o) const {
        return role == o.role && content == o.content && model == o.model
               && stop_reason == o.stop_reason;
    }
};

// ---------- Elicitation ----------

struct ElicitationRequest {
    std::string message;
    nlohmann::json requested_schema;

    bool operator==(const ElicitationRequest& o) const {
        return message == o.message && requested_schema == o.requested_schema;
    }
};

struct ElicitationResult {
    std::string action; // "accept", "decline", "cancel"
    std::optional<nlohmann::json> content;

    bool operator==(const ElicitationResult& o) const {
        return action == o.action && content == o.content;
    }
};

// ---------- Roots ----------

struct Root {
    std::string uri;  // file:// URI
    std::optional<std::string> name;

    bool operator==(const Root& o) const {
        return uri == o.uri && name == o.name;
    }
};

// ---------- Completion ----------

struct CompletionRef {
    std::string type;  // "ref/prompt" or "ref/resource"
    std::string name;  // prompt name or resource URI

    bool operator==(const CompletionRef& o) const {
        return type == o.type && name == o.name;
    }
};

struct CompletionResult {
    std::vector<std::string> values; // max 100
    std::optional<int> total;
    bool has_more = false;

    bool operator==(const CompletionResult& o) const {
        return values == o.values && total == o.total && has_more == o.has_more;
    }
};

// ---------- Capabilities ----------

struct ServerCapabilities {
    std::optional<nlohmann::json> tools;
    std::optional<nlohmann::json> resources;
    std::optional<nlohmann::json> prompts;
    std::optional<nlohmann::json> logging;
    std::optional<nlohmann::json> completions;
    std::optional<nlohmann::json> experimental;

    bool operator==(const ServerCapabilities& o) const {
        return tools == o.tools && resources == o.resources && prompts == o.prompts
               && logging == o.logging && completions == o.completions
               && experimental == o.experimental;
    }
};

struct ClientCapabilities {
    std::optional<nlohmann::json> roots;
    std::optional<nlohmann::json> sampling;
    std::optional<nlohmann::json> elicitation;
    std::optional<nlohmann::json> experimental;

    bool operator==(const ClientCapabilities& o) const {
        return roots == o.roots && sampling == o.sampling && elicitation == o.elicitation
               && experimental == o.experimental;
    }
};

struct Implementation {
    std::string name;
    std::optional<std::string> title;
    std::string version;

    bool operator==(const Implementation& o) const {
        return name == o.name && title == o.title && version == o.version;
    }
};

struct InitializeResult {
    std::string protocol_version;
    ServerCapabilities capabilities;
    Implementation server_info;
    std::optional<std::string> instructions;

    bool operator==(const InitializeResult& o) const {
        return protocol_version == o.protocol_version && capabilities == o.capabilities
               && server_info == o.server_info && instructions == o.instructions;
    }
};

// ---------- Logging ----------

enum class LogLevel {
    Debug, Info, Notice, Warning, Error, Critical, Alert, Emergency
};

std::string log_level_to_string(LogLevel level);
LogLevel log_level_from_string(const std::string& s);

struct LogMessage {
    LogLevel level;
    std::optional<std::string> logger;
    nlohmann::json data;

    bool operator==(const LogMessage& o) const {
        return level == o.level && logger == o.logger && data == o.data;
    }
};

// ---------- Pagination ----------

struct PaginatedRequest {
    std::optional<std::string> cursor;
};

template <typename T>
struct PaginatedResult {
    std::vector<T> items;
    std::optional<std::string> next_cursor;

    bool operator==(const PaginatedResult<T>& o) const {
        return items == o.items && next_cursor == o.next_cursor;
    }
};

// ---------- JSON serialization ----------

void to_json(nlohmann::json& j, const Annotations& a);
void from_json(const nlohmann::json& j, Annotations& a);

void to_json(nlohmann::json& j, const TextContent& t);
void from_json(const nlohmann::json& j, TextContent& t);

void to_json(nlohmann::json& j, const ImageContent& t);
void from_json(const nlohmann::json& j, ImageContent& t);

void to_json(nlohmann::json& j, const AudioContent& t);
void from_json(const nlohmann::json& j, AudioContent& t);

void to_json(nlohmann::json& j, const ResourceLink& t);
void from_json(const nlohmann::json& j, ResourceLink& t);

void to_json(nlohmann::json& j, const EmbeddedResource& t);
void from_json(const nlohmann::json& j, EmbeddedResource& t);

void to_json(nlohmann::json& j, const Content& c);
void from_json(const nlohmann::json& j, Content& c);

void to_json(nlohmann::json& j, const ToolDefinition& t);
void from_json(const nlohmann::json& j, ToolDefinition& t);

void to_json(nlohmann::json& j, const CallToolResult& t);
void from_json(const nlohmann::json& j, CallToolResult& t);

void to_json(nlohmann::json& j, const ResourceDefinition& t);
void from_json(const nlohmann::json& j, ResourceDefinition& t);

void to_json(nlohmann::json& j, const ResourceContent& t);
void from_json(const nlohmann::json& j, ResourceContent& t);

void to_json(nlohmann::json& j, const ResourceTemplate& t);
void from_json(const nlohmann::json& j, ResourceTemplate& t);

void to_json(nlohmann::json& j, const PromptArgument& t);
void from_json(const nlohmann::json& j, PromptArgument& t);

void to_json(nlohmann::json& j, const PromptDefinition& t);
void from_json(const nlohmann::json& j, PromptDefinition& t);

void to_json(nlohmann::json& j, const PromptMessage& t);
void from_json(const nlohmann::json& j, PromptMessage& t);

void to_json(nlohmann::json& j, const GetPromptResult& t);
void from_json(const nlohmann::json& j, GetPromptResult& t);

void to_json(nlohmann::json& j, const ModelHint& t);
void from_json(const nlohmann::json& j, ModelHint& t);

void to_json(nlohmann::json& j, const ModelPreferences& t);
void from_json(const nlohmann::json& j, ModelPreferences& t);

void to_json(nlohmann::json& j, const SamplingRequest& t);
void from_json(const nlohmann::json& j, SamplingRequest& t);

void to_json(nlohmann::json& j, const SamplingResult& t);
void from_json(const nlohmann::json& j, SamplingResult& t);

void to_json(nlohmann::json& j, const ElicitationRequest& t);
void from_json(const nlohmann::json& j, ElicitationRequest& t);

void to_json(nlohmann::json& j, const ElicitationResult& t);
void from_json(const nlohmann::json& j, ElicitationResult& t);

void to_json(nlohmann::json& j, const Root& t);
void from_json(const nlohmann::json& j, Root& t);

void to_json(nlohmann::json& j, const CompletionRef& t);
void from_json(const nlohmann::json& j, CompletionRef& t);

void to_json(nlohmann::json& j, const CompletionResult& t);
void from_json(const nlohmann::json& j, CompletionResult& t);

void to_json(nlohmann::json& j, const ServerCapabilities& t);
void from_json(const nlohmann::json& j, ServerCapabilities& t);

void to_json(nlohmann::json& j, const ClientCapabilities& t);
void from_json(const nlohmann::json& j, ClientCapabilities& t);

void to_json(nlohmann::json& j, const Implementation& t);
void from_json(const nlohmann::json& j, Implementation& t);

void to_json(nlohmann::json& j, const InitializeResult& t);
void from_json(const nlohmann::json& j, InitializeResult& t);

void to_json(nlohmann::json& j, LogLevel level);
void from_json(const nlohmann::json& j, LogLevel& level);

void to_json(nlohmann::json& j, const LogMessage& t);
void from_json(const nlohmann::json& j, LogMessage& t);

} // namespace mcp
