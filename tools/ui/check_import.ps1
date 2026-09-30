param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path)
# File > Import WAV: exports ba.csv with File > Export WAV, imports that WAV through the file and import dialogs,
# checks the new document's title and the Save As suggestion, then imports a CSV file as a WAV to see the error
# leave the document untouched. Screenshots go to build\ui.
$Out = "$Repo\build\ui"; New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
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
$IDM_SAVEAS = 1004; $IDM_EXPORTWAV = 1005; $IDM_IMPORTWAV = 1007; $IDOK = 1; $IDCANCEL = 2; $IDNO = 7

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
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
Start-Sleep -Milliseconds 500
Shot $main "ui_import_result"
"imported: $(Title $main)"

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
$dlg = WaitDialog "Import WAV"
[U]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
$err = WaitDialog "Klatt Parameter Editor"
Shot $err "ui_import_error"
[U]::PostMessageW($err, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400
WaitTitle $main "ui_import.wav* - Klatt Parameter Editor"
"after the error: $(Title $main)"
} finally {
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
