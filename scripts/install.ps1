# Downloads the latest release and installs the VST3 plug-ins (Simplr, Multidyn, Lowfocus) on Windows.
#
#   irm https://raw.githubusercontent.com/bfielstr/audio-plugins/main/scripts/install.ps1 | iex
#
# Run from an elevated (Administrator) PowerShell to install into the system VST3 folder
# (C:\Program Files\Common Files\VST3), which every host scans. Otherwise it installs for the
# current user into %LOCALAPPDATA%\Programs\Common\VST3.
#
# Environment overrides: $env:SIMPLR_VERSION = 'v0.2.0'; $env:SIMPLR_DEST = 'D:\VST3';
#   $env:SIMPLR_PLUGINS = 'Multidyn Lowfocus' to install only some of them

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is very slow with the progress bar

$repo = if ($env:SIMPLR_REPO) { $env:SIMPLR_REPO } else { 'bfielstr/audio-plugins' }
$version = if ($env:SIMPLR_VERSION) { $env:SIMPLR_VERSION } else { 'latest' }
$asset = 'Plugins-Windows-x64.zip'
$base = if ($version -eq 'latest') { "https://github.com/$repo/releases/latest/download" }
        else { "https://github.com/$repo/releases/download/$version" }

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($env:SIMPLR_DEST) {
    $dest = $env:SIMPLR_DEST
} elseif ($isAdmin) {
    $common = if (${env:CommonProgramW6432}) { ${env:CommonProgramW6432} } else { ${env:CommonProgramFiles} }
    $dest = Join-Path $common 'VST3'
} else {
    $dest = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("simplr-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Write-Host "Downloading $asset ($version) from github.com/$repo"
    $zip = Join-Path $tmp $asset
    Invoke-WebRequest -Uri "$base/$asset" -OutFile $zip -UseBasicParsing

    # Verify the checksum published with the release.
    $sums = Join-Path $tmp 'SHA256SUMS.txt'
    $haveSums = $true
    try { Invoke-WebRequest -Uri "$base/SHA256SUMS.txt" -OutFile $sums -UseBasicParsing }
    catch { $haveSums = $false }
    if ($haveSums) {
        $line = Get-Content $sums | Where-Object { $_ -match "\s$([regex]::Escape($asset))$" } | Select-Object -First 1
        $expected = if ($line) { ($line -split '\s+')[0].ToLower() } else { '' }
        $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
        if (-not $expected -or $expected -ne $actual) { throw "Checksum mismatch for $asset - aborting." }
        Write-Host 'Checksum OK'
    } else {
        Write-Warning 'No SHA256SUMS.txt in this release; skipping checksum verification.'
    }

    $x = Join-Path $tmp 'x'
    Expand-Archive -Path $zip -DestinationPath $x -Force
    $plugins = if ($env:SIMPLR_PLUGINS) { $env:SIMPLR_PLUGINS -split '\s+' | Where-Object { $_ } }
               else { Get-ChildItem -Path $x -Directory -Filter '*.vst3' | ForEach-Object { $_.Name -replace '\.vst3$', '' } }
    if (-not $plugins) { throw 'Unexpected archive layout (no .vst3 inside).' }
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    foreach ($p in $plugins) {
        $bundle = Join-Path $x "$p.vst3"
        if (-not (Test-Path $bundle)) { Write-Warning "No $p.vst3 in this release - skipping."; continue }
        $target = Join-Path $dest "$p.vst3"
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Move-Item -Path $bundle -Destination $target
        Get-ChildItem -Recurse $target | Unblock-File
        Write-Host "Installed: $target"
    }
    if (-not $isAdmin -and -not $env:SIMPLR_DEST) {
        Write-Host "Installed for the current user. If REAPER doesn't find it, add this folder under"
        Write-Host "Options > Preferences > Plug-ins > VST > VST plug-in paths:  $dest"
        Write-Host "(or re-run this command from an Administrator PowerShell to install system-wide)."
    }
    Write-Host "In REAPER: Options > Preferences > Plug-ins > VST > Re-scan."
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
