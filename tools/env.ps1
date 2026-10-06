# Dot-source this file (". .\tools\env.ps1") to use the portable LegoRR toolchain in PowerShell.
$T = "E:\Users\LtLary\Desktop\WTF\tools"
$env:JAVA_HOME = "$T\jdk21"
$env:MAVEN_HOME = "$T\maven"
$env:GHIDRA_INSTALL_DIR = "$T\ghidra"
$env:GHIDRA_PATH = $env:GHIDRA_INSTALL_DIR
$env:UV_PYTHON_INSTALL_DIR = "$T\python"
$env:UV_CACHE_DIR = "$T\uv-cache"
$env:UV_TOOL_DIR = "$T\uv-tools"
$env:UV_PYTHON_BIN_DIR = "$T\python\bin"
$env:UV_TOOL_BIN_DIR = "$T\uv-tools\bin"
$env:UV_PYTHON_PREFERENCE = "only-managed"
$env:PATH = "$T\jdk21\bin;$T\maven\bin;$T\uv;$T\cmake\bin;$T\ninja;$T\llvm-mingw\bin;$env:PATH"
$env:GHIDRA_DEBUGGER_PYTHON = "$T\ghidra-mcp\.venv\Scripts\python.exe"
