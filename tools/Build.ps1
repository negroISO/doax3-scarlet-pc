param(
    [string]$MSYS2Root = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }),
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$profile = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'project.json') | ConvertFrom-Json
$bash = Join-Path $MSYS2Root 'usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash)) { throw 'MSYS2 bash not found. Pass -MSYS2Root C:\path\to\msys64.' }
$env:DOAX3_REPO_ROOT = $repoRoot
$env:DOAX3_MSYS_ROOT = $MSYS2Root
$env:DOAX3_BUILD_TARGET = $profile.target
$env:DOAX3_BUILD_JOBS = "$Jobs"
$scriptPath = (Join-Path $PSScriptRoot 'Build-Windows.sh').Replace('\','/')
& $bash --noprofile --norc $scriptPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built $($profile.name): source/out/gpu/$($profile.target).exe"
