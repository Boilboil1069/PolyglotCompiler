<#
.SYNOPSIS
    PolyglotCompiler third-party dependency cacher (Windows / PowerShell).

.DESCRIPTION
    Pre-downloads every FetchContent dependency declared in Dependencies.cmake
    into "<repo>/.cache/deps/<name>/" so subsequent CMake configurations (and
    the packaging scripts) do not need network access. Each dependency is
    fetched at the exact tag pinned in Dependencies.cmake; an existing cache
    entry already at the correct tag is left untouched.

    Two transport strategies are tried per dependency, in order:
      1. shallow "git clone --depth 1 --branch <tag>"
      2. HTTPS tarball from "https://codeload.github.com/<owner>/<repo>/tar.gz/refs/tags/<tag>"

    Each strategy is retried with exponential backoff (5s / 15s / 45s) so a
    transient timeout does not abort the whole run.

    Optional GitHub mirror prefixes can be supplied through -Mirror, which
    rewrites the canonical GitHub URL (handy in restricted networks).

.PARAMETER Refresh
    Re-fetch every dependency, even if a matching cache entry already exists.

.PARAMETER OnlyMissing
    Only fetch dependencies that are missing or mis-tagged. (Default behaviour.)

.PARAMETER Mirror
    URL prefix used as a GitHub mirror. The string "https://github.com/" in
    every dependency URL is replaced by this prefix. Example:
        -Mirror "https://gitclone.com/github.com/"

.PARAMETER CacheRoot
    Override the cache root directory. Defaults to "<repo>/.cache/deps".

.PARAMETER Quiet
    Suppress per-step progress lines (errors are still printed).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1
    powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 -Refresh
    powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 -Mirror "https://gitclone.com/github.com/"
#>

[CmdletBinding()]
param(
    [switch]$Refresh,
    [switch]$OnlyMissing,
    [string]$Mirror    = "",
    [string]$CacheRoot = "",
    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $CacheRoot -or $CacheRoot -eq "") {
    $CacheRoot = Join-Path $ProjectRoot ".cache/deps"
}
$ManifestPath = Join-Path $CacheRoot "manifest.json"

# Single source of truth, kept in lock-step with Dependencies.cmake.
# A change here must be mirrored there (and vice versa).
$DepSpecs = @(
    [pscustomobject]@{ Name = "fmt";           FcName = "fmt";           Owner = "fmtlib";    Repo = "fmt";    Tag = "11.2.0" }
    [pscustomobject]@{ Name = "nlohmann_json"; FcName = "nlohmann_json"; Owner = "nlohmann";  Repo = "json";   Tag = "v3.11.3" }
    [pscustomobject]@{ Name = "Catch2";        FcName = "Catch2";        Owner = "catchorg";  Repo = "Catch2"; Tag = "v3.5.4" }
    [pscustomobject]@{ Name = "mimalloc";      FcName = "mimalloc";      Owner = "microsoft"; Repo = "mimalloc"; Tag = "v2.1.7" }
)

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    if ($Quiet) { return }
    Write-Host ""
    Write-Host "=========================================="  -ForegroundColor Cyan
    Write-Host " $msg"                                       -ForegroundColor Cyan
    Write-Host "=========================================="  -ForegroundColor Cyan
}

function Write-Info([string]$msg) {
    if (-not $Quiet) { Write-Host "  $msg" }
}

function Write-Ok([string]$msg) {
    if (-not $Quiet) { Write-Host "  [OK] $msg" -ForegroundColor Green }
}

function Write-Warn2([string]$msg) {
    Write-Host "  [!]  $msg" -ForegroundColor Yellow
}

function Convert-MirrorUrl([string]$url) {
    if ([string]::IsNullOrWhiteSpace($Mirror)) { return $url }
    $prefix = "https://github.com/"
    if ($url.StartsWith($prefix)) {
        return ($Mirror.TrimEnd('/') + "/" + $url.Substring($prefix.Length))
    }
    return $url
}

function Get-DepGitUrl([pscustomobject]$dep) {
    return Convert-MirrorUrl "https://github.com/$($dep.Owner)/$($dep.Repo).git"
}

function Get-DepTarballUrl([pscustomobject]$dep) {
    return Convert-MirrorUrl "https://github.com/$($dep.Owner)/$($dep.Repo)/archive/refs/tags/$($dep.Tag).tar.gz"
}

