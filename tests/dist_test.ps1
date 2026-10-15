# dist_test.ps1 - the bundle build.sh makes, run the way someone else would
# run it: nothing from MSYS2 on PATH, started from a folder that is not its
# own. It is started twice, as a double-click starts it and as an MSYS2
# shell does; a window opens for a few seconds each time.
#
#   powershell -File tests\dist_test.ps1 -Bundle <folder with clock.exe>
#
# Checks named "regression:" fail against the first commit's build.
param([Parameter(Mandatory)] [string] $Bundle)

$ErrorActionPreference = 'Stop'
$failures = 0
$checks = 0

function Check([bool] $ok, [string] $name, [string] $detail = '') {
    $script:checks++
    $line = if ($ok) { 'ok    ' } else { 'FAIL  ' }
    $line += $name
    if ($detail) { $line += "  -- $detail" }
    Write-Output $line
    if (-not $ok) { $script:failures++ }
}

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class DistWin {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr Find(uint pid, string title) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var s = new StringBuilder(256); GetWindowText(h, s, 256);
      if (p == pid && s.ToString() == title) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@

# Start clock.exe from the Windows folder with a bare PATH, and XDG_DATA_DIRS
# unset or set to $Xdg. Report its window, its icon and its stderr.
function Launch([string] $Xdg) {
    $stderr = Join-Path $env:TEMP "7seg-dist-stderr-$PID.txt"
    $saved = @{ PATH = $env:PATH; XDG_DATA_DIRS = $env:XDG_DATA_DIRS }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    if ($Xdg) { $env:XDG_DATA_DIRS = $Xdg } else { Remove-Item Env:XDG_DATA_DIRS -ErrorAction SilentlyContinue }
    try {
        $p = Start-Process -FilePath $exe -WorkingDirectory $env:SystemRoot -PassThru `
                           -RedirectStandardError $stderr
    } finally {
        $env:PATH = $saved.PATH
        if ($saved.XDG_DATA_DIRS) { $env:XDG_DATA_DIRS = $saved.XDG_DATA_DIRS }
        else { Remove-Item Env:XDG_DATA_DIRS -ErrorAction SilentlyContinue }
    }

    $r = @{ Window = [IntPtr]::Zero; Icon = [IntPtr]::Zero; Detail = ''; Stderr = '' }
    foreach ($i in 1..60) {
        Start-Sleep -Milliseconds 250
        if ($p.HasExited) { break }
        $r.Window = [DistWin]::Find([uint32]$p.Id, 'LED Clock')
        if ($r.Window -ne [IntPtr]::Zero) { break }
    }
    if ($p.HasExited) { $r.Detail = "exited with $($p.ExitCode)" }
    if ($r.Window -ne [IntPtr]::Zero) {
        foreach ($i in 1..20) {
            $r.Icon = [DistWin]::SendMessage($r.Window, 0x7F, [IntPtr]1, [IntPtr]::Zero)   # WM_GETICON, ICON_BIG
            if ($r.Icon -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 250
        }
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id; $p.WaitForExit() }
    if (Test-Path $stderr) {
        $r.Stderr = Get-Content $stderr -Raw
        Remove-Item $stderr -ErrorAction SilentlyContinue
    }
    return $r
}

function FirstLine([string] $s) { if ($s) { ($s.Trim() -split "`n")[0] } else { '' } }

$exe = Join-Path $Bundle 'clock.exe'
Check (Test-Path $exe) 'the bundle has clock.exe' $exe
if (-not (Test-Path $exe)) { exit 1 }

# PE subsystem: 2 is a Windows program, 3 a console one. A console program
# started from Explorer gets a console window beside the clock, and closing
# that window ends the clock.
$bytes = [System.IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$subsystem = [BitConverter]::ToUInt16($bytes, $pe + 24 + 68)
Check ($subsystem -eq 2) 'regression: clock.exe is a Windows program, with no console window' `
      "PE subsystem $subsystem"

# As a double-click starts it: nothing sets XDG_DATA_DIRS.
$r = Launch ''
Check ($r.Window -ne [IntPtr]::Zero) 'it opens its window with nothing from MSYS2 on PATH' $r.Detail
Check ($r.Icon -ne [IntPtr]::Zero) 'the window has the clock''s icon, started from another folder'
Check (-not $r.Stderr) 'it writes nothing to stderr' (FirstLine $r.Stderr)

# As an MSYS2 shell starts it: its profile sets XDG_DATA_DIRS to MSYS2's own
# share folders, which GLib then reads instead of the bundle's.
$msys = "$env:SystemDrive\msys64\mingw64\share;$env:SystemDrive\msys64\usr\share"
$r = Launch $msys
Check ($r.Window -ne [IntPtr]::Zero) 'it opens its window from an MSYS2 shell too' $r.Detail
Check ($r.Icon -ne [IntPtr]::Zero) 'regression: and has its icon there, with XDG_DATA_DIRS set'
Check (-not $r.Stderr) 'and writes nothing to stderr' (FirstLine $r.Stderr)

Write-Output ''
if ($failures) { Write-Output "FAILED: $failures of $checks checks failed"; exit 1 }
Write-Output "passed: 0 of $checks checks failed"
