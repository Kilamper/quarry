<#
.SYNOPSIS
    Idempotent Windows dev-environment setup: MSVC Build Tools, CMake, Ninja, Git,
    Python, aqtinstall, Qt (version from .qt-version) and vcpkg.
    Only installs what is missing. Re-running is safe.

.PARAMETER NonInteractive
    Never prompt (CI). Steps that need confirmation are performed automatically.
.PARAMETER SkipCompiler
    Do not install MSVC Build Tools (e.g. CI images that already ship Visual Studio).
.PARAMETER QtBaseDir
    Root folder for aqt installs. Default C:\Qt (must match CMakePresets.json).
.PARAMETER VcpkgRoot
    vcpkg checkout. Default: $env:VCPKG_ROOT, else C:\vcpkg.
#>
[CmdletBinding()]
param(
    [switch]$NonInteractive,
    [switch]$SkipCompiler,
    [string]$QtBaseDir = 'C:\Qt',
    [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\vcpkg' })
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$QtVersion = (Get-Content (Join-Path $RepoRoot '.qt-version') -TotalCount 1).Trim()
$QtArch = 'win64_msvc2022_64'
$QtDir = Join-Path $QtBaseDir "$QtVersion\msvc2022_64"
$AqtRequirements = Join-Path $PSScriptRoot 'requirements-aqt.txt'
$MinCMake = [version]'3.24'
$Summary = [System.Collections.Generic.List[object]]::new()

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }

function Confirm-Step([string]$Message) {
    if ($NonInteractive) { return $true }
    $answer = Read-Host "$Message [y/N]"
    return $answer -match '^(y|s|yes|si)$'
}

function Add-Result([string]$Tool, [string]$Status, [string]$Detail) {
    $Summary.Add([pscustomobject]@{ Tool = $Tool; Status = $Status; Detail = $Detail })
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Install-Winget([string]$Id, [string[]]$ExtraArgs = @()) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        # GitHub-hosted Windows runners ship Chocolatey but not winget.
        $chocoIds = @{ 'Kitware.CMake' = 'cmake'; 'Ninja-build.Ninja' = 'ninja'; 'Git.Git' = 'git'; 'Python.Python.3.14' = 'python' }
        if ((Get-Command choco -ErrorAction SilentlyContinue) -and $chocoIds.ContainsKey($Id)) {
            & choco install $chocoIds[$Id] -y --no-progress
            if ($LASTEXITCODE -ne 0) { throw "choco install $($chocoIds[$Id]) failed with exit code $LASTEXITCODE" }
            Update-SessionPath
            return
        }
        throw "winget not found. Install 'App Installer' from the Microsoft Store and re-run."
    }
    $wingetArgs = @('install', '--id', $Id, '-e', '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity') + $ExtraArgs
    & winget @wingetArgs
    # -1978335189 = APPINSTALLER_CLI_ERROR_UPDATE_NOT_APPLICABLE (already installed, no upgrade)
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
        throw "winget install $Id failed with exit code $LASTEXITCODE"
    }
    Update-SessionPath
}

function Get-VsWhere { Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe' }

function Get-MsvcInstallPath {
    $vswhere = Get-VsWhere
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return $path.Trim() } else { return $null }
}

function Get-PythonExe {
    # 'python3' on Windows is often the Microsoft Store alias stub, so prefer py / python.
    foreach ($candidate in 'py', 'python') {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notlike '*WindowsApps*') { return $cmd.Source }
    }
    return $null
}

# Pick up tools installed since this shell started (winget updates the registry PATH only).
Update-SessionPath

# --- 1. MSVC -----------------------------------------------------------------
Write-Step 'MSVC (C++20)'
$vsPath = Get-MsvcInstallPath
if ($vsPath) {
    Add-Result 'MSVC' 'ok' $vsPath
} elseif ($SkipCompiler) {
    Add-Result 'MSVC' 'skipped' '-SkipCompiler'
} elseif (Confirm-Step 'Install Visual Studio 2022 Build Tools (C++ workload, needs admin/UAC, ~3-6 GB)?') {
    Install-Winget 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
        '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vsPath = Get-MsvcInstallPath
    if (-not $vsPath) { throw 'Build Tools installed but vswhere cannot find the VC tools component.' }
    Add-Result 'MSVC' 'installed' $vsPath
} else {
    Add-Result 'MSVC' 'missing' 'declined'
}

