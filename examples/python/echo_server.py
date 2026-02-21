"""
Echo server example using mcpxx Python bindings.
Usage: python echo_server.py
"""

import sys
import os

# Add the build directory to path if needed
# sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../build/python'))

try:
    import mcpxx
except ImportError:
    print("mcpxx not installed. Build with -DMCPXX_BUILD_PYTHON=ON", file=sys.stderr)
    sys.exit(1)

def main():
    server = mcpxx.McpServer(
        name="echo-server-py",
        version="1.0.0",
        instructions="Python echo server using mcpxx bindings"
    )

    @server.tool(
        name="echo",
        description="Echo the input text back to the caller",
        input_schema={
            "type": "object",
            "properties": {
                "text": {"type": "string", "description": "Text to echo"}
            },
            "required": ["text"]
        }
    )
    def echo(arguments: dict) -> dict:
        return {
            "content": [{"type": "text", "text": arguments["text"]}]
        }

    server.serve_stdio()

if __name__ == "__main__":
    main()
