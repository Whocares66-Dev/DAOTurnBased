<#
.SYNOPSIS
    Copy a build's plugin, a folder of its own, and DAOLib's loader into the game; or take the plugin out.

.DESCRIPTION
    After `.\tools\build.ps1 -Preset <preset>`, puts the plugin's folder,
    build\<preset>\daolib\plugins\turnbased\ (turnbased.dll, its .pdb, so a crash log names
    our frames, and the mod's two movies), under bin_ship\daolib\plugins, and
    build\<preset>\version.dll -- DAOLib's loader, built from extern\DAOLib -- beside
    DAOrigins.exe, where the game loads it, with the address library's files under
    bin_ship\daolib\addresses. The game must not be running from that folder: it holds
    those files open.

    A plugin is a folder since DAOLib's issue 26 (its d892f71; issue 47 here, 2026-10-05):
    the loader loads plugins\turnbased\turnbased.dll, and a DLL loose in plugins\ is not
    loaded. Other plugins in bin_ship\daolib\plugins (Eluvian, DAOLib's probe) are left as
    they are.

    The turn bar's movie, build\ui\turnorder.swf (python tools\movie.py), goes beside the
    dll: DAOLib serves a movie the game has none of by that name from the plugin's own
    folder (its dao/Gui.h), so nothing of the mod's goes into the game's override folder.
    The build copies the movie beside the built dll, on every build (CMakeLists.txt,
    FILES), so the build's folder is the whole of the mod and the deploy copies it as it
    is. The bar shows with `turn_bar = true` in turnbased.ini. The plugin and the movie
    are a pair, so the deploy stops at a movie older than its source (ui\turnorder,
    tools\movie.py), and at one built since the last build, which is not beside the dll
    yet; -StaleMovie deploys the folder's all the same.

    The tactics page's box's movie, build\ui\turnbased_tactics.swf (ui\tactics; issue 46),
    goes the same way and is held the same way: the plugin loads it from its own folder
    into the game's tactics screen as that opens, and without it the page has no box.

    The movement preview's red ring and red marker, build\art (python tools\reticles.py: the
    game's own art made again in red; issue 72), go with the folder too, in its override\
    subfolder, which the game reads as it reads its own override folder (DAOLib's issue 43,
    7bedb3b). Without them the deploy says so and goes on: the preview then shows a path as
    the movie's line.

    What deploys before that left is taken out, by a deploy and by -Remove: turnbased.dll
    and its .pdb loose in bin_ship\daolib\plugins, and in
    Documents\BioWare\Dragon Age\packages\core\override\ the movie turnorder.swf, which
    the game's resource system would find before the plugin's own, and the two material
    objects a deploy of 2026-10-01 put there, where they are the mod's own. And by a
    deploy, any texture, material object, model or table loose beside the dll, where the
    first build of issue 72 had the red ring's files.

    -Remove deletes the plugin's folder and those; the loader and the address files stay
    for other plugins. DAOLib's own `deploy.ps1 -Remove` returns the game to stock. The
    settings and logs are in Documents\BioWare\Dragon Age\daolib\ and stay. A folder that
    is a link, or has one anywhere in it (a junction, a symbolic link), is not deleted: a
    recursive delete goes through one and empties what it points at.

    Neither is done while another agent's claim on the game stands (game.claim.json
    beside the logs, Eluvian's dev/DESIGN.md, "Outside the game"); -Owner is who deploys.

    -Game is another bin_ship folder, and -Documents the game's folder in Documents that
    goes with it (its override folder, the settings, the logs and the claim): another
    install's, or a test's throwaway pair (tools\test_deploy.py).

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
    [string] $Game = 'C:\Program Files (x86)\GOG Galaxy\Games\Dragon Age Origins\bin_ship',
    [string] $Documents = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Dragon Age'),
    [string] $Owner = 'turn-based-agent',
    [switch] $StaleMovie
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path (Join-Path $Game 'DAOrigins.exe'))) { throw "No DAOrigins.exe in $Game" }
# The game holds its DLLs open, and only one running from this folder holds this folder's.
# Another has the same process name (EA's install beside GOG's; the player's game while a
# test deploys into a folder of its own), so the running one is asked where it runs from,
# as DAOLib's own deploy.ps1 asks. One whose path cannot be read is taken for this folder's.
$exe = (Resolve-Path (Join-Path $Game 'DAOrigins.exe')).Path
foreach ($running in @(Get-Process -Name DAOrigins -ErrorAction SilentlyContinue)) {
    $from = $running.Path
    if (-not $from -or [string]::Equals($from, $exe, [StringComparison]::OrdinalIgnoreCase)) {
        $where = if ($from) { $from } else { 'a path that cannot be read' }
        throw "DAOrigins.exe is running from $where (process $($running.Id)); close it first"
    }
    Show-Info "a game is running from $from (process $($running.Id)); it does not hold this folder's files"
}

