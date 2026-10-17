<#
.SYNOPSIS
    Build DAOTurnBased from any shell -- no "x86 Native Tools Command Prompt" needed.

.DESCRIPTION
    DAOLib's build script, for this repository: it finds Visual Studio, imports the
    32-bit target environment (vcvarsall.bat amd64_x86) into this session, and runs
    CMake with a preset. DAOLib comes from extern\DAOLib (a submodule; run
    `git submodule update --init` after cloning), or ..\DAOLib if there is none.

.PARAMETER Preset
    debug       turnbased.dll, DAOLib's loader (version.dll), and the tests; build\debug
                is then a game folder's worth of files for tools\deploy.ps1.
                The first run compiles spdlog, Detours and Catch2 through vcpkg.
    release     same, optimized, with its symbols: what the game plays, and what
                tools\package.ps1 puts in a release's zip.
    debug-asan  same, under AddressSanitizer; for -Test. The DLLs it makes are
                not for the game (they need the ASan runtime beside the exe).
    debug-analyze
                same, our own targets under MSVC's static analyser (/analyze).
                Its own tree, so the debug tree's compile database, which
                clang-tidy reads, never holds /analyze. Not for -Lint.
    core        src/core and its tests only: no DAOLib, no plugin. The fast loop.
    core-asan   the same under AddressSanitizer.
    core-cov    the same built 64-bit with clang-cl and instrumented for
                coverage (LLVM ships the profile runtime for x64 only);
                with -Coverage, the report. Catch2 is fetched, not vcpkg's.

.PARAMETER Coverage
    After building (and testing, with -Test), run the tests once more under
    the instrumentation and print src/core's coverage, file by file (the
    `coverage` target). core-cov only. The line-by-line view is
    build\core-cov\coverage\html\index.html.

.PARAMETER Format
    After building, rewrite every source in place with its formatter: the
    `format` target (DAOLib's cmake/Quality.cmake, over this tree).

.PARAMETER Lint
    After building (and testing, with -Test), run the linters: clang-tidy over
    every C++ file and PSScriptAnalyzer over the PowerShell. The `tidy` target;
    a finding fails the run.

.PARAMETER LongFights
    After building, play the five hundred long random fights
    (tests\test_fights.cpp, "[fights-long]"): twenty test cases that share
    nothing, as twenty processes side by side. For a change to src/core's
    logic, before a merge.

.PARAMETER Mutants
    After building, break the core on purpose: the mutants whose names (or
    parts of names) are given, by tools\mutants.py, in the compiler's
    environment this script has imported. The ones a branch added or changed,
    before a merge.

.PARAMETER Suffix
    A trial build's mark, appended to the version: -Suffix -ultrawide makes the
    log say 0.2.0-ultrawide, and tools\package.ps1 names the zip so. Set at
    every configure, empty without it, so a later build is the plain version.

.PARAMETER Jobs
    How many compilations to run at once. The default leaves two of the
    machine's cores to everything else.

.EXAMPLE
    .\tools\build.ps1 -Preset debug -Test
    .\tools\build.ps1 -Preset debug -Test -Format -Lint
    .\tools\build.ps1 -Preset debug-asan -Test
    .\tools\build.ps1 -Preset release
    .\tools\build.ps1 -Preset release -Suffix -ultrawide   # a trial for a player
    .\tools\build.ps1 -Preset core -Test
    .\tools\build.ps1 -Preset core -LongFights -Mutants 'rules: a shot', 'a staff draws'
    .\tools\build.ps1 -Preset core-cov -Coverage
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'debug-asan', 'debug-analyze', 'core', 'core-asan', 'core-cov')]
    [string] $Preset = 'debug',
    [switch] $Test,
    [switch] $Coverage,
    [switch] $Format,
    [switch] $Lint,
    [switch] $Fresh,
    [switch] $LongFights,
    [string[]] $Mutants = @(),
    [int] $Jobs = 0,
    [string] $Suffix = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')