function Read-Manifest() {
    if (-not (Test-Path $ManifestPath)) { return @{} }
    try {
        $raw = Get-Content -Raw -LiteralPath $ManifestPath
        if ([string]::IsNullOrWhiteSpace($raw)) { return @{} }
        $obj = $raw | ConvertFrom-Json
        $h = @{}
        foreach ($p in $obj.PSObject.Properties) { $h[$p.Name] = $p.Value }
        return $h
    } catch {
        Write-Warn2 "Manifest unreadable, treating as empty: $($_.Exception.Message)"
        return @{}
    }
}

function Write-Manifest([hashtable]$manifest) {
    $manifest | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $ManifestPath -Encoding UTF8
}

function Test-CachedDep([pscustomobject]$dep, [hashtable]$manifest) {
    $dst = Join-Path $CacheRoot $dep.Name
    if (-not (Test-Path (Join-Path $dst "CMakeLists.txt"))) { return $false }
    if (-not $manifest.ContainsKey($dep.Name)) { return $false }
    $entry = $manifest[$dep.Name]
    return ($entry.tag -eq $dep.Tag)
}

function Remove-DepDir([string]$path) {
    if (Test-Path $path) {
        # Git pack files often have read-only attributes on Windows.
        Get-ChildItem -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue |
            ForEach-Object { try { $_.Attributes = 'Normal' } catch {} }
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
    }
}

function Invoke-GitCloneStrategy([pscustomobject]$dep, [string]$dst) {
    $url = Get-DepGitUrl $dep
    Write-Info "git clone --depth 1 --branch $($dep.Tag) $url"
    $tmp = "$dst.partial-$([System.IO.Path]::GetRandomFileName())"
    Remove-DepDir $tmp
    $argsList = @('clone', '--depth', '1', '--single-branch',
                  '--branch', $dep.Tag, '--config', 'advice.detachedHead=false',
                  $url, $tmp)
    & git @argsList 2>&1 | ForEach-Object {
        if (-not $Quiet) { Write-Host "      $_" -ForegroundColor DarkGray }
    }
    $rc = $LASTEXITCODE
    if ($rc -ne 0) {
        Remove-DepDir $tmp
        return $false
    }
    Remove-DepDir $dst
    Move-Item -LiteralPath $tmp -Destination $dst -Force
    return $true
}

function Invoke-TarballStrategy([pscustomobject]$dep, [string]$dst) {
    $url = Get-DepTarballUrl $dep
    Write-Info "https GET $url"

    $tmp = "$dst.partial-$([System.IO.Path]::GetRandomFileName())"
    Remove-DepDir $tmp
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    $tarFile = Join-Path $tmp "src.tar.gz"
    try {
        $oldPP = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $url -OutFile $tarFile -UseBasicParsing -TimeoutSec 120
        $ProgressPreference = $oldPP
    } catch {
        Write-Warn2 "Download failed: $($_.Exception.Message)"
        Remove-DepDir $tmp
        return $false
    }

    # Use BSD tar bundled with modern Windows; fall back to System.IO.Compression for .zip.
    $tarExe = (Get-Command tar -ErrorAction SilentlyContinue)
    if (-not $tarExe) {
        Write-Warn2 "'tar' is required for tarball extraction but was not found in PATH."
        Remove-DepDir $tmp
        return $false
    }

    $extractDir = Join-Path $tmp "extract"
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    & tar.exe -xzf $tarFile -C $extractDir
    if ($LASTEXITCODE -ne 0) {
        Write-Warn2 "tar extraction failed"
        Remove-DepDir $tmp
        return $false
    }

    # GitHub tarballs unpack into "<repo>-<tag-without-v>/".
    $inner = Get-ChildItem -LiteralPath $extractDir -Directory | Select-Object -First 1
    if (-not $inner) {
        Write-Warn2 "Tarball did not contain a top-level directory"
        Remove-DepDir $tmp
        return $false
    }
    if (-not (Test-Path (Join-Path $inner.FullName "CMakeLists.txt"))) {
        Write-Warn2 "Extracted tarball is missing CMakeLists.txt at the top level"
        Remove-DepDir $tmp
        return $false
    }

    Remove-DepDir $dst
    Move-Item -LiteralPath $inner.FullName -Destination $dst -Force
    Remove-DepDir $tmp
    return $true
}

