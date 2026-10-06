# Starts the GhidraMCP headless server (no GUI) on 127.0.0.1:8089 using the portable toolchain.
# Usage: powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp-headless.ps1 [-Port 8089] [-Project <path to .gpr dir>] [-Program <name>]
param(
    [int]$Port = 8089,
    [string]$Project = "",
    [string]$Program = ""
)
$ErrorActionPreference = "Stop"
$T = $PSScriptRoot
. "$T\env.ps1"

$ghidra = "$T\ghidra"
$jars = @("$T\ghidra-mcp-dist\GhidraMCPHeadless.jar")
$jars += Get-ChildItem -Path "$ghidra\Ghidra\*\*\lib\*.jar" | ForEach-Object { $_.FullName }

# The classpath is far longer than the Windows command-line limit, so pass it through a java @argfile.
$argFile = "$T\ghidra-home\headless-classpath.args"
$cp = ($jars | ForEach-Object { $_ -replace '\\', '/' }) -join ';'
Set-Content -Path $argFile -Value "-classpath `"$cp`"" -Encoding ascii

# Restrict file-path endpoints (import/open project) to the project folder.
$env:GHIDRA_MCP_FILE_ROOT = (Resolve-Path "$T\..").Path

$serverArgs = @("--port", "$Port", "--bind", "127.0.0.1")
if ($Project) { $serverArgs += @("--project", $Project) }
if ($Program) { $serverArgs += @("--program", $Program) }

& "$T\jdk21\bin\java.exe" -Xmx6g -XX:+UseG1GC `
    "-Dghidra.home=$ghidra" `
    "-Dapplication.name=GhidraMCP" `
    "-Dapplication.settingsdir=$T\ghidra-home\settings" `
    "-Dapplication.cachedir=$T\ghidra-home\cache" `
    "-Dapplication.tempdir=$T\ghidra-home\temp" `
    "-Dfile.encoding=UTF8" `
    "@$argFile" `
    com.xebyte.headless.GhidraMCPHeadlessServer @serverArgs
