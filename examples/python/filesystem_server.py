"""
Filesystem server example using mcpxx Python bindings.
Usage: python filesystem_server.py [root_dir]
"""

import sys
import os

try:
    import mcpxx
except ImportError:
    print("mcpxx not installed. Build with -DMCPXX_BUILD_PYTHON=ON", file=sys.stderr)
    sys.exit(1)

def main():
    root_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    root_dir = os.path.abspath(root_dir)

    server = mcpxx.McpServer(
        name="filesystem-server-py",
        version="1.0.0",
        instructions=f"Exposes filesystem under: {root_dir}"
    )

    @server.tool(
        name="read_file",
        description="Read a file",
        input_schema={
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"]
        }
    )
    def read_file(arguments: dict) -> dict:
        path = os.path.join(root_dir, arguments["path"])
        path = os.path.realpath(path)
        if not path.startswith(root_dir):
            return {"content": [{"type": "text", "text": "Access denied"}], "isError": True}
        try:
            with open(path, "r") as f:
                content = f.read()
            return {"content": [{"type": "text", "text": content}]}
        except FileNotFoundError:
            return {"content": [{"type": "text", "text": f"File not found: {path}"}], "isError": True}

    @server.tool(
        name="list_directory",
        description="List directory contents",
        input_schema={
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"]
        }
    )
    def list_directory(arguments: dict) -> dict:
        path = os.path.join(root_dir, arguments["path"])
        try:
            entries = os.listdir(path)
            text = "\n".join(
                f"[dir]  {e}" if os.path.isdir(os.path.join(path, e)) else f"[file] {e}"
                for e in sorted(entries)
            )
            return {"content": [{"type": "text", "text": text}]}
        except Exception as e:
            return {"content": [{"type": "text", "text": str(e)}], "isError": True}

    @server.resource(
        uri_template="file:///{path}",
        name="File",
        description="A file from the filesystem"
    )
    def read_file_resource(uri: str) -> list:
        # Extract path from URI
        path = uri[len("file:///"):]
        full_path = os.path.join(root_dir, path)
        try:
            with open(full_path, "r") as f:
                content = f.read()
            return [{"uri": uri, "text": content}]
        except FileNotFoundError:
            raise ValueError(f"File not found: {uri}")

    server.serve_stdio()

if __name__ == "__main__":
    main()