if (($Lint -or $Format) -and $Preset -like 'core*') {
    throw 'the format and lint targets are the plugin presets''; use -Preset debug.'
}
if ($Coverage -and $Preset -ne 'core-cov') {
    throw '-Coverage needs -Preset core-cov, the one built with the instrumentation.'
}
# core-cov is the one 64-bit build (the profile runtime); everything else
# targets the game's x86.
$hostTarget = if ($Preset -eq 'core-cov') { 'amd64' } else { 'amd64_x86' }
if ($Lint -and $Preset -eq 'debug-analyze') {
    throw 'clang-tidy cannot read a compile database holding /analyze; lint with -Preset debug.'
}
$repo = Split-Path -Parent $PSScriptRoot

function Get-VcVarsAllPath {
    # Do NOT locate the toolset with `vswhere -latest -requires ...`: a VS install
    # flagged isComplete=False is invisible to it while cl.exe sits on disk. Probe
    # the filesystem and let the presence of vcvarsall.bat be the answer.
    $candidates = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $candidates += & $vswhere -all -prerelease -products * -property installationPath
    }
    foreach ($root in ${env:ProgramFiles}, ${env:ProgramFiles(x86)}) {
        foreach ($ed in 'Enterprise', 'Professional', 'Community', 'BuildTools') {
            $candidates += (Join-Path $root "Microsoft Visual Studio\2022\$ed")
        }
    }
    foreach ($c in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $vcvars = Join-Path $c 'VC\Auxiliary\Build\vcvarsall.bat'
        if (Test-Path $vcvars) { return $vcvars }
    }
    return $null
}

