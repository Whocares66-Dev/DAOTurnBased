<#
.SYNOPSIS
    Make the release zip the mod is installed from.

.DESCRIPTION
    After `.\tools\build.ps1 -Preset release`, writes build\package\DAOTurnBased-<version>.zip,
    laid out as the game's install folder, so it is extracted there (beside bin_ship) as
    DAOLib's own zip is. It holds the plugin's folder and nothing else:

        bin_ship\daolib\plugins\turnbased\turnbased.dll
        bin_ship\daolib\plugins\turnbased\turnbased.pdb          so a crash log names the mod's frames
        bin_ship\daolib\plugins\turnbased\turnorder.swf          the portrait bar's movie
        bin_ship\daolib\plugins\turnbased\turnbased_tactics.swf  the tactics page's box's
        bin_ship\daolib\plugins\turnbased\override\              the movement preview's rings and red marker,
                                                                 and a Veil Tear's portrait
        bin_ship\daolib\plugins\turnbased\README.md, LICENSE.txt, VERSION.txt

    No loader and no address files: those are DAOLib's zip, which a game has first. The
    texts are in the plugin's folder and not at the zip's root, which is the game's folder
    and where DAOLib's zip has a README.txt of its own.

    The folder is the build's, which the build makes whole on every run (CMakeLists.txt,
    FILES and OVERRIDE), and this refuses one that is not: a movie missing (python
    tools\movie.py, then the build) or built since the build, or the override subfolder
    short of a file. README.md is the repository's own. VERSION.txt is what
    the README cannot say: the release, and the DAOLib it was built on, which is the tag
    extern\DAOLib is at, where a release holds it (dev/RELEASING.md).

    The version is the project's (CMakeLists.txt). Nothing is copied into the game.

.EXAMPLE
    .\tools\package.ps1                 # from build\release
    .\tools\package.ps1 -Preset debug   # a debug build's, for a trial
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $Preset = 'release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')
. (Join-Path $PSScriptRoot 'daolib_release.ps1')
$repo = Split-Path -Parent $PSScriptRoot

# A file's SHA-256. Not Get-FileHash: after tools\build.ps1 has taken the compiler's
# environment into the session (as tools\release.ps1 runs the two), Windows PowerShell
# no longer finds that cmdlet's module.
function Get-Sha256([string] $Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))) }
    finally { $sha.Dispose() }
}

$built = Join-Path $repo "build\$Preset\daolib\plugins\turnbased"
$builtPlugin = Join-Path $built 'turnbased.dll'
if (-not (Test-Path $builtPlugin)) { throw "No $builtPlugin; run .\tools\build.ps1 -Preset $Preset first" }
$builtPdb = Join-Path $built 'turnbased.pdb'
if (-not (Test-Path $builtPdb)) { throw "No $builtPdb; a crash log names none of the mod's frames without it" }

# The movies, each the one tools\movie.py last built: one built since the build is not
# beside the dll yet, and the plugin and its movies are a pair (tools\deploy.ps1).
$movies = 'turnorder.swf', 'turnbased_tactics.swf'
foreach ($movie in $movies) {
    $beside = Join-Path $built $movie
    $made = Join-Path $repo "build\ui\$movie"
    if (-not (Test-Path $made)) { throw "No $made; run python tools\movie.py, then the build" }
    if (-not (Test-Path $beside) -or (Get-Sha256 $beside) -ne (Get-Sha256 $made)) {
        throw "$beside is not build\ui\$movie as it is now; run .\tools\build.ps1 -Preset $Preset again"
    }
}

