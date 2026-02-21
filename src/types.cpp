#include "mcp/types.hpp"
#include <stdexcept>

namespace mcp {

// ---------- Annotations ----------

void to_json(nlohmann::json& j, const Annotations& a) {
    j = nlohmann::json::object();
    if (a.audience) j["audience"] = *a.audience;
    if (a.priority) j["priority"] = *a.priority;
    if (a.last_modified) j["lastModified"] = *a.last_modified;
}

void from_json(const nlohmann::json& j, Annotations& a) {
    if (j.contains("audience")) a.audience = j.at("audience").get<std::vector<std::string>>();
    if (j.contains("priority")) a.priority = j.at("priority").get<double>();
    if (j.contains("lastModified")) a.last_modified = j.at("lastModified").get<std::string>();
}

// ---------- TextContent ----------

void to_json(nlohmann::json& j, const TextContent& t) {
    j = {{"type", "text"}, {"text", t.text}};
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, TextContent& t) {
    t.text = j.at("text").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- ImageContent ----------

void to_json(nlohmann::json& j, const ImageContent& t) {
    j = {{"type", "image"}, {"data", t.data}, {"mimeType", t.mime_type}};
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, ImageContent& t) {
    t.data = j.at("data").get<std::string>();
    t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- AudioContent ----------

void to_json(nlohmann::json& j, const AudioContent& t) {
    j = {{"type", "audio"}, {"data", t.data}, {"mimeType", t.mime_type}};
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, AudioContent& t) {
    t.data = j.at("data").get<std::string>();
    t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- ResourceLink ----------

void to_json(nlohmann::json& j, const ResourceLink& t) {
    j = {{"type", "resource_link"}, {"uri", t.uri}, {"name", t.name}};
    if (t.description) j["description"] = *t.description;
    if (t.mime_type) j["mimeType"] = *t.mime_type;
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, ResourceLink& t) {
    t.uri = j.at("uri").get<std::string>();
    t.name = j.at("name").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("mimeType")) t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- EmbeddedResource ----------

void to_json(nlohmann::json& j, const EmbeddedResource& t) {
    nlohmann::json resource;
    resource["uri"] = t.uri;
    if (t.mime_type) resource["mimeType"] = *t.mime_type;
    if (t.text) resource["text"] = *t.text;
    if (t.blob) resource["blob"] = *t.blob;
    j = {{"type", "resource"}, {"resource", resource}};
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, EmbeddedResource& t) {
    const auto& resource = j.at("resource");
    t.uri = resource.at("uri").get<std::string>();
    if (resource.contains("mimeType")) t.mime_type = resource.at("mimeType").get<std::string>();
    if (resource.contains("text")) t.text = resource.at("text").get<std::string>();
    if (resource.contains("blob")) t.blob = resource.at("blob").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- Content ----------

void to_json(nlohmann::json& j, const Content& c) {
    std::visit([&j](const auto& v) { to_json(j, v); }, c);
}

void from_json(const nlohmann::json& j, Content& c) {
    const std::string type = j.at("type").get<std::string>();
    if (type == "text") {
        c = j.get<TextContent>();
    } else if (type == "image") {
        c = j.get<ImageContent>();
    } else if (type == "audio") {
        c = j.get<AudioContent>();
    } else if (type == "resource_link") {
        c = j.get<ResourceLink>();
    } else if (type == "resource") {
        c = j.get<EmbeddedResource>();
    } else {
        throw std::invalid_argument("Unknown content type: " + type);
    }
}

// ---------- ToolDefinition ----------

void to_json(nlohmann::json& j, const ToolDefinition& t) {
    j = {{"name", t.name}, {"inputSchema", t.input_schema}};
    if (t.title) j["title"] = *t.title;
    if (t.description) j["description"] = *t.description;
    if (t.output_schema) j["outputSchema"] = *t.output_schema;
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, ToolDefinition& t) {
    t.name = j.at("name").get<std::string>();
    t.input_schema = j.at("inputSchema");
    if (j.contains("title")) t.title = j.at("title").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("outputSchema")) t.output_schema = j.at("outputSchema");
    if (j.contains("annotations")) t.annotations = j.at("annotations");
}

// ---------- CallToolResult ----------

void to_json(nlohmann::json& j, const CallToolResult& t) {
    j = nlohmann::json::object();
    j["content"] = nlohmann::json::array();
    for (const auto& c : t.content) {
        nlohmann::json cj;
        to_json(cj, c);
        j["content"].push_back(cj);
    }
    if (t.structured_content) j["structuredContent"] = *t.structured_content;
    if (t.is_error) j["isError"] = t.is_error;
}

void from_json(const nlohmann::json& j, CallToolResult& t) {
    if (j.contains("content")) {
        for (const auto& cj : j.at("content")) {
            Content c;
            from_json(cj, c);
            t.content.push_back(std::move(c));
        }
    }
    if (j.contains("structuredContent")) t.structured_content = j.at("structuredContent");
    if (j.contains("isError")) t.is_error = j.at("isError").get<bool>();
}

// ---------- ResourceDefinition ----------

void to_json(nlohmann::json& j, const ResourceDefinition& t) {
    j = {{"uri", t.uri}, {"name", t.name}};
    if (t.title) j["title"] = *t.title;
    if (t.description) j["description"] = *t.description;
    if (t.mime_type) j["mimeType"] = *t.mime_type;
    if (t.size) j["size"] = *t.size;
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, ResourceDefinition& t) {
    t.uri = j.at("uri").get<std::string>();
    t.name = j.at("name").get<std::string>();
    if (j.contains("title")) t.title = j.at("title").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("mimeType")) t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("size")) t.size = j.at("size").get<size_t>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- ResourceContent ----------

void to_json(nlohmann::json& j, const ResourceContent& t) {
    j = {{"uri", t.uri}};
    if (t.mime_type) j["mimeType"] = *t.mime_type;
    if (t.text) j["text"] = *t.text;
    if (t.blob) j["blob"] = *t.blob;
}

void from_json(const nlohmann::json& j, ResourceContent& t) {
    t.uri = j.at("uri").get<std::string>();
    if (j.contains("mimeType")) t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("text")) t.text = j.at("text").get<std::string>();
    if (j.contains("blob")) t.blob = j.at("blob").get<std::string>();
}

// ---------- ResourceTemplate ----------

void to_json(nlohmann::json& j, const ResourceTemplate& t) {
    j = {{"uriTemplate", t.uri_template}, {"name", t.name}};
    if (t.title) j["title"] = *t.title;
    if (t.description) j["description"] = *t.description;
    if (t.mime_type) j["mimeType"] = *t.mime_type;
    if (t.annotations) j["annotations"] = *t.annotations;
}

void from_json(const nlohmann::json& j, ResourceTemplate& t) {
    t.uri_template = j.at("uriTemplate").get<std::string>();
    t.name = j.at("name").get<std::string>();
    if (j.contains("title")) t.title = j.at("title").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("mimeType")) t.mime_type = j.at("mimeType").get<std::string>();
    if (j.contains("annotations")) t.annotations = j.at("annotations").get<Annotations>();
}

// ---------- PromptArgument ----------

void to_json(nlohmann::json& j, const PromptArgument& t) {
    j = {{"name", t.name}, {"required", t.required}};
    if (t.description) j["description"] = *t.description;
}

void from_json(const nlohmann::json& j, PromptArgument& t) {
    t.name = j.at("name").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("required")) t.required = j.at("required").get<bool>();
}

