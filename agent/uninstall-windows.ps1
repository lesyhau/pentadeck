# Remove the Pentadeck stats agent that install-windows.ps1 set up for this Windows user:
#   powershell -ExecutionPolicy Bypass -File uninstall-windows.ps1     (or double-click uninstall-windows.cmd)
#   irm https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/uninstall-windows.ps1 | iex
#
# Stops it, removes its Startup entry and %LOCALAPPDATA%\Pentadeck, asks whether to remove the saved
# device token (PENTADECK_TOKEN), and removes its firewall rule (one admin prompt).
#
#   -Yes          ask nothing (keeps the token unless -RemoveToken)
#   -RemoveToken  also remove the PENTADECK_TOKEN user environment variable
#   -Sandbox DIR  (for testing) undo an install made with install-windows.ps1 -Sandbox DIR
param(
    [switch]$Yes,
    [switch]$RemoveToken,
    [string]$Sandbox = ''
)

$ErrorActionPreference = 'Stop'

function Uninstall-PentadeckAgent {
    $sandboxDir = $Sandbox
    if (-not $sandboxDir -and $env:PENTADECK_SANDBOX) { $sandboxDir = $env:PENTADECK_SANDBOX }
    if ($sandboxDir) {
        $sandboxDir = [IO.Path]::GetFullPath($sandboxDir)
        $installDir = Join-Path $sandboxDir 'Pentadeck'
        $startupDir = Join-Path $sandboxDir 'Startup'
        Write-Host "Sandbox: removing the install under $sandboxDir" -ForegroundColor Yellow
    } else {
        $installDir = Join-Path $env:LOCALAPPDATA 'Pentadeck'
        $startupDir = [Environment]::GetFolderPath('Startup')
    }
    $backupDir = Join-Path (Split-Path $installDir) 'Pentadeck-backup'
    $agentPath = (Join-Path $installDir 'pentadeck_agent.py').ToLowerInvariant()
    $nonInteractive = [Environment]::GetCommandLineArgs() | Where-Object { $_ -match '^-NonI' }
    $ask = (-not $Yes) -and [Environment]::UserInteractive -and (-not $nonInteractive)

    # Stop the running agent (only the one started from our folder).
    $procs = Get-CimInstance Win32_Process -Filter "Name like 'py%.exe'" -ErrorAction SilentlyContinue
    foreach ($p in @($procs)) {
        if ("$($p.CommandLine)".ToLowerInvariant().Contains($agentPath)) {
            Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
            Write-Host "Stopped the agent (pid $($p.ProcessId))."
        }
    }

    $lnk = Join-Path $startupDir 'Pentadeck agent.lnk'
    if (Test-Path $lnk) { Remove-Item $lnk -Force; Write-Host "Removed $lnk" }

    if (Test-Path $installDir) {
        for ($i = 0; $i -lt 10; $i++) {   # the process may take a moment to let go
            try { Remove-Item $installDir -Recurse -Force; break } catch { Start-Sleep -Milliseconds 300 }
        }
        if (Test-Path $installDir) { throw "Could not remove $installDir" }
        Write-Host "Removed $installDir"
    }

    if (-not $sandboxDir -and [Environment]::GetEnvironmentVariable('PENTADECK_TOKEN', 'User')) {
        $drop = [bool]$RemoveToken
        if (-not $drop -and $ask) {
            $a = Read-Host 'Also remove the saved device token (the PENTADECK_TOKEN environment variable)? [y/N]'
            $drop = ("$a".Trim() -match '^[yY]')
        }
        if ($drop) {
            [Environment]::SetEnvironmentVariable('PENTADECK_TOKEN', $null, 'User')
            Write-Host 'Removed PENTADECK_TOKEN.'
        } else {
            Write-Host 'Kept PENTADECK_TOKEN.'
        }
    }

    $rules = @(Get-NetFirewallRule -Group 'Pentadeck' -ErrorAction SilentlyContinue)
    if ($rules.Count) {
        $cmd = "Remove-NetFirewallRule -Group 'Pentadeck'"
        if ($sandboxDir) {
            Write-Host "Sandbox: would run as admin: $cmd"
        } else {
            $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(
                "`$ErrorActionPreference = 'Stop'; try { $cmd; exit 0 } catch { exit 1 }"))
            $argv = '-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $enc
            $ok = $false
            try {
                Write-Host 'Windows will ask for admin rights to remove the firewall rule.'
                $p = Start-Process powershell.exe -ArgumentList $argv -Verb RunAs -WindowStyle Hidden -Wait -PassThru
                $ok = ($p.ExitCode -eq 0)
            } catch {
                Write-Host 'You declined the admin prompt.' -ForegroundColor Yellow
            }
            if ($ok) {
                Write-Host 'Removed the firewall rule.'
            } else {
                Write-Host "The firewall rule is still there. As admin, run: $cmd" -ForegroundColor Yellow
            }
        }
    }

    if (Test-Path $backupDir) {
        Write-Host ''
        Write-Host "An older setup the installer replaced is kept in $backupDir."
        Write-Host 'To bring it back, move its file into your Startup folder (Win+R, shell:startup).'
    }
    Write-Host ''
    Write-Host 'The Pentadeck agent is removed.' -ForegroundColor Green
}

$rc = 0
try {
    Uninstall-PentadeckAgent
} catch {
    Write-Host "Uninstall failed: $($_.Exception.Message)" -ForegroundColor Red
    $rc = 1
}
if ($PSCommandPath) { exit $rc }
