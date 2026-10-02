<#
.SYNOPSIS
    Copy a build's turnbased.dll, and DAOLib's loader, into the game; or take the plugin out.

.DESCRIPTION
    After `.\tools\build.ps1 -Preset <preset>`, puts build\<preset>\daolib\plugins\turnbased.dll
    (and its .pdb, so a crash log names our frames) under bin_ship\daolib\plugins, and
    build\<preset>\version.dll -- DAOLib's loader, built from extern\DAOLib -- beside
    DAOrigins.exe, where the game loads it. The game must not be running: it holds
    those files open.

    Other plugins in bin_ship\daolib\plugins (DAOLib's probe, say) are left as they are.

    With build\ui\turnorder.swf built (python tools\movie.py), the turn bar's movie goes
    into Documents\BioWare\Dragon Age\packages\core\override\, the folder the game reads
    loose resources from, where its resource system finds a screen's movie as it finds
    its own (DAOLib's dev/GUI.md). The bar shows with `turn_bar = true` in turnbased.ini.

    -Remove deletes turnbased.dll, its .pdb and the movie; the loader stays for other
    plugins. DAOLib's own `deploy.ps1 -Remove` returns the game to stock. The settings
    and logs are in Documents\BioWare\Dragon Age\daolib\ and stay.

.EXAMPLE
    .\tools\deploy.ps1                 # debug build into the GOG install
    .\tools\deploy.ps1 -Preset release
    .\tools\deploy.ps1 -Remove
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $Preset = 'debug',
    [switch] $Remove,
    [string] $Game = 'C:\Program Files (x86)\GOG Galaxy\Games\Dragon Age Origins\bin_ship'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path (Join-Path $Game 'DAOrigins.exe'))) { throw "No DAOrigins.exe in $Game" }
if (Get-Process -Name DAOrigins -ErrorAction SilentlyContinue) { throw 'DAOrigins.exe is running; close it first' }

$plugins = Join-Path $Game 'daolib\plugins'
$plugin = Join-Path $plugins 'turnbased.dll'
$pluginPdb = Join-Path $plugins 'turnbased.pdb'
$override = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Dragon Age\packages\core\override'
$movie = Join-Path $override 'turnorder.swf'

if ($Remove) {
    foreach ($f in @($plugin, $pluginPdb, $movie)) {
        if (Test-Path $f) { Remove-Item -Force $f; Show-Ok "removed $f" }
    }
    Show-Info 'the loader (version.dll) stays; DAOLib''s deploy.ps1 -Remove takes it out'
    return
}

$build = Join-Path $repo "build\$Preset"
$builtPlugin = Join-Path $build 'daolib\plugins\turnbased.dll'
$builtLoader = Join-Path $build 'version.dll'
foreach ($f in @($builtPlugin, $builtLoader)) {
    if (-not (Test-Path $f)) { throw "No $f; run .\tools\build.ps1 -Preset $Preset first" }
}

New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item -Force $builtLoader (Join-Path $Game 'version.dll')
$loaderPdb = Join-Path $build 'version.pdb'
if (Test-Path $loaderPdb) { Copy-Item -Force $loaderPdb (Join-Path $Game 'version.pdb') }
Show-Ok "loader  -> $(Join-Path $Game 'version.dll')"
Copy-Item -Force $builtPlugin $plugin
$builtPdb = [IO.Path]::ChangeExtension($builtPlugin, '.pdb')
if (Test-Path $builtPdb) { Copy-Item -Force $builtPdb $pluginPdb }
Show-Ok "plugin  -> $plugin"
$builtMovie = Join-Path $repo 'build\ui\turnorder.swf'
if (Test-Path $builtMovie) {
    New-Item -ItemType Directory -Force $override | Out-Null
    Copy-Item -Force $builtMovie $movie
    Show-Ok "movie   -> $movie"
}
else {
    Show-Warn 'no build\ui\turnorder.swf (python tools\movie.py): the turn bar has no movie to show'
}
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Dragon Age\daolib'
Show-Info "settings: $(Join-Path $docs 'turnbased.ini') (written on first run)"
Show-Info "log:      $(Join-Path $docs 'turnbased.log')"