function Import-DeveloperEnvironment {
    $vcvars = Get-VcVarsAllPath
    if (-not $vcvars) {
        throw @'
No Visual Studio 2022 C++ toolset found (no vcvarsall.bat anywhere).

Install the C++ workload, with Visual Studio CLOSED:

  & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe" modify `
      --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" `
      --add Microsoft.VisualStudio.Workload.NativeDesktop `
      --add Microsoft.VisualStudio.Component.VC.ASAN `
      --includeRecommended --quiet --norestart
'@
    }
    Show-Line "Importing developer environment from $vcvars ($hostTarget)" -Colour DarkGray
    # `set` after vcvarsall dumps the whole environment; replay it into this session.
    $output = & ${env:ComSpec} /s /c "`"$vcvars`" $hostTarget >nul 2>&1 && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:\$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
}

# vcvarsall sets VCPKG_ROOT to the vcpkg bundled inside Visual Studio, which is
# NOT the one this project uses. Preserve whatever was set beforehand.
$userVcpkgRoot = $env:VCPKG_ROOT
Import-DeveloperEnvironment
if ($userVcpkgRoot) {
    if ($env:VCPKG_ROOT -ne $userVcpkgRoot) {
        Show-Line "  restoring VCPKG_ROOT=$userVcpkgRoot (vcvarsall had reset it to $env:VCPKG_ROOT)" -Colour DarkGray
    }
    $env:VCPKG_ROOT = $userVcpkgRoot
}

# LLVM's own release first on PATH, not the older one Visual Studio bundles.
$llvmBin = if ($env:LLVM_ROOT) { Join-Path $env:LLVM_ROOT 'bin' } else { Join-Path $env:ProgramFiles 'LLVM\bin' }
if (Test-Path (Join-Path $llvmBin 'clang-tidy.exe')) {
    $env:PATH = "$llvmBin;$env:PATH"
}

foreach ($tool in 'cl', 'cmake', 'ninja') {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) { throw "'$tool' is still not on PATH after importing vcvarsall." }
    Show-Line ("  {0,-6} {1}" -f $tool, $found.Source) -Colour DarkGray
}
if (-not $env:VCPKG_ROOT) { throw 'VCPKG_ROOT is not set; every preset needs vcpkg.' }
if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    throw "vcpkg.exe missing. Run: & '$env:VCPKG_ROOT\bootstrap-vcpkg.bat'"
}

$buildDir = Join-Path $repo "build\$Preset"
if ($Fresh -and (Test-Path $buildDir)) {
    Show-Line "Removing $buildDir" -Colour Yellow
    Remove-Item -Recurse -Force $buildDir
}

$cores = [Environment]::ProcessorCount
$parallel = if ($Jobs -gt 0) { $Jobs } else { [Math]::Max(1, $cores - 2) }
$env:CMAKE_BUILD_PARALLEL_LEVEL = $parallel
(Get-Process -Id $PID).PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
Show-Line "  jobs   $parallel of $cores cores, below normal priority"

Push-Location $repo
try {
    Show-Line "`n== configure ($Preset) ==" -Colour Cyan
    cmake --preset $Preset "-DTB_VERSION_SUFFIX=$Suffix"
    if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

    Show-Line "`n== build ($Preset) ==" -Colour Cyan
    # The linker writes the path of its temporary files into a PDB, and the
    # user's own is under their name (C:\Users\<name>\AppData\Local\Temp);
    # a release's PDB is given to whoever installs it (issue 106, as DAOLib
    # #55 and Eluvian #115). So a build's is in its tree, for the build alone.
    $userTemp = $env:TEMP, $env:TMP
    $env:TEMP = $env:TMP = New-Item -ItemType Directory -Force (Join-Path $buildDir 'tmp')
    try { cmake --build --preset $Preset --parallel $parallel }
    finally { $env:TEMP, $env:TMP = $userTemp }
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

    if ($Test) {
        Show-Line "`n== test ($Preset) ==" -Colour Cyan
        # As many at a time as the build's jobs (the user's ask of every
        # repository, 2026-10-03: tests and mutants in parallel where
        # possible). Each test is a process of its own with nothing shared.
        ctest --preset $Preset --parallel $parallel
        if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
    }

    if ($LongFights) {
        Show-Line "`n== long fights ($Preset) ==" -Colour Cyan
        # Twenty test cases that share nothing, each a twentieth of every lot
        # (tests\test_fights.cpp, kLongParts): one process each, all at once,
        # by Catch2's own sharding of the cases it is asked for. The user,
        # 2026-10-10, of two thousand played one after another in one process
        # on a machine of twenty cores: "parallelize them".
        $parts = 20
        $tests = Join-Path $buildDir 'tests\tb_tests.exe'
        $logs = Join-Path $buildDir 'long-fights'
        New-Item -ItemType Directory -Force $logs | Out-Null
        $began = Get-Date
        $running = foreach ($part in 0..($parts - 1)) {
            $out = Join-Path $logs "part$part.txt"
            $process = Start-Process -FilePath $tests -NoNewWindow -PassThru `
                -ArgumentList '[fights-long]', '--shard-count', $parts, '--shard-index', $part `
                -RedirectStandardOutput $out -RedirectStandardError "$out.err"
            $null = $process.Handle # held, or ExitCode reads empty once the process has gone
            [pscustomobject]@{ Part = $part; Process = $process; Out = $out }
        }
        $failed = @()
        foreach ($one in $running) {
            $one.Process.WaitForExit()
            if ($one.Process.ExitCode -ne 0) { $failed += $one }
        }
        foreach ($one in $failed) {
            Show-Line "`n-- part $($one.Part) --" -Colour Red
            Get-Content $one.Out | Select-Object -Last 40 | ForEach-Object { Show-Line $_ }
        }
        if ($failed) { throw "long fights failed: $($failed.Count) of $parts parts; their output is in $logs" }
        Show-Line ("  {0} parts side by side, {1:n0} s" -f $parts, ((Get-Date) - $began).TotalSeconds)
    }

    if ($Mutants) {
        Show-Line "`n== mutants ==" -Colour Cyan
        python (Join-Path $repo 'tools\mutants.py') @Mutants
        if ($LASTEXITCODE -ne 0) { throw "mutants failed ($LASTEXITCODE)" }
    }

    if ($Format) {
        Show-Line "`n== format ==" -Colour Cyan
        cmake --build --preset $Preset --target format
        if ($LASTEXITCODE -ne 0) { throw "format failed ($LASTEXITCODE)" }
    }

    if ($Coverage) {
        Show-Line "`n== coverage ($Preset) ==" -Colour Cyan
        cmake --build --preset $Preset --target coverage
        if ($LASTEXITCODE -ne 0) { throw "coverage failed ($LASTEXITCODE)" }
    }

    if ($Lint) {
        Show-Line "`n== lint ($Preset) ==" -Colour Cyan
        cmake --build --preset $Preset --parallel $parallel --target tidy
        if ($LASTEXITCODE -ne 0) { throw "lint failed ($LASTEXITCODE)" }
    }
    Show-Line "`nOK" -Colour Green
}
finally {
    Pop-Location
}
