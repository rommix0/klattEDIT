param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)
$Out = "$Repo\build\ui"; New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
"@
[U]::SetProcessDPIAware() | Out-Null
function Shot($h, $name) {
  $r = New-Object U+RECT; [U]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [U]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $bmp.Save("$Out\$name.png"); "saved $name"
}
function MouseAt($h, $msg, $x, $y, $keys) {
  $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [U]::SendMessageW($h, $msg, [IntPtr]$keys, $lp) | Out-Null
}
$WM_COMMAND = 0x111; $WM_LBUTTONDOWN = 0x201; $WM_LBUTTONUP = 0x202; $WM_MOUSEMOVE = 0x200; $MK_LBUTTON = 1

$p = Start-Process -FilePath "$Repo\build\32\kpe.exe" -ArgumentList "`"$Repo\samples\ba.csv`"" -PassThru
Start-Sleep -Milliseconds 1500
$p.Refresh(); $main = $p.MainWindowHandle
$plot = [U]::FindWindowExW($main, [IntPtr]::Zero, "KpePlot", $null)
$c = New-Object U+RECT; [U]::GetClientRect($plot, [ref]$c) | Out-Null
"plot client $($c.R)x$($c.B)"

# Geometry, mirroring strips.c at this DPI: margins in 96-DPI pixels scaled by dpi/96.
$dpi = [int][System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero).DpiY
function Scale($v) { [int]($v * $dpi / 96) }
$ML = Scale 70; $MR = Scale 40; $MT = Scale 6; $MB = Scale 44
$pw = $c.R - $ML - $MR
function X($t) { [int]($ML + $t * $pw / 800.0) }
# Strips: F0, AV, AF, AH (64 high + 8 gap each), then the formant strip to the bottom.
$ftop = $MT + 4 * ((Scale 64) + (Scale 8)); $fh = ($c.B - $MT - $MB) - 4 * ((Scale 64) + (Scale 8))
function Y($hz) { [int]($ftop + $fh - 1 - $hz / 5000.0 * ($fh - 1)) }

# Paint F2 from (250 ms, 1250 Hz) up to (400 ms, 1700 Hz), then back down to 1250 Hz at 550 ms.
MouseAt $plot $WM_LBUTTONDOWN (X 250) (Y 1250) $MK_LBUTTON
for ($i = 1; $i -le 10; $i++) { MouseAt $plot $WM_MOUSEMOVE (X (250 + 15 * $i)) (Y (1250 + 45 * $i)) $MK_LBUTTON }
for ($i = 1; $i -le 10; $i++) { MouseAt $plot $WM_MOUSEMOVE (X (400 + 15 * $i)) (Y (1700 - 45 * $i)) $MK_LBUTTON }
MouseAt $plot $WM_LBUTTONUP (X 550) (Y 1250) 0
# Shift+drag a line on AV (strip 2): from 150 ms at 40 dB to 500 ms at 70 dB.
$avTop = $MT + (Scale 64) + (Scale 8); $avH = Scale 64
function YAV($db) { [int]($avTop + $avH - 1 - $db / 80.0 * ($avH - 1)) }
MouseAt $plot $WM_LBUTTONDOWN (X 150) (YAV 40) 5
MouseAt $plot $WM_MOUSEMOVE (X 500) (YAV 70) 5
MouseAt $plot $WM_LBUTTONUP (X 500) (YAV 70) 4
# Select 300-450 ms in the ruler.
$ry = $c.B - $MB + (Scale 10)
MouseAt $plot $WM_LBUTTONDOWN (X 300) $ry $MK_LBUTTON
MouseAt $plot $WM_MOUSEMOVE (X 450) $ry $MK_LBUTTON
MouseAt $plot $WM_LBUTTONUP (X 450) $ry 0
MouseAt $plot $WM_MOUSEMOVE (X 420) (Y 2600) 0
Start-Sleep -Milliseconds 300
Shot $main "ui_painted"

[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]1501, [IntPtr]::Zero) | Out-Null   # View > Frame table
Start-Sleep -Milliseconds 500
$table = [U]::FindWindowW("KpeTable", "Frame table"); Shot $table "ui_table"

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]1401, [IntPtr]::Zero) | Out-Null   # Parameters > Globals
Start-Sleep -Milliseconds 700
$dlg = [U]::FindWindowW("#32770", "Globals"); Shot $dlg "ui_globals"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]2, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]1402, [IntPtr]::Zero) | Out-Null   # Parameters > Optimize
Start-Sleep -Milliseconds 700
$dlg = [U]::FindWindowW("#32770", "Optimize")
[U]::SendMessageW($dlg, $WM_COMMAND, [IntPtr]2103, [IntPtr]::Zero) | Out-Null    # whole file
[U]::SendMessageW([U]::FindWindowExW($dlg, [IntPtr]::Zero, "Button", "Whole file"), 0xF1, [IntPtr]1, [IntPtr]::Zero) | Out-Null
[U]::SendMessageW([U]::FindWindowExW($dlg, [IntPtr]::Zero, "Button", "Selection"), 0xF1, [IntPtr]0, [IntPtr]::Zero) | Out-Null
[U]::SendMessageW($dlg, $WM_COMMAND, [IntPtr]2108, [IntPtr]::Zero) | Out-Null    # Preview
Start-Sleep -Milliseconds 500
Shot $dlg "ui_optimize"
Shot $main "ui_optimize_preview"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]2, [IntPtr]::Zero) | Out-Null       # Cancel
Start-Sleep -Milliseconds 400

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]1601, [IntPtr]::Zero) | Out-Null   # Help > Parameter reference
Start-Sleep -Milliseconds 700
$dlg = [U]::FindWindowW("#32770", "Parameter reference"); Shot $dlg "ui_paramref"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400
Shot $main "ui_after_cancel"
Stop-Process -Id $p.Id -Force
