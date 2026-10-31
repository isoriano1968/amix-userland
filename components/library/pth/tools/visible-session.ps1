param(
    [ValidateSet('AMIX', 'Linux')]
    [string]$Target,
    [string]$CommandsFile,
    [string]$PythonExecutable,
    [string]$LogName = 'visible-session.log',
    [string]$WatchDirectory
)
$ErrorActionPreference = 'Stop'
$host.UI.RawUI.WindowTitle = "pthreads-amix - $Target visible session"
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$logPath = Join-Path $projectRoot "local/logs/$LogName"
Write-Host "Visible $Target session - commands and output are logged to $logPath"
$sessionSecret = $env:AMIX_PASS
function Invoke-VisibleCommands([string]$file) {
 $commands = @(Get-Content -LiteralPath $file -Raw | ConvertFrom-Json)
 if ($Target -eq 'AMIX') {
    $env:AMIX_PASS = $sessionSecret
    if (-not $env:AMIX_HOST -or -not $env:AMIX_PASS) {
        throw 'AMIX credentials must be supplied in the parent process environment'
    }
    & $PythonExecutable -u tools/run-amix.py --timeout 65 @commands |
        Tee-Object -FilePath $logPath -Append
    Remove-Item Env:AMIX_PASS
 } else {
    foreach ($command in $commands) {
        if ($command -isnot [string]) {
            $vmRoot = Join-Path $projectRoot 'local/linuxvm'
            if ($command.upload) {
                $source = Join-Path $projectRoot $command.upload
                $destination = 'builder@127.0.0.1:' + $command.destination
            } elseif ($command.download) {
                $source = 'builder@127.0.0.1:' + $command.download
                $destination = Join-Path $projectRoot $command.destination
            } else { throw 'Unknown file-transfer job' }
            "SCP $source -> $destination" | Tee-Object -FilePath $logPath -Append
            & scp.exe -i (Join-Path $vmRoot 'id_ed25519') -P 2228 `
                -o BatchMode=yes -o "UserKnownHostsFile=$(Join-Path $vmRoot 'known_hosts')" `
                $source $destination | Tee-Object -FilePath $logPath -Append
            "SCP exit=$LASTEXITCODE" | Tee-Object -FilePath $logPath -Append
            continue
        }
        "builder@pthreads-amix-build > $command" |
            Tee-Object -FilePath $logPath -Append
        & "$PSScriptRoot/linux-vm.ps1" ssh $command |
            Tee-Object -FilePath $logPath -Append
        "SSH command exit=$LASTEXITCODE" |
            Tee-Object -FilePath $logPath -Append
    }
 }
}
if ($WatchDirectory) {
    Write-Host "Watching command queue $WatchDirectory. Close this window to stop."
    while (-not (Test-Path (Join-Path $WatchDirectory '.stop'))) {
        Get-ChildItem -LiteralPath $WatchDirectory -Filter '*.json' |
            Sort-Object Name | ForEach-Object {
                $doneFile = $_.FullName + '.done'
                if (-not (Test-Path -LiteralPath $doneFile)) {
                    Invoke-VisibleCommands $_.FullName
                    Set-Content -LiteralPath $doneFile -Value "completed $(Get-Date -Format o)"
                }
            }
        Start-Sleep -Milliseconds 500
    }
} else {
    Invoke-VisibleCommands $CommandsFile
    Write-Host 'Session complete. This window stays open for inspection.'
}