# Several agents share the game and its folder, and a test may leave a loader and plugins of
# its own in place between two runs. game.claim.json says who holds them, the one claim every
# agent's scripts honour (the user's decision, 2026-10-02; Eluvian's dev/DESIGN.md, "Outside the game"):
# while one stands that is not this caller's, nothing is copied or taken out. -Owner says who
# is deploying. The old name too, which a bridge started before the shared name still writes.
$claimFolder = Join-Path $Documents 'daolib'
foreach ($claimFile in @(Join-Path $claimFolder 'game.claim.json')) {
    if (Test-Path $claimFile) {
        try {
            $claim = Get-Content $claimFile -Raw | ConvertFrom-Json
            $left = [double]$claim.until - [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
            if ($left -gt 0 -and $claim.owner -ne $Owner) {
                throw "the game and its folder are claimed by $($claim.owner) for another $([math]::Round($left / 60, 1)) minutes ($($claim.note)); nothing deployed"
            }
        }
        catch [System.ArgumentException] { Show-Info "$claimFile is not readable; taken as no claim" }
    }
}

$plugins = Join-Path $Game 'daolib\plugins'
$pluginFolder = Join-Path $plugins 'turnbased'
# What a deploy before 2026-10-05 left: the dll and its pdb loose among the plugins, which
# the loader no longer loads (its log says where one belongs), and the movie in the override
# folder, which the game's resource system finds before DAOLib looks in the plugin's folder:
# left there, an old movie would be the one shown with every plugin after it. The script
# never wrote a turnorder.gfx.
$override = Join-Path $Documents 'packages\core\override'
$leftovers = @(
    (Join-Path $plugins 'turnbased.dll'),
    (Join-Path $plugins 'turnbased.pdb'),
    (Join-Path $override 'turnorder.swf')
)
# And two material objects that a deploy of 2026-10-01 put in the override folder (dda42c9:
# the game's move_indicator.mao and move_icon.mao given the semantic that takes a colour, for
# a movement marker in red). They changed nothing seen and were taken back the same evening
# (18f3961), which stopped deploying them and left the deployed ones where they were. The
# names are the game's, so another mod may have its own there: one is taken out only where
# it says it is this mod's, as both did in a comment at their head.
$oldMaterials = @(
    (Join-Path $override 'move_indicator.mao'),
    (Join-Path $override 'move_icon.mao')
)
function Clear-Leftover {
    foreach ($f in $leftovers) {
        if (Test-Path -LiteralPath $f) {
            Remove-Item -LiteralPath $f -Force
            Show-Ok "removed $f (the layout before a plugin was a folder)"
        }
    }
    foreach ($f in $oldMaterials) {
        if (-not (Test-Path -LiteralPath $f)) { continue }
        if (Select-String -LiteralPath $f -SimpleMatch -Quiet -Pattern '<!-- DAOTurnBased:') {
            Remove-Item -LiteralPath $f -Force
            Show-Ok "removed $f (the mod's own, of 2026-10-01)"
        }
        else { Show-Info "left $f, which does not say it is this mod's" }
    }
}

# The first link at a path or anywhere under it: a junction, a symbolic link, anything with
# a reparse point. Remove-Item -Recurse goes through one and deletes what it points at: the
# game's install was emptied that way (2026-10-02), and the user's rule since is to look
# before any recursive delete. The walk is by hand, a folder at a time, and stops at the
# first, so it goes through none itself (Get-ChildItem -Recurse does, in Windows PowerShell 5).
function Find-ReparsePoint([string] $Path) {
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($Path)
    while ($pending.Count -gt 0) {
        $item = Get-Item -LiteralPath $pending.Pop() -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return $item.FullName }
        if ($item.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) { $pending.Push($child.FullName) }
        }
    }
    return $null
}

if ($Remove) {
    if (Test-Path -LiteralPath $pluginFolder) {
        $link = Find-ReparsePoint $pluginFolder
        if ($link) {
            throw "$link is a link (a junction or the like), and a recursive delete would go through it: nothing removed; take the link out by hand first"
        }
        Remove-Item -LiteralPath $pluginFolder -Recurse -Force
        Show-Ok "removed $pluginFolder"
    }
    Clear-Leftover
    Show-Info 'the loader (version.dll) and the address files stay; DAOLib''s deploy.ps1 -Remove takes them out'
    return
}

