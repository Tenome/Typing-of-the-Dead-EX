# Copies this project's Claude Code memory into your own Claude Code profile so a new session in this
# folder starts with the same working knowledge. Run from anywhere: powershell -File claude\install_memory.ps1
$root = Split-Path -Parent $PSScriptRoot
$encoded = ($root -replace '[^A-Za-z0-9]', '-')
$target = Join-Path $env:USERPROFILE ".claude\projects\$encoded\memory"
New-Item -ItemType Directory -Force $target | Out-Null
foreach ($file in Get-ChildItem (Join-Path $PSScriptRoot "memory") -Filter *.md) {
    $destination = Join-Path $target $file.Name
    if ((Test-Path $destination) -and $file.Name -ne "MEMORY.md") {
        Write-Host "kept existing $($file.Name)"
        continue
    }
    Copy-Item $file.FullName $destination -Force
    Write-Host "installed $($file.Name)"
}
Write-Host "Memory folder: $target"