// ---------- PromptDefinition ----------

void to_json(nlohmann::json& j, const PromptDefinition& t) {
    j = {{"name", t.name}, {"arguments", t.arguments}};
    if (t.title) j["title"] = *t.title;
    if (t.description) j["description"] = *t.description;
}

void from_json(const nlohmann::json& j, PromptDefinition& t) {
    t.name = j.at("name").get<std::string>();
    if (j.contains("title")) t.title = j.at("title").get<std::string>();
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    if (j.contains("arguments")) t.arguments = j.at("arguments").get<std::vector<PromptArgument>>();
}

// ---------- PromptMessage ----------

void to_json(nlohmann::json& j, const PromptMessage& t) {
    nlohmann::json content_j;
    to_json(content_j, t.content);
    j = {{"role", t.role}, {"content", content_j}};
}

void from_json(const nlohmann::json& j, PromptMessage& t) {
    t.role = j.at("role").get<std::string>();
    from_json(j.at("content"), t.content);
}

// ---------- GetPromptResult ----------

void to_json(nlohmann::json& j, const GetPromptResult& t) {
    j = {{"messages", t.messages}};
    if (t.description) j["description"] = *t.description;
}

void from_json(const nlohmann::json& j, GetPromptResult& t) {
    if (j.contains("description")) t.description = j.at("description").get<std::string>();
    t.messages = j.at("messages").get<std::vector<PromptMessage>>();
}

// ---------- ModelHint ----------

void to_json(nlohmann::json& j, const ModelHint& t) {
    j = {{"name", t.name}};
}