# The override subfolder, whole: every file CMakeLists.txt names for it. Without the
# rings' the mod draws a path as the movie's line, which a release is not made to do.
$cmake = Get-Content -Raw (Join-Path $repo 'CMakeLists.txt')
$named = [regex]::Matches($cmake, '(?m)^\s+(?:ui/reticles|ui/portraits)/(\S+)\s*$') | ForEach-Object { $_.Groups[1].Value }
if (-not $named) { throw 'No OVERRIDE files read out of CMakeLists.txt' }
$override = Join-Path $built 'override'
$missing = $named | Where-Object { -not (Test-Path (Join-Path $override $_)) }
if ($missing) { throw "Not in ${override}: $($missing -join ', '); run .\tools\build.ps1 -Preset $Preset again" }
$extra = Get-ChildItem $override -File | Where-Object { $_.Name -notin $named }
if ($extra) { throw "In $override and not in CMakeLists.txt: $($extra.Name -join ', ')" }

if ($cmake -notmatch 'project\((\w+) VERSION (\d+\.\d+\.\d+)') { throw 'No version in CMakeLists.txt' }
$version = $Matches[2]
$name = "$($Matches[1])-$version"

# The loader and the address files a game has are a DAOLib release's, so VERSION.txt
# says which this was built on. At no tag it is a commit's, which a trial may be and a
# release is not.
$daolib = Join-Path $repo 'extern\DAOLib'
$daolibVersion = Get-DAOLibRelease $daolib
$pluginApi = Get-DAOLibPluginApi $daolib
if ($daolibVersion) {
    $builtOn = "DAOLib $daolibVersion"
}
else {
    $builtOn = "DAOLib at its commit $(git -C $daolib rev-parse --short HEAD), between releases"
    Show-Warn 'extern\DAOLib is at no release''s tag; a release of the mod holds it at one'
}

$out = Join-Path $repo 'build\package'
$stage = Join-Path $out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$plugin = Join-Path $stage 'bin_ship\daolib\plugins\turnbased'
New-Item -ItemType Directory -Force (Join-Path $plugin 'override') | Out-Null

# By name, not the folder as built: a debug build leaves the linker's .ilk there.
Copy-Item -Force $builtPlugin, $builtPdb $plugin
foreach ($movie in $movies) { Copy-Item -Force (Join-Path $built $movie) $plugin }
foreach ($file in $named) { Copy-Item -Force (Join-Path $override $file) (Join-Path $plugin 'override') }
Copy-Item -Force (Join-Path $repo 'README.md') $plugin
Copy-Item -Force (Join-Path $repo 'LICENSE') (Join-Path $plugin 'LICENSE.txt')

$versionText = @"
DAOTurnBased $version
https://github.com/Whocares66-Dev/DAOTurnBased/releases
Guide: https://whocares66-dev.github.io/DAOTurnBased/

Built on $builtOn, plugin API $pluginApi
(https://github.com/Whocares66-Dev/DAOLib/releases). The game needs that release of
DAOLib or a later one of the same plugin API: its loader, and its address files. A loader
of another plugin API does not load this plugin, and the logs in
Documents\BioWare\Dragon Age\daolib (loader.log, turnbased.log) say so.
"@
Set-Content -Path (Join-Path $plugin 'VERSION.txt') -Value $versionText -Encoding utf8

# What is given out does not say who built it. The linker writes the folder of its
# temporary files into a PDB, which tools\build.ps1 keeps in the build tree for that
# reason (as DAOLib #55); a plugin linked any other way is caught here, and so is any
# other file of the zip that names this user's folder or this user.
$profileFolder = [Environment]::GetFolderPath('UserProfile')
$userName = Split-Path -Leaf $profileFolder
foreach ($file in Get-ChildItem $stage -Recurse -File) {
    $bytes = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($file.FullName))
    foreach ($word in $profileFolder, "Users\$userName", "Users/$userName") {
        foreach ($written in $word, ($word.ToCharArray() -join "`0")) {
            if ($bytes.IndexOf($written, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                Remove-Item -Recurse -Force $stage
                throw "$($file.Name) holds the path of this user's folder; build with .\tools\build.ps1 -Preset $Preset (and tools\movie.py) and nothing else"
            }
        }
    }
}

$zip = Join-Path $out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Remove-Item -Recurse -Force $stage
Show-Info "built on $builtOn, plugin API $pluginApi; $($named.Count) files in override"
Show-Ok "package -> $zip"
