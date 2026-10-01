param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)
$Out = "$Repo\build\ui"; New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr d, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr m, uint id, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public static IntPtr ChildOfClass(IntPtr parent, string cls) {
    for (IntPtr c = GetWindow(parent, 5); c != IntPtr.Zero; c = GetWindow(c, 2)) {
      var sb = new StringBuilder(64); GetClassNameW(c, sb, 64);
      if (string.Equals(sb.ToString(), cls, StringComparison.OrdinalIgnoreCase)) return c;
    }
    return IntPtr.Zero;
  }
  public struct RECT { public int L, T, R, B; }
}
"@
[U]::SetProcessDPIAware() | Out-Null
function Shot($h, $name) {
  $r = New-Object U+RECT; [U]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [U]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $bmp.Save("$Out\$name.png"); "saved $name"
}
function Wait-Window($cls, $title) {
  for ($i = 0; $i -lt 40; $i++) { $h = [U]::FindWindowW($cls, $title); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 100 }
  throw "window '$title' not found"
}
$WM_COMMAND = 0x111; $WM_SETTEXT = 0x0C; $WM_KEYDOWN = 0x100

$p = Start-Process -FilePath "$Repo\build\32\kpe.exe" -PassThru
Start-Sleep -Milliseconds 1500
$p.Refresh(); $main = $p.MainWindowHandle
try {

# File > New with 500 ms
[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]1001, [IntPtr]::Zero) | Out-Null
$dlg = Wait-Window "#32770" "New document"
Start-Sleep -Milliseconds 300
Shot $dlg "ui2_new"
[U]::SendMessageW([U]::GetDlgItem($dlg, 2002), $WM_SETTEXT, [IntPtr]::Zero, "500") | Out-Null
[U]::SendMessageW($dlg, $WM_COMMAND, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500

# Frame table: put the cursor on frame 3 with a ruler click, then F2, type 150, Enter.
$plot = [U]::FindWindowExW($main, [IntPtr]::Zero, "KpePlot", $null)
$c = New-Object U+RECT; [U]::GetClientRect($plot, [ref]$c) | Out-Null
$dpi = [int][System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero).DpiY; $ML = [int](70 * $dpi / 96); $MR = [int](40 * $dpi / 96); $MB = [int](44 * $dpi / 96)
$pw = $c.R - $ML - $MR
$x = [int]($ML + 15 * $pw / 500.0); $y = $c.B - $MB + [int](10 * $dpi / 96)
[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]1501, [IntPtr]::Zero) | Out-Null
$lp = [IntPtr](($y -shl 16) -bor $x)
[U]::SendMessageW($plot, 0x201, [IntPtr]1, $lp) | Out-Null
[U]::SendMessageW($plot, 0x202, [IntPtr]0, $lp) | Out-Null
$table = Wait-Window "KpeTable" "Frame table"
$list = [U]::FindWindowExW($table, [IntPtr]::Zero, "SysListView32", $null)
[U]::SendMessageW($list, $WM_KEYDOWN, [IntPtr]0x71, [IntPtr]::Zero) | Out-Null   # F2
$edit = [U]::ChildOfClass($list, "Edit")
"editor: $edit"
[U]::SendMessageW($edit, $WM_SETTEXT, [IntPtr]::Zero, "150") | Out-Null
[U]::SendMessageW($edit, $WM_KEYDOWN, [IntPtr]0x0D, [IntPtr]::Zero) | Out-Null   # Enter
Start-Sleep -Milliseconds 400
Shot $table "ui2_table_edit"
Shot $main "ui2_after_edit"

# Play: the playhead should appear
[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]1301, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 250
Shot $main "ui2_playing"
$box = [U]::FindWindowW("#32770", "Klatt Parameter Editor")
"message box after play: $box"
Start-Sleep -Milliseconds 600

# Edit > Crop to selection: select 100-300 ms in the ruler, crop (40 frames remain), then undo (100 again).
# The frame table has a row per frame; LVM_GETITEMCOUNT passes no pointer, so it works across processes.
function Frames() { [int][U]::SendMessageW($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero) }
function CropEnabled() {
  [U]::SendMessageW($main, 0x117, [U]::GetMenu($main), [IntPtr]::Zero) | Out-Null   # WM_INITMENUPOPUP: update the menu
  ([U]::GetMenuState([U]::GetMenu($main), 1109, 0) -band 3) -eq 0                    # not MF_GRAYED/MF_DISABLED
}
function RulerX($t) { [int]($ML + $t * $pw / 500.0) }
$lp = [IntPtr](($y -shl 16) -bor (RulerX 100)); [U]::SendMessageW($plot, 0x201, [IntPtr]1, $lp) | Out-Null
$lp = [IntPtr](($y -shl 16) -bor (RulerX 297)); [U]::SendMessageW($plot, 0x200, [IntPtr]1, $lp) | Out-Null
[U]::SendMessageW($plot, 0x202, [IntPtr]0, $lp) | Out-Null
"frames before crop: $(Frames); crop enabled with a selection: $(CropEnabled)"
[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]1109, [IntPtr]::Zero) | Out-Null   # Edit > Crop to selection
Start-Sleep -Milliseconds 300
$cropped = Frames
"frames after crop: $cropped; crop enabled with everything selected: $(CropEnabled)"
Shot $main "ui2_cropped"
[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]1101, [IntPtr]::Zero) | Out-Null   # Edit > Undo
Start-Sleep -Milliseconds 300
$undone = Frames
"frames after undo: $undone"
Shot $main "ui2_crop_undone"
if ($cropped -ne 40 -or $undone -ne 100) { throw "crop: expected 40 frames, then 100 after undo" }
} finally {
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