$build = Join-Path $repo "build\$Preset"
$builtFolder = Join-Path $build 'daolib\plugins\turnbased'
$builtPlugin = Join-Path $builtFolder 'turnbased.dll'
$builtLoader = Join-Path $build 'version.dll'
foreach ($f in @($builtPlugin, $builtLoader)) {
    if (-not (Test-Path $f)) { throw "No $f; run .\tools\build.ps1 -Preset $Preset first" }
}
# DAOLib's address library, one file per build of the game (DAOLib 8b027c6 on): the loader
# and the plugin read the one for the game they are in, and the loader loads nothing
# without it. Looked for before anything is copied, with the rest of what a build makes.
$builtAddresses = Join-Path $build 'daolib\addresses'
if (-not (Test-Path (Join-Path $builtAddresses '*.bin'))) { throw "No address files in $builtAddresses; the build makes them" }

# The movie is built apart from the dll (tools\movie.py: Java and FFDec), so a tree with a
# plugin just built can hold a movie of days before; deployed, it went over the newer one in
# the game with a plain OK, the bar a day behind the plugin that feeds it (issue 5, the
# playthrough, 2026-10-02). They are a pair (TB_GetView's fields, dev/UI.md): a movie older
# than its own source stops the deploy before anything is copied.
#
# The movie that goes is the one beside the built dll: the build puts build\ui's there, on
# every build whether the dll links or not, keeping its time (add_daolib_plugin's FILES,
# DAOLib d892f71; seen 2026-10-05). So the folder is copied as it is, and what is left to
# catch is a movie built with no build after it, python tools\movie.py and then this: the
# folder's is the one before it, or none. That stops the deploy too, and a build mends it.
# With no movie in build\ui the build warns and leaves the folder's as it was: that one, an
# earlier build's, is held to the sources by its own time.
#
# Two movies since issue 46 (2026-10-05), each held the same way to its own sources: the
# turn bar's, and the tactics page's box's, which the plugin loads into the game's tactics
# screen and whose calls it answers (TurnBased_TacticsBox and the rest, dev/UI.md).
$movies = @(
    @{ Name = 'turnorder.swf'; Source = 'ui\turnorder'; Without = 'the turn bar has no movie to show' },
    @{ Name = 'turnbased_tactics.swf'; Source = 'ui\tactics'; Without = 'the tactics page has no box' }
)
foreach ($m in $movies) {
    $builtMovie = Join-Path $repo "build\ui\$($m.Name)"
    $folderMovie = Join-Path $builtFolder $m.Name
    $movie = @($builtMovie, $folderMovie) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $movie -or $StaleMovie) { continue }
    $movieSources = @(Get-ChildItem -Path (Join-Path $repo $m.Source) -Recurse -File) +
    @(Get-Item (Join-Path $PSScriptRoot 'movie.py'))
    $newest = $movieSources | Sort-Object LastWriteTimeUtc | Select-Object -Last 1
    if ($newest.LastWriteTimeUtc -gt (Get-Item $movie).LastWriteTimeUtc) {
        throw "$($movie.Substring($repo.Length + 1)) is older than $($newest.FullName.Substring($repo.Length + 1)); run python tools\movie.py and then .\tools\build.ps1 -Preset $Preset first (or -StaleMovie to deploy the folder's as it is); nothing deployed"
    }
    # By their bytes, read through .NET. Get-FileHash is not found by a Windows PowerShell that
    # another program starts with PowerShell 7's module path in its environment, as ctest's
    # python does under build.ps1 (tools\test_deploy.py met it, 2026-10-05; with PSModulePath
    # taken out of that environment it is found).
    $same = $false
    if (Test-Path $folderMovie) {
        $same = [Convert]::ToBase64String([IO.File]::ReadAllBytes($folderMovie)) -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($movie))
    }
    if (-not $same) {
        throw "$($folderMovie.Substring($repo.Length + 1)) is not build\ui\$($m.Name), which was built after the last build; run .\tools\build.ps1 -Preset $Preset first, which copies it there (or -StaleMovie to deploy the folder's as it is); nothing deployed"
    }
}

