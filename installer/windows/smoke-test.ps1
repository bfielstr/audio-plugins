# Runs the Windows installer silently on a CI runner (as Administrator) and checks the result: a full
# install over planted leftovers (which must be cleared away, except another vendor's bundle), the
# uninstaller registered in Settings > Apps, a silent uninstall, then an install of one plug-in only.
#
#   pwsh installer/windows/smoke-test.ps1 dist/Plugins-Windows-x64-Setup.exe
param([Parameter(Mandatory = $true)][string]$Setup)
$ErrorActionPreference = 'Stop'

$Setup = (Resolve-Path $Setup).Path
$common = if (${env:CommonProgramW6432}) { ${env:CommonProgramW6432} } else { ${env:CommonProgramFiles} }
$sys = Join-Path $common 'VST3'
$dest = Join-Path $sys 'bfielstr'
$userRoot = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{8804C6F2-6057-4889-BBE2-D199E43D020B}_is1'
$log = Join-Path $env:RUNNER_TEMP 'setup.log'

function New-FakeBundle ($path, $vendor) {
    $res = Join-Path $path 'Contents\Resources'
    New-Item -ItemType Directory -Force -Path $res | Out-Null
    "{ `"Vendor`": `"$vendor`", `"Version`": `"0.1.0.0`" }" | Set-Content (Join-Path $res 'moduleinfo.json')
}
function Invoke-Setup ([string[]]$extra) {
    $argList = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"") + $extra
    $p = Start-Process -FilePath $Setup -ArgumentList $argList -Wait -PassThru
    if ($p.ExitCode -ne 0) { Get-Content $log | Write-Host; throw "setup exited with $($p.ExitCode)" }
}
function Invoke-Uninstall {
    $cmd = (Get-ItemProperty $uninstallKey).UninstallString.Trim('"')
    Start-Process -FilePath $cmd -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
    # the uninstaller hands over to a copy of itself in TEMP and returns at once: wait for it
    for ($i = 0; $i -lt 120 -and (Test-Path $uninstallKey); $i++) { Start-Sleep -Seconds 1 }
    if (Test-Path $uninstallKey) { throw 'uninstall did not finish' }
}
function Assert ($cond, $message) { if (-not $cond) { throw $message } }

# Leftovers: a renamed bundle, an old top-level copy of ours, another vendor's plug-in with one of our
# names, a stale file inside an older install, and a copy install.ps1 put in the user's own folder.
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
New-FakeBundle (Join-Path $dest 'Gently.vst3') 'bfielstr'
New-FakeBundle (Join-Path $sys 'Multidyn.vst3') 'Simplr'
New-FakeBundle (Join-Path $sys 'Locus.vst3') 'Someone Else'
New-FakeBundle (Join-Path $dest 'Multidyn.vst3') 'bfielstr'
'old' | Set-Content (Join-Path $dest 'Multidyn.vst3\Contents\stale-file')
New-FakeBundle (Join-Path $userRoot 'bfielstr\Multidyn.vst3') 'bfielstr'

# 1. Everything (the default).
Invoke-Setup @()
Assert (-not (Test-Path (Join-Path $dest 'Multidyn.vst3\Contents\stale-file'))) 'the older Multidyn was not replaced'
Assert (-not (Test-Path (Join-Path $dest 'Gently.vst3'))) 'renamed Gently.vst3 not removed'
Assert (-not (Test-Path (Join-Path $sys 'Multidyn.vst3'))) 'old top-level Multidyn.vst3 not removed'
Assert (Test-Path (Join-Path $sys 'Locus.vst3')) "another vendor's Locus.vst3 was removed"
Assert (-not (Test-Path (Join-Path $userRoot 'bfielstr\Multidyn.vst3'))) "the user's copy of Multidyn was not removed"
$installed = @(Get-ChildItem $dest -Directory -Filter '*.vst3')
Assert ($installed.Count -gt 1) 'expected several plug-ins'
foreach ($b in $installed) {
    $p = $b.Name -replace '\.vst3$', ''
    Assert (Test-Path "$($b.FullName)\Contents\x86_64-win\$p.vst3") "$p binary missing"
    Assert ((Get-Content -Raw "$($b.FullName)\Contents\Resources\moduleinfo.json") -match '"Vendor":\s*"bfielstr"') "$p vendor"
    Write-Host "$p installed"
}
Assert (-not (Test-Path (Join-Path $dest 'unins000.exe'))) 'the uninstaller must not be in the VST3 folder'
$entry = Get-ItemProperty $uninstallKey
Write-Host "Settings > Apps: $($entry.DisplayName)"

# 2. Uninstall: every bundle goes, other vendors' stay.
Invoke-Uninstall
Assert ((-not (Test-Path $dest)) -or (@(Get-ChildItem $dest).Count -eq 0)) 'plug-ins left after uninstalling'
Assert (Test-Path (Join-Path $sys 'Locus.vst3')) "uninstalling removed another vendor's Locus.vst3"

# 3. One plug-in only.
Invoke-Setup @('/COMPONENTS="multidyn"')
$installed = @(Get-ChildItem $dest -Directory -Filter '*.vst3' | ForEach-Object Name)
Assert ($installed.Count -eq 1 -and $installed[0] -eq 'Multidyn.vst3') "expected only Multidyn, got $installed"
Invoke-Uninstall

Remove-Item -Recurse -Force (Join-Path $sys 'Locus.vst3')
Write-Host 'Windows installer OK'