# --- 2. CMake / Ninja / Git ----------------------------------------------------
Write-Step 'CMake'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeVersion = if ($cmake) { [version](((& cmake --version)[0] -split ' ')[2] -replace '-.*$', '') } else { $null }
if ($cmakeVersion -and $cmakeVersion -ge $MinCMake) {
    Add-Result 'CMake' 'ok' "$cmakeVersion"
} else {
    Install-Winget 'Kitware.CMake'
    Add-Result 'CMake' 'installed' (((& cmake --version)[0] -split ' ')[2])
}

Write-Step 'Ninja'
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    Add-Result 'Ninja' 'ok' (& ninja --version)
} else {
    Install-Winget 'Ninja-build.Ninja'
    Add-Result 'Ninja' 'installed' (& ninja --version)
}

Write-Step 'Git'
if (Get-Command git -ErrorAction SilentlyContinue) {
    Add-Result 'Git' 'ok' ((& git --version) -replace 'git version ', '')
} else {
    Install-Winget 'Git.Git'
    Add-Result 'Git' 'installed' ((& git --version) -replace 'git version ', '')
}

# --- 3. Python + aqtinstall ---------------------------------------------------
Write-Step 'Python 3'
$python = Get-PythonExe
if ($python) {
    Add-Result 'Python' 'ok' (& $python --version)
} else {
    Install-Winget 'Python.Python.3.14'
    $python = Get-PythonExe
    if (-not $python) { throw 'Python installed but not found on PATH. Open a new shell and re-run.' }
    Add-Result 'Python' 'installed' (& $python --version)
}

Write-Step 'aqtinstall (pinned in scripts/requirements-aqt.txt)'
# pip itself is idempotent: a satisfied pinned requirement is a no-op.
& $python -m pip install --user --disable-pip-version-check --quiet -r $AqtRequirements
if ($LASTEXITCODE -ne 0) { throw 'pip install of aqtinstall failed.' }
Add-Result 'aqtinstall' 'ok' ((& $python -m pip show aqtinstall | Select-String '^Version:') -replace 'Version:\s*', '')

# --- 4. Qt --------------------------------------------------------------------
Write-Step "Qt $QtVersion ($QtArch)"
$qtReady = (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6Quick')) -and (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6ShaderTools'))
if ($qtReady) {
    Add-Result 'Qt' 'ok' $QtDir
} else {
    & $python -m aqt install-qt windows desktop $QtVersion $QtArch -m qtshadertools -O $QtBaseDir
    if ($LASTEXITCODE -ne 0) { throw "aqt install-qt $QtVersion $QtArch failed." }
    Add-Result 'Qt' 'installed' $QtDir
}

# --- 5. vcpkg -----------------------------------------------------------------
Write-Step "vcpkg ($VcpkgRoot)"
$vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path (Join-Path $VcpkgRoot '.vcpkg-root'))) {
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw 'git clone vcpkg failed.' }
}
if (-not (Test-Path $vcpkgExe)) {
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'bootstrap-vcpkg failed.' }
}
Add-Result 'vcpkg' 'ok' ((& $vcpkgExe version | Select-Object -First 1) -replace '^.*version ', '')

if ([Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User') -ne $VcpkgRoot -and -not $env:CI) {
    if (Confirm-Step "Set user environment variable VCPKG_ROOT=$VcpkgRoot ?") {
        [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, 'User')
    }
}
$env:VCPKG_ROOT = $VcpkgRoot
Add-Result 'VCPKG_ROOT' 'ok' $VcpkgRoot

# --- Summary ------------------------------------------------------------------
$Summary | Format-Table -AutoSize | Out-String | Write-Host
if ($vsPath) {
    Write-Host 'Ninja + MSVC need the Developer environment. In a new PowerShell run:'
    Write-Host "  & '$vsPath\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation"
    Write-Host '  cmake --preset windows-debug; cmake --build --preset windows-debug; ctest --preset windows-debug'
}