New-Item -ItemType Directory -Force $pluginFolder | Out-Null
Copy-Item -Force $builtLoader (Join-Path $Game 'version.dll')
$loaderPdb = Join-Path $build 'version.pdb'
if (Test-Path $loaderPdb) { Copy-Item -Force $loaderPdb (Join-Path $Game 'version.pdb') }
Show-Ok "loader  -> $(Join-Path $Game 'version.dll')"
# The plugin and its files, not the linker's leftovers (the incremental link's .ilk beside
# a debug dll), as DAOLib's own deploy.ps1 copies a plugin's folder.
Copy-Item -Force -Recurse (Join-Path $builtFolder '*') $pluginFolder -Exclude '*.ilk', '*.exp', '*.lib'
Show-Ok "plugin  -> $pluginFolder"
foreach ($m in $movies) {
    if (Test-Path (Join-Path $builtFolder $m.Name)) {
        Show-Ok "movie   -> $(Join-Path $pluginFolder $m.Name)"
    }
    elseif (Test-Path (Join-Path $pluginFolder $m.Name)) {
        Show-Warn "no $($m.Name) beside the built dll (python tools\movie.py, then a build): the movie an earlier deploy left in $pluginFolder stays, and may be older than the plugin"
    }
    else {
        Show-Warn "no $($m.Name) beside the built dll (python tools\movie.py, then a build): $($m.Without)"
    }
}
# The movement preview's rings and red marker (issue 72): the game's own art made again by
# tools\reticles.py, which the build copies into the folder's override\ subfolder
# (CMakeLists.txt, OVERRIDE) and which went with the folder just now. The game reads that
# subfolder as it reads its own override folder (DAOLib's issue 43, 7bedb3b), and the plugin
# says at start whether it finds them. The subfolder is the build's and nothing else: a file
# an earlier deploy left under a name the tool has given up would be read by the game all the
# same. Their table is the last file the tool writes, so it stands for all of them.
$art = Join-Path $pluginFolder 'override'
if (Test-Path -LiteralPath $art) {
    foreach ($f in @(Get-ChildItem -LiteralPath $art -File)) {
        if (-not (Test-Path -LiteralPath (Join-Path $builtFolder "override\$($f.Name)"))) {
            Remove-Item -LiteralPath $f.FullName -Force
            Show-Ok "removed $($f.FullName) (the build has no such file)"
        }
    }
}
if (-not (Test-Path (Join-Path $art 'vfx_base_turnbased.gda'))) {
    Show-Warn "no vfx_base_turnbased.gda in $art (python tools\reticles.py, then a build and this again): the preview has no rings or red marker of its own, and shows a path as the movie's line"
}
# And nowhere but there. The first build of issue 72 (on DAOLib's first form of its issue 43,
# which made the plugin's whole folder a source) had the thirteen files loose beside the dll:
# a deploy of it left them there, and a build tree of that day still has them for the copy
# above to bring. Nothing reads them, so a file of the game's kinds loose in the plugin's own
# folder is taken out.
$loose = Get-ChildItem -LiteralPath $pluginFolder -File |
    Where-Object { $_.Extension -in '.dds', '.mao', '.mmh', '.gda' }
foreach ($f in @($loose)) {
    Remove-Item -LiteralPath $f.FullName -Force
    Show-Ok "removed $($f.FullName) (loose beside the dll: the layout before override\)"
}
Clear-Leftover
# The address files beside the loader, as DAOLib's own deploy.ps1 puts them.
$addresses = Join-Path $Game 'daolib\addresses'
New-Item -ItemType Directory -Force $addresses | Out-Null
$built = @(Get-ChildItem -Path $builtAddresses -Filter '*.bin' | ForEach-Object { $_.Name })
foreach ($name in $built) {
    Copy-Item -Force (Join-Path $builtAddresses $name) (Join-Path $addresses $name)
}
# The folder holds what this build has and nothing else, as DAOLib's own deploy leaves it: a
# file the address library no longer writes (the names by link stamp alone, before its
# ae992b3) would be stale here. This deploy puts the loader in, so the files are its to keep
# current; Eluvian's reads the same four-field names and adds only what is missing
# (eluvian-agent, 2026-10-02).
foreach ($file in Get-ChildItem -Path $addresses -Filter 'daoaddresses-*.bin') {
    if ($built -notcontains $file.Name) {
        Remove-Item -Force $file.FullName
        Show-Info "removed $($file.Name): the build has no such file"
    }
}
Show-Ok "addresses -> $addresses"
$docs = Join-Path $Documents 'daolib'
Show-Info "settings: $(Join-Path $docs 'turnbased.ini') (written on first run)"
Show-Info "log:      $(Join-Path $docs 'turnbased.log')"
