#!/bin/bash
# PostToolUse hook: clang-format the C++ file Claude just edited or wrote.
# The hook input is JSON on stdin; tool_input.file_path is the edited file.
file=$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("file_path",""))')
case "$file" in
    *.cpp|*.hpp|*.h|*.cc|*.cxx)
        command -v clang-format >/dev/null && [ -f "$file" ] && clang-format -i "$file"
        ;;
esac
exit 0