function Invoke-DepFetch([pscustomobject]$dep) {
    $dst = Join-Path $CacheRoot $dep.Name
    $strategies = @(
        @{ Name = "git";     Action = { param($d, $p) Invoke-GitCloneStrategy $d $p } },
        @{ Name = "tarball"; Action = { param($d, $p) Invoke-TarballStrategy  $d $p } }
    )

    foreach ($strat in $strategies) {
        $delaySeconds = 5
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            Write-Info ("attempt {0}/{1} via {2}" -f $attempt, 3, $strat.Name)
            $ok = & $strat.Action $dep $dst
            if ($ok) { return $strat.Name }
            if ($attempt -lt 3) {
                Write-Warn2 ("retrying in {0}s ..." -f $delaySeconds)
                Start-Sleep -Seconds $delaySeconds
                $delaySeconds *= 3
            }
        }
        Write-Warn2 ("strategy '{0}' exhausted, falling back" -f $strat.Name)
    }

    return $null
}

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------
Write-Step "Dependency cache"
Write-Info "cache root : $CacheRoot"
if ($Mirror) { Write-Info "mirror     : $Mirror" }
if ($Refresh) { Write-Info "mode       : refresh (re-fetch everything)" }

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Warn2 "'git' was not found in PATH; only the tarball transport will be available."
}

New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
$manifest = Read-Manifest

# ---------------------------------------------------------------------------
# Migrate any existing build/_deps/<name>-src into the cache for free, so a
# project that has already configured once does not need to download again.
# ---------------------------------------------------------------------------
$candidateBuilds = @(
    (Join-Path $ProjectRoot "build/_deps"),
    (Join-Path $ProjectRoot "build-release/_deps")
)
foreach ($dep in $DepSpecs) {
    $cacheDir = Join-Path $CacheRoot $dep.Name
    if ((Test-CachedDep $dep $manifest) -and (-not $Refresh)) { continue }
    if (Test-Path (Join-Path $cacheDir "CMakeLists.txt")) { continue }
    foreach ($buildDeps in $candidateBuilds) {
        $candidate = Join-Path $buildDeps "$($dep.Name.ToLowerInvariant())-src"
        if (Test-Path (Join-Path $candidate "CMakeLists.txt")) {
            Write-Info "[$($dep.Name)] importing existing source from $candidate"
            New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
            # Robocopy is faster and survives long paths better than Copy-Item.
            $rcArgs = @($candidate, $cacheDir, '/MIR', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:1', '/W:1')
            & robocopy @rcArgs | Out-Null
            $rc = $LASTEXITCODE
            # robocopy uses bit-flag exit codes; <8 means success.
            if ($rc -ge 8) {
                Write-Warn2 "[$($dep.Name)] robocopy returned $rc, falling back to network"
                Remove-DepDir $cacheDir
            } else {
                $manifest[$dep.Name] = @{
                    tag        = $dep.Tag
                    source     = "imported:$candidate"
                    fetched_at = (Get-Date).ToString("o")
                }
            }
            break
        }
    }
}
Write-Manifest $manifest

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
$failures = @()
foreach ($dep in $DepSpecs) {
    $needFetch = $Refresh -or (-not (Test-CachedDep $dep $manifest))
    if (-not $needFetch) {
        Write-Step "$($dep.Name) @ $($dep.Tag)"
        Write-Ok "cache hit"
        continue
    }

    Write-Step "$($dep.Name) @ $($dep.Tag)"
    $strategyUsed = Invoke-DepFetch $dep
    if ($null -eq $strategyUsed) {
        $failures += $dep.Name
        Write-Warn2 "FAILED to fetch $($dep.Name) at $($dep.Tag) via every transport"
        continue
    }

    $manifest[$dep.Name] = @{
        tag        = $dep.Tag
        source     = $strategyUsed
        fetched_at = (Get-Date).ToString("o")
    }
    Write-Manifest $manifest
    Write-Ok "fetched via $strategyUsed"
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Step "Summary"
foreach ($dep in $DepSpecs) {
    $entry = $manifest[$dep.Name]
    if ($entry -and $entry.tag -eq $dep.Tag -and (Test-Path (Join-Path (Join-Path $CacheRoot $dep.Name) "CMakeLists.txt"))) {
        Write-Ok "$($dep.Name)  $($dep.Tag)  ($($entry.source))"
    } else {
        Write-Warn2 "$($dep.Name)  MISSING"
    }
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "One or more dependencies could not be fetched:" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Tips:" -ForegroundColor Yellow
    Write-Host "  - Re-run with -Mirror 'https://gitclone.com/github.com/' to use a GitHub mirror."
    Write-Host "  - Check your proxy / firewall settings."
    Write-Host "  - Manually clone the missing repo into '<repo>/.cache/deps/<name>/' and re-run."
    exit 2
}

Write-Host ""
Write-Host "Dependency cache is ready at: $CacheRoot" -ForegroundColor Green
exit 0
