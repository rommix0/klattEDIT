param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)
# File > Import WAV: exports ba.csv with File > Export WAV, imports that WAV through the file and import dialogs (a
# range dragged on the waveform: the document is as long as the range), checks the new document's title and the Save
# As suggestion, then imports a CSV file as a WAV to see the error leave the document untouched. Screenshots go to
# build\ui.
$Out = "$Repo\build\ui"; New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
  // The texts of every Edit control below h (the file dialogs nest theirs several levels deep).
  public static string[] EditTexts(IntPtr h) {
    var list = new System.Collections.Generic.List<string>();
    EnumChildWindows(h, (c, l) => {
      var cls = new StringBuilder(64); GetClassNameW(c, cls, 64);
      if (cls.ToString() == "Edit") { var t = new StringBuilder(512); SendMessageW(c, 0x0D, (IntPtr)512, t); list.Add(t.ToString()); }
      return true; }, IntPtr.Zero);
    return list.ToArray();
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
function Title($h) { $s = New-Object System.Text.StringBuilder 512; [U]::GetWindowTextW($h, $s, 512) | Out-Null; $s.ToString() }
# Waits for a top-level dialog with this title; throws after about 10 s.
function WaitDialog($title) {
  for ($i = 0; $i -lt 100; $i++) {
    $h = [U]::FindWindowW("#32770", $title)
    if ($h -ne [IntPtr]::Zero) { Start-Sleep -Milliseconds 300; return $h }
    Start-Sleep -Milliseconds 100
  }
  throw "no dialog '$title'"
}
function WaitTitle($h, $want) {
  for ($i = 0; $i -lt 100; $i++) { if ((Title $h) -eq $want) { return }; Start-Sleep -Milliseconds 100 }
  throw "title is '$(Title $h)', expected '$want'"
}
# Types a path into a file dialog's file name box (it has the focus when the dialog opens) and confirms it.
function TypePath($dlg, $path) {
  [U]::SetForegroundWindow($dlg) | Out-Null; Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait($path); Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
}
$WM_COMMAND = 0x111
$IDM_SAVEAS = 1004; $IDM_EXPORTWAV = 1005; $IDM_IMPORTWAV = 1007; $IDM_TABLE = 1501; $IDOK = 1; $IDCANCEL = 2; $IDNO = 7
$IDC_WAVE = 2405; $IDC_RANGE_START = 2406; $IDC_RANGE_END = 2407

$wav = "$Out\ui_import.wav"; Remove-Item $wav -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$Repo\build\32\kpe.exe" -ArgumentList "`"$Repo\samples\ba.csv`"" -PassThru
Start-Sleep -Milliseconds 1500
$p.Refresh(); $main = $p.MainWindowHandle
try {
[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_EXPORTWAV, [IntPtr]::Zero) | Out-Null   # File > Export WAV
TypePath (WaitDialog "Save As") $wav
for ($i = 0; $i -lt 50 -and -not (Test-Path $wav); $i++) { Start-Sleep -Milliseconds 100 }
if (-not (Test-Path $wav)) { throw "Export WAV wrote no file" }
"exported $((Get-Item $wav).Length) bytes"

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_IMPORTWAV, [IntPtr]::Zero) | Out-Null   # File > Import WAV
TypePath (WaitDialog "Open") $wav
$dlg = WaitDialog "Import WAV"
Shot $dlg "ui_import_dialog"
# drag from a quarter to three quarters of the waveform strip
$wave = [U]::GetDlgItem($dlg, $IDC_WAVE)
$wr = New-Object U+RECT; [U]::GetClientRect($wave, [ref]$wr) | Out-Null
$y = [int]($wr.B / 2)
foreach ($m in @(@(0x201, 0.25, 1), @(0x200, 0.5, 1), @(0x200, 0.75, 1), @(0x202, 0.75, 0))) {
  $x = [int]($wr.R * $m[1])
  [U]::SendMessageW($wave, [uint32]$m[0], [IntPtr]$m[2], [IntPtr](($y -shl 16) -bor $x)) | Out-Null
}
function Field($id) {
  $t = New-Object System.Text.StringBuilder 64
  [U]::SendMessageW([U]::GetDlgItem($dlg, $id), 0x0D, [IntPtr]64, $t) | Out-Null                # WM_GETTEXT
  [double]$t.ToString()
}
$start = Field $IDC_RANGE_START; $end = Field $IDC_RANGE_END
"range dragged: $start - $end s"
Shot $dlg "ui_import_range"
if (-not ($start -gt 0.15 -and $start -lt 0.25 -and $end -gt 0.55 -and $end -lt 0.65)) { throw "the range is $start - $end s" }
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
Start-Sleep -Milliseconds 500
Shot $main "ui_import_result"
"imported: $(Title $main)"
[U]::SendMessageW($main, $WM_COMMAND, [IntPtr]$IDM_TABLE, [IntPtr]::Zero) | Out-Null       # View > Frame table
Start-Sleep -Milliseconds 400
$list = [U]::FindWindowExW([U]::FindWindowW("KpeTable", "Frame table"), [IntPtr]::Zero, "SysListView32", $null)
$frames = [int][U]::SendMessageW($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)           # LVM_GETITEMCOUNT
$want = [math]::Round(($end - $start) * 200)                                               # 5 ms frames
"document: $frames frames for $([math]::Round(($end - $start) * 1000)) ms"
if ([math]::Abs($frames - $want) -gt 1) { throw "the document has $frames frames, expected $want" }

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_SAVEAS, [IntPtr]::Zero) | Out-Null      # File > Save As
$dlg = WaitDialog "Save As"
$names = [U]::EditTexts($dlg)                                                               # the file name box
Shot $dlg "ui_import_saveas"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDCANCEL, [IntPtr]::Zero) | Out-Null
if (-not ($names -contains "ui_import.csv")) { throw "Save As suggests '$($names -join "', '")', not ui_import.csv" }
"Save As suggests ui_import.csv"
Start-Sleep -Milliseconds 400

[U]::PostMessageW($main, $WM_COMMAND, [IntPtr]$IDM_IMPORTWAV, [IntPtr]::Zero) | Out-Null   # a CSV file as the WAV
$ask = WaitDialog "Klatt Parameter Editor"                                                 # unsaved changes: No
[U]::PostMessageW($ask, $WM_COMMAND, [IntPtr]$IDNO, [IntPtr]::Zero) | Out-Null
TypePath (WaitDialog "Open") "$Repo\samples\ba.csv"
$err = WaitDialog "Klatt Parameter Editor"                                                 # read first: no Import dialog
Shot $err "ui_import_error"
[U]::PostMessageW($err, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
"after the error: $(Title $main)"
} finally {
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
