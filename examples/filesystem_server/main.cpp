/// Filesystem server — exposes files as MCP resources and tools.
/// Usage: ./filesystem_server [root_dir]
/// Default root_dir is current directory.

#include <mcp/mcp.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::string root_dir = (argc > 1) ? argv[1] : ".";
    fs::path root = fs::canonical(root_dir);

    mcp::McpServer::Options opts;
    opts.server_info = {"filesystem-server", std::nullopt, "1.0.0"};
    opts.instructions = "Exposes the local filesystem under: " + root.string();

    mcp::McpServer server{std::move(opts)};

    // ---- Tools ----

    // read_file tool
    mcp::ToolDefinition read_tool;
    read_tool.name = "read_file";
    read_tool.description = "Read the contents of a file";
    read_tool.input_schema = {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}
    };
    server.add_tool(read_tool, [root](const nlohmann::json& args) -> mcp::CallToolResult {
        fs::path p = root / args.at("path").get<std::string>();
        // Security: ensure path is under root
        p = fs::canonical(p);
        if (p.string().find(root.string()) != 0) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"Access denied: path outside root", std::nullopt});
            return err;
        }
        std::ifstream file(p);
        if (!file) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"File not found: " + p.string(), std::nullopt});
            return err;
        }
        std::ostringstream oss;
        oss << file.rdbuf();
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent{oss.str(), std::nullopt});
        return result;
    });

    // list_directory tool
    mcp::ToolDefinition list_tool;
    list_tool.name = "list_directory";
    list_tool.description = "List files in a directory";
    list_tool.input_schema = {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", {"path"}}
    };
    server.add_tool(list_tool, [root](const nlohmann::json& args) -> mcp::CallToolResult {
        fs::path p = root / args.at("path").get<std::string>();
        try {
            p = fs::canonical(p);
        } catch (...) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"Directory not found", std::nullopt});
            return err;
        }
        if (p.string().find(root.string()) != 0) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"Access denied", std::nullopt});
            return err;
        }

        std::string listing;
        for (const auto& entry : fs::directory_iterator(p)) {
            std::string type = entry.is_directory() ? "[dir] " : "[file] ";
            listing += type + entry.path().filename().string() + "\n";
        }
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent{listing, std::nullopt});
        return result;
    });

    // write_file tool
    mcp::ToolDefinition write_tool;
    write_tool.name = "write_file";
    write_tool.description = "Write content to a file";
    write_tool.input_schema = {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }},
        {"required", {"path", "content"}}
    };
    server.add_tool(write_tool, [root](const nlohmann::json& args) -> mcp::CallToolResult {
        fs::path p = root / args.at("path").get<std::string>();
        // Basic path traversal check
        if (args.at("path").get<std::string>().find("..") != std::string::npos) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"Access denied", std::nullopt});
            return err;
        }
        std::ofstream file(p);
        if (!file) {
            mcp::CallToolResult err;
            err.is_error = true;
            err.content.push_back(mcp::TextContent{"Cannot write file: " + p.string(), std::nullopt});
            return err;
        }
        file << args.at("content").get<std::string>();
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent{"File written successfully", std::nullopt});
        return result;
    });

    // ---- Resources ----

    // Resource template for file:///{path}
    mcp::ResourceTemplate file_tmpl;
    file_tmpl.uri_template = "file:///{path}";
    file_tmpl.name = "File";
    file_tmpl.description = "A file from the filesystem";
    server.add_resource_template(file_tmpl, [root](const std::string& uri) -> std::vector<mcp::ResourceContent> {
        // Parse path from uri: file:///path/to/file
        std::string path_str = uri.substr(7); // Remove "file://"
        fs::path p = root / path_str;

        std::ifstream file(p);
        if (!file) {
            throw std::runtime_error("File not found: " + uri);
        }
        std::ostringstream oss;
        oss << file.rdbuf();
        return {mcp::ResourceContent{uri, std::nullopt, oss.str(), std::nullopt}};
    });

    server.serve_stdio();
    return 0;
}
