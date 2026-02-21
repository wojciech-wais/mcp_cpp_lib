/// Prompt server — demonstrates prompts, completions, and logging.
/// Usage: ./prompt_server

#include <mcp/mcp.hpp>
#include <vector>
#include <string>
#include <algorithm>

int main() {
    mcp::McpServer::Options opts;
    opts.server_info = {"prompt-server", std::nullopt, "1.0.0"};
    opts.instructions = "A server providing code review and translation prompts.";

    mcp::McpServer server{std::move(opts)};

    // ---- Prompts ----

    // code_review prompt
    mcp::PromptDefinition code_review_def;
    code_review_def.name = "code_review";
    code_review_def.description = "Generate a code review prompt";
    code_review_def.arguments = {
        {"code", std::string("The code to review"), true},
        {"language", std::string("Programming language"), false}
    };
    server.add_prompt(code_review_def,
        [](const std::string&, const nlohmann::json& args) -> mcp::GetPromptResult {
            std::string code = args.at("code").get<std::string>();
            std::string lang = args.value("language", "");

            std::string prompt_text = "Please review the following";
            if (!lang.empty()) prompt_text += " " + lang;
            prompt_text += " code:\n\n```\n" + code + "\n```\n\n";
            prompt_text += "Focus on: correctness, performance, readability, and best practices.";

            mcp::GetPromptResult result;
            result.description = "Code review for " + (lang.empty() ? "code" : lang);
            result.messages.push_back({"user", mcp::TextContent{prompt_text, std::nullopt}});
            return result;
        });

    // translate prompt
    mcp::PromptDefinition translate_def;
    translate_def.name = "translate";
    translate_def.description = "Translate text to another language";
    translate_def.arguments = {
        {"text", std::string("Text to translate"), true},
        {"target_language", std::string("Target language"), true}
    };
    server.add_prompt(translate_def,
        [](const std::string&, const nlohmann::json& args) -> mcp::GetPromptResult {
            std::string text = args.at("text").get<std::string>();
            std::string target = args.at("target_language").get<std::string>();

            mcp::GetPromptResult result;
            result.messages.push_back({"user", mcp::TextContent{
                "Please translate the following text to " + target + ":\n\n" + text,
                std::nullopt
            }});
            return result;
        });

    // summarize prompt
    mcp::PromptDefinition summarize_def;
    summarize_def.name = "summarize";
    summarize_def.description = "Summarize a text";
    summarize_def.arguments = {
        {"text", std::string("Text to summarize"), true},
        {"length", std::string("Summary length: short, medium, long"), false}
    };
    server.add_prompt(summarize_def,
        [](const std::string&, const nlohmann::json& args) -> mcp::GetPromptResult {
            std::string text = args.at("text").get<std::string>();
            std::string length = args.value("length", "medium");

            mcp::GetPromptResult result;
            result.messages.push_back({"user", mcp::TextContent{
                "Please provide a " + length + " summary of:\n\n" + text,
                std::nullopt
            }});
            return result;
        });

    // ---- Completions ----

    static const std::vector<std::string> languages = {
        "c++", "python", "javascript", "typescript", "rust", "go",
        "java", "kotlin", "swift", "ruby", "php", "haskell"
    };

    static const std::vector<std::string> summary_lengths = {"short", "medium", "long"};

    server.set_completion_handler(
        [](const mcp::CompletionRef& ref, const std::string& arg_name,
           const std::string& arg_value) -> mcp::CompletionResult {
            mcp::CompletionResult result;

            if (ref.name == "code_review" && arg_name == "language") {
                std::string prefix = arg_value;
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                for (const auto& lang : languages) {
                    if (lang.find(prefix) == 0) {
                        result.values.push_back(lang);
                        if (result.values.size() >= 10) break;
                    }
                }
            } else if (ref.name == "summarize" && arg_name == "length") {
                for (const auto& len : summary_lengths) {
                    if (len.find(arg_value) == 0) {
                        result.values.push_back(len);
                    }
                }
            } else if (ref.name == "translate" && arg_name == "target_language") {
                static const std::vector<std::string> langs = {
                    "English", "French", "German", "Spanish", "Italian",
                    "Japanese", "Chinese", "Korean", "Polish", "Portuguese"
                };
                for (const auto& l : langs) {
                    std::string lower_l = l, lower_v = arg_value;
                    std::transform(lower_l.begin(), lower_l.end(), lower_l.begin(), ::tolower);
                    std::transform(lower_v.begin(), lower_v.end(), lower_v.begin(), ::tolower);
                    if (lower_l.find(lower_v) == 0) {
                        result.values.push_back(l);
                    }
                }
            }

            result.has_more = false;
            return result;
        });

    // Log startup
    server.log(mcp::LogLevel::Info, "prompt-server", nlohmann::json("Prompt server started"));

    server.serve_stdio();
    return 0;
}
