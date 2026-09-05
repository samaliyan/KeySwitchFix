# Local Windows build of KeySwitchFix — the same steps as build-native.sh,
# for checking a change before pushing it to GitHub.
#
# Requirements (one-time):
#   1. Python 3   — https://www.python.org/downloads/  (tick "Add python.exe to PATH")
#   2. Zig 0.12+  — https://ziglang.org/download/  → "zig-windows-x86_64-*.zip",
#                   extract so that C:\zig\zig.exe exists (or pass -Zig <path>)
#
# Run from PowerShell in the source folder:
#   powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
#
# Output: dist\KeySwitchFix-Setup.exe, dist\KeySwitchFix.exe,
#         dist\KeySwitchFix-Uninstall.exe, dist\SHA256SUMS.txt

param(
    [string]$Zig = "",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "`nFAILED: $text" -ForegroundColor Red; exit 1 }
function Run($exe, $arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { Fail "$exe $($arguments -join ' ')" }
}

# --- locate tools ---------------------------------------------------------
$python = $null
foreach ($candidate in @("python", "py", "python3")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) { $python = $candidate; break }
}
if (-not $python) { Fail "Python 3 was not found. Install it from python.org and tick 'Add to PATH'." }

if (-not $Zig) {
    foreach ($candidate in @("C:\zig\zig.exe", "$env:USERPROFILE\zig\zig.exe", "zig")) {
        if (($candidate -eq "zig" -and (Get-Command zig -ErrorAction SilentlyContinue)) -or
            ($candidate -ne "zig" -and (Test-Path $candidate))) { $Zig = $candidate; break }
    }
}
if (-not $Zig) { Fail "zig.exe was not found. Extract the Zig zip to C:\zig or pass -Zig C:\path\to\zig.exe" }

Step "Tools"
& $python --version
& $Zig version
if ($LASTEXITCODE -ne 0) { Fail "zig did not run" }

$env:ZIG_GLOBAL_CACHE_DIR = "$env:TEMP\keyswitchfix-zig-global"
$env:ZIG_LOCAL_CACHE_DIR  = "$env:TEMP\keyswitchfix-zig-local"
New-Item -ItemType Directory -Force -Path dist | Out-Null

# --- spelling rank tables (wordfreq) ---------------------------------------
if (-not (Test-Path resources\en-rank.bin) -or -not (Test-Path resources\fa-rank.bin)) {
    Step "Installing wordfreq 3.1.1 (only needed once) and generating rank tables"
    & $python -m pip install --quiet wordfreq==3.1.1
    if ($LASTEXITCODE -ne 0) { Fail "pip install wordfreq==3.1.1" }
    Run $python @("tools\generate_rank_tables.py", "--if-missing")
}

Step "Metadata verification"
Run $python @("tests\verify_metadata.py")

$common = @("cc", "-target", "x86_64-windows-gnu", "-DUNICODE", "-D_UNICODE", "-std=c11", "-O2",
            "-Wall", "-Wextra", "-Werror", "-Isrc", "-Iresources")

# --- native tests (compiled with zig for Windows, run here) -----------------
if (-not $SkipTests) {
    Step "Core tests"
    Run $Zig ($common + @("src\core.c", "tests\core_tests.c", "-o", "dist\core_tests.exe"))
    Run "dist\core_tests.exe" @()
    Step "Spelling tests"
    Run $Zig ($common + @("src\core.c", "src\spell.c", "tests\spell_tests.c", "-o", "dist\spell_tests.exe"))
    Run "dist\spell_tests.exe" @()
}

# --- application ----------------------------------------------------------
Step "KeySwitchFix.exe"
Push-Location resources
Run $Zig @("rc", "/:auto-includes", "gnu", "/c", "65001", "/fo", "app.res", "app.rc")
Pop-Location
Run $Zig ($common + @("src\app.c", "src\core.c", "src\spell.c", "resources\app.res",
          "-o", "dist\KeySwitchFix.exe",
          "-luser32", "-lgdi32", "-lcomctl32", "-lshell32", "-ladvapi32", "-Wl,/subsystem:windows"))

Step "KeySwitchFix-Uninstall.exe"
Push-Location resources
Run $Zig @("rc", "/:auto-includes", "gnu", "/c", "65001", "/fo", "uninstaller.res", "uninstaller.rc")
Pop-Location
Run $Zig ($common + @("src\installer.c", "resources\uninstaller.res",
          "-o", "dist\KeySwitchFix-Uninstall.exe",
          "-luser32", "-lshell32", "-ladvapi32", "-lole32", "-luuid", "-Wl,/subsystem:windows"))

Step "KeySwitchFix-Setup.exe"
Push-Location resources
Run $Zig @("rc", "/:auto-includes", "gnu", "/c", "65001", "/fo", "installer.res", "installer.rc")
Pop-Location
Run $Zig ($common + @("src\installer.c", "resources\installer.res",
          "-o", "dist\KeySwitchFix-Setup.exe",
          "-luser32", "-lshell32", "-ladvapi32", "-lole32", "-luuid", "-Wl,/subsystem:windows"))

Step "PE verification"
Run $python @("tests\verify_pe.py")

Step "Checksums"
$lines = foreach ($name in @("KeySwitchFix-Setup.exe", "KeySwitchFix-Uninstall.exe", "KeySwitchFix.exe")) {
    $hash = (Get-FileHash "dist\$name" -Algorithm SHA256).Hash.ToLower()
    "$hash  $name"
}
$lines | Set-Content -Encoding ascii dist\SHA256SUMS.txt
$lines

Write-Host "`nDone. Run dist\KeySwitchFix-Setup.exe to install and test." -ForegroundColor Green
