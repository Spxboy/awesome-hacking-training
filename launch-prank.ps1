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

# ─── Win32 API — taskbar control + window-style manipulation ─────────────────
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
    const uint WS_EX_TOOLWINDOW = 0x00000080;  // hides from taskbar + Alt+Tab
    const uint WS_EX_NOACTIVATE = 0x08000000;  // won't steal focus / appear active

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

# ─── Hide taskbar ────────────────────────────────────────────────────────────
[WinAPI]::ShowWindow($taskbar, 0) | Out-Null   # 0 = hide

# ─── Optionally disable Task Manager ─────────────────────────────────────────
if (-not $NoTaskMgrBlock) {
    New-Item -Path $TmPath -Force | Out-Null
    Set-ItemProperty -Path $TmPath -Name 'DisableTaskMgr' -Value 1
    # Note: Ctrl+Alt+Del (the secure attention sequence) always works at OS
    # level and cannot be fully blocked — this only blocks Ctrl+Shift+Esc.
}

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
        $proc = Start-Process $browser $kioskFlags -PassThru
        # Wait until ALL Edge/Chrome processes spawned by this kiosk session exit
        $procName = [System.IO.Path]::GetFileNameWithoutExtension($browser)
        Start-Sleep -Seconds 3   # give the browser time to launch
        do { Start-Sleep -Seconds 1 } while (
            Get-Process -Name $procName -ErrorAction SilentlyContinue |
            Where-Object { $_.SessionId -eq $proc.SessionId }
        )
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
    Remove-ItemProperty -Path $TmPath     -Name 'DisableTaskMgr' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $StartupKey -Name $StartupName     -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $CounterKey -Name $CounterName     -ErrorAction SilentlyContinue
}