void from_json(const nlohmann::json& j, ModelHint& t) {
    t.name = j.at("name").get<std::string>();
}

// ---------- ModelPreferences ----------

void to_json(nlohmann::json& j, const ModelPreferences& t) {
    j = {{"hints", t.hints}};
    if (t.cost_priority) j["costPriority"] = *t.cost_priority;
    if (t.speed_priority) j["speedPriority"] = *t.speed_priority;
    if (t.intelligence_priority) j["intelligencePriority"] = *t.intelligence_priority;
}

void from_json(const nlohmann::json& j, ModelPreferences& t) {
    if (j.contains("hints")) t.hints = j.at("hints").get<std::vector<ModelHint>>();
    if (j.contains("costPriority")) t.cost_priority = j.at("costPriority").get<double>();
    if (j.contains("speedPriority")) t.speed_priority = j.at("speedPriority").get<double>();
    if (j.contains("intelligencePriority")) t.intelligence_priority = j.at("intelligencePriority").get<double>();
}

// ---------- SamplingRequest ----------

void to_json(nlohmann::json& j, const SamplingRequest& t) {
    j = {{"messages", t.messages}};
    if (t.model_preferences) j["modelPreferences"] = *t.model_preferences;
    if (t.system_prompt) j["systemPrompt"] = *t.system_prompt;
    if (t.max_tokens) j["maxTokens"] = *t.max_tokens;
}

void from_json(const nlohmann::json& j, SamplingRequest& t) {
    t.messages = j.at("messages").get<std::vector<PromptMessage>>();
    if (j.contains("modelPreferences")) t.model_preferences = j.at("modelPreferences").get<ModelPreferences>();
    if (j.contains("systemPrompt")) t.system_prompt = j.at("systemPrompt").get<std::string>();
    if (j.contains("maxTokens")) t.max_tokens = j.at("maxTokens").get<int>();
}

// ---------- SamplingResult ----------

void to_json(nlohmann::json& j, const SamplingResult& t) {
    nlohmann::json content_j;
    to_json(content_j, t.content);
    j = {{"role", t.role}, {"content", content_j}, {"model", t.model}};
    if (t.stop_reason) j["stopReason"] = *t.stop_reason;
}

void from_json(const nlohmann::json& j, SamplingResult& t) {
    t.role = j.at("role").get<std::string>();
    from_json(j.at("content"), t.content);
    t.model = j.at("model").get<std::string>();
    if (j.contains("stopReason")) t.stop_reason = j.at("stopReason").get<std::string>();
}

// ---------- ElicitationRequest ----------

void to_json(nlohmann::json& j, const ElicitationRequest& t) {
    j = {{"message", t.message}, {"requestedSchema", t.requested_schema}};
}

void from_json(const nlohmann::json& j, ElicitationRequest& t) {
    t.message = j.at("message").get<std::string>();
    t.requested_schema = j.at("requestedSchema");
}

// ---------- ElicitationResult ----------

void to_json(nlohmann::json& j, const ElicitationResult& t) {
    j = {{"action", t.action}};
    if (t.content) j["content"] = *t.content;
}

void from_json(const nlohmann::json& j, ElicitationResult& t) {
    t.action = j.at("action").get<std::string>();
    if (j.contains("content")) t.content = j.at("content");
}

// ---------- Root ----------

void to_json(nlohmann::json& j, const Root& t) {
    j = {{"uri", t.uri}};
    if (t.name) j["name"] = *t.name;
}

void from_json(const nlohmann::json& j, Root& t) {
    t.uri = j.at("uri").get<std::string>();
    if (j.contains("name")) t.name = j.at("name").get<std::string>();
}

// ---------- CompletionRef ----------

void to_json(nlohmann::json& j, const CompletionRef& t) {
    j = {{"type", t.type}, {"name", t.name}};
}

void from_json(const nlohmann::json& j, CompletionRef& t) {
    t.type = j.at("type").get<std::string>();
    t.name = j.at("name").get<std::string>();
}

// ---------- CompletionResult ----------

void to_json(nlohmann::json& j, const CompletionResult& t) {
    j = {{"completion", {{"values", t.values}, {"hasMore", t.has_more}}}};
    if (t.total) j["completion"]["total"] = *t.total;
}

void from_json(const nlohmann::json& j, CompletionResult& t) {
    const auto& comp = j.contains("completion") ? j.at("completion") : j;
    t.values = comp.at("values").get<std::vector<std::string>>();
    if (comp.contains("hasMore")) t.has_more = comp.at("hasMore").get<bool>();
    if (comp.contains("total")) t.total = comp.at("total").get<int>();
}

