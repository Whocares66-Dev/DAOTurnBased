<#
.SYNOPSIS
    The merge's gate in one command, its parts side by side.

.DESCRIPTION
    What CLAUDE.md asks before a merge to master, each part a process of its
    own in its own build folder, all started at once:

      debug     .\tools\build.ps1 -Preset debug -Test -Format -Lint
      asan      .\tools\build.ps1 -Preset debug-asan -Test
      analyze   .\tools\build.ps1 -Preset debug-analyze
      core      .\tools\build.ps1 -Preset core -LongFights -Mutants ...

    The last is for a change to src/core's logic: the five hundred long random
    fights and the mutants the branch added or changed (-Mutants, by name).
    With neither -LongFights nor -Mutants it is left out.

    Each part's output goes to build\gate\<part>.txt. A part that fails has
    the end of its output shown, and the script ends with 1.

    The user, 2026-10-10, of a gate whose parts ran one after another behind
    two thousand fights in one process: "Why aren't all these things running
    in parallel?". The format step rewrites sources in place while the other
    parts compile them: on a branch already formatted, as one is by its own
    commits' gate, it rewrites nothing.

.PARAMETER LongFights
    Play the long random fights (a change to src/core's logic).

.PARAMETER Mutants
    The mutants the branch added or changed, by name or part of a name.

.PARAMETER Jobs
    How many compilations each part runs at once. The default shares the
    machine's cores, less two, among the parts.

.EXAMPLE
    .\tools\gate.ps1
    .\tools\gate.ps1 -LongFights -Mutants 'rules: a shot', 'a staff draws'
    powershell -Command ".\tools\gate.ps1 -Mutants 'rules: a shot', 'a staff draws'"   # from another shell
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [switch] $LongFights,
    [string[]] $Mutants = @(),
    [int] $Jobs = 0
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $PSScriptRoot 'build.ps1'
$logs = Join-Path $repo 'build\gate'
New-Item -ItemType Directory -Force $logs | Out-Null

$parts = [ordered]@{
    debug   = "-Preset debug -Test -Format -Lint"
    asan    = "-Preset debug-asan -Test"
    analyze = "-Preset debug-analyze"
}
if ($LongFights -or $Mutants) {
    $core = '-Preset core'
    if ($LongFights) { $core += ' -LongFights' }
    if ($Mutants) {
        # Names have spaces, colons and apostrophes: each a single-quoted
        # string of the command, its own apostrophes doubled.
        $names = ($Mutants | ForEach-Object { "'" + ($_ -replace "'", "''") + "'" }) -join ', '
        $core += " -Mutants $names"
    }
    $parts.core = $core
}
$cores = [Environment]::ProcessorCount
$each = if ($Jobs -gt 0) { $Jobs } else { [Math]::Max(2, [int](($cores - 2) / $parts.Count)) }

Show-Head "The merge's gate: $($parts.Count) parts side by side, $each jobs each"
$began = Get-Date
$running = foreach ($name in $parts.Keys) {
    $out = Join-Path $logs "$name.txt"
    # The whole command encoded, so that no quoting stands between this
    # script and the part's own. Redirected by the process and not inside
    # PowerShell, where a native tool's line on stderr (a CMake warning)
    # under `*>` ends a script that stops on errors.
    $command = "& '$build' $($parts[$name]) -Jobs $each; exit `$LASTEXITCODE"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $process = Start-Process -FilePath 'powershell' -NoNewWindow -PassThru `
        -ArgumentList '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded `
        -RedirectStandardOutput $out -RedirectStandardError "$out.err"
    $null = $process.Handle # held, or ExitCode reads empty once the process has gone
    Show-Info "$name  $($parts[$name])"
    [pscustomobject]@{ Name = $name; Process = $process; Out = $out }
}

$failed = @()
foreach ($one in $running) {
    $one.Process.WaitForExit()
    $took = '{0:n0} s' -f ($one.Process.ExitTime - $began).TotalSeconds
    if ($one.Process.ExitCode -eq 0) {
        Show-Ok "$($one.Name) ($took)"
    }
    else {
        Show-Bad "$($one.Name) ($took): exit $($one.Process.ExitCode); $($one.Out)"
        $failed += $one
    }
}
foreach ($one in $failed) {
    Show-Head "$($one.Name): the end of its output"
    Get-Content $one.Out, "$($one.Out).err" -ErrorAction SilentlyContinue | Select-Object -Last 40 |
        ForEach-Object { Show-Line $_ }
}
if ($failed) {
    Show-Line "`nThe gate failed: $(($failed.Name) -join ', ')" -Colour Red
    exit 1
}
Show-Line ("`nThe gate is green, {0:n0} s" -f ((Get-Date) - $began).TotalSeconds) -Colour Green
