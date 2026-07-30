[CmdletBinding()]
param(
    [ValidateSet("codex", "antigravity-cli", "antigravity-ide")]
    [string]$Target = "codex",

    [ValidateSet("User", "Project")]
    [string]$Scope = "User",

    [string]$Project = (Get-Location).Path,

    [ValidateSet("Copy", "Link")]
    [string]$Mode = "Copy",

    [ValidateSet("none", "global", "workspace")]
    [string]$InstallAgents = "none",

    [switch]$Force,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$Python = Get-Command python3 -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
}
if (-not $Python) {
    throw "Не найден Python 3.10 или новее."
}

$Arguments = @(
    (Join-Path $PSScriptRoot "install_skill.py"),
    "--target", $Target,
    "--scope", $Scope.ToLowerInvariant(),
    "--mode", $Mode.ToLowerInvariant(),
    "--install-agents", $InstallAgents,
    "--pretty"
)

if ($Scope -eq "Project") {
    $Arguments += @("--repo", [System.IO.Path]::GetFullPath($Project))
}
if ($Force) {
    $Arguments += "--force"
}
if ($DryRun) {
    $Arguments += "--dry-run"
}

& $Python.Source @Arguments
exit $LASTEXITCODE