// ---------- Capabilities ----------

void to_json(nlohmann::json& j, const ServerCapabilities& t) {
    j = nlohmann::json::object();
    if (t.tools) j["tools"] = *t.tools;
    if (t.resources) j["resources"] = *t.resources;
    if (t.prompts) j["prompts"] = *t.prompts;
    if (t.logging) j["logging"] = *t.logging;
    if (t.completions) j["completions"] = *t.completions;
    if (t.experimental) j["experimental"] = *t.experimental;
}

void from_json(const nlohmann::json& j, ServerCapabilities& t) {
    if (j.contains("tools")) t.tools = j.at("tools");
    if (j.contains("resources")) t.resources = j.at("resources");
    if (j.contains("prompts")) t.prompts = j.at("prompts");
    if (j.contains("logging")) t.logging = j.at("logging");
    if (j.contains("completions")) t.completions = j.at("completions");
    if (j.contains("experimental")) t.experimental = j.at("experimental");
}

void to_json(nlohmann::json& j, const ClientCapabilities& t) {
    j = nlohmann::json::object();
    if (t.roots) j["roots"] = *t.roots;
    if (t.sampling) j["sampling"] = *t.sampling;
    if (t.elicitation) j["elicitation"] = *t.elicitation;
    if (t.experimental) j["experimental"] = *t.experimental;
}

void from_json(const nlohmann::json& j, ClientCapabilities& t) {
    if (j.contains("roots")) t.roots = j.at("roots");
    if (j.contains("sampling")) t.sampling = j.at("sampling");
    if (j.contains("elicitation")) t.elicitation = j.at("elicitation");
    if (j.contains("experimental")) t.experimental = j.at("experimental");
}

void to_json(nlohmann::json& j, const Implementation& t) {
    j = {{"name", t.name}, {"version", t.version}};
    if (t.title) j["title"] = *t.title;
}

void from_json(const nlohmann::json& j, Implementation& t) {
    t.name = j.at("name").get<std::string>();
    t.version = j.at("version").get<std::string>();
    if (j.contains("title")) t.title = j.at("title").get<std::string>();
}

void to_json(nlohmann::json& j, const InitializeResult& t) {
    j = {
        {"protocolVersion", t.protocol_version},
        {"capabilities", t.capabilities},
        {"serverInfo", t.server_info}
    };
    if (t.instructions) j["instructions"] = *t.instructions;
}

void from_json(const nlohmann::json& j, InitializeResult& t) {
    t.protocol_version = j.at("protocolVersion").get<std::string>();
    t.capabilities = j.at("capabilities").get<ServerCapabilities>();
    t.server_info = j.at("serverInfo").get<Implementation>();
    if (j.contains("instructions")) t.instructions = j.at("instructions").get<std::string>();
}

// ---------- LogLevel ----------

std::string log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:     return "debug";
        case LogLevel::Info:      return "info";
        case LogLevel::Notice:    return "notice";
        case LogLevel::Warning:   return "warning";
        case LogLevel::Error:     return "error";
        case LogLevel::Critical:  return "critical";
        case LogLevel::Alert:     return "alert";
        case LogLevel::Emergency: return "emergency";
        default:                  return "info";
    }
}

LogLevel log_level_from_string(const std::string& s) {
    if (s == "debug")     return LogLevel::Debug;
    if (s == "info")      return LogLevel::Info;
    if (s == "notice")    return LogLevel::Notice;
    if (s == "warning")   return LogLevel::Warning;
    if (s == "error")     return LogLevel::Error;
    if (s == "critical")  return LogLevel::Critical;
    if (s == "alert")     return LogLevel::Alert;
    if (s == "emergency") return LogLevel::Emergency;
    throw std::invalid_argument("Unknown log level: " + s);
}

void to_json(nlohmann::json& j, LogLevel level) {
    j = log_level_to_string(level);
}

void from_json(const nlohmann::json& j, LogLevel& level) {
    level = log_level_from_string(j.get<std::string>());
}

void to_json(nlohmann::json& j, const LogMessage& t) {
    j = {{"level", t.level}, {"data", t.data}};
    if (t.logger) j["logger"] = *t.logger;
}

void from_json(const nlohmann::json& j, LogMessage& t) {
    from_json(j.at("level"), t.level);
    t.data = j.at("data");
    if (j.contains("logger")) t.logger = j.at("logger").get<std::string>();
}

} // namespace mcp
