[CmdletBinding()]
param(
    [string]$FirstName = "Smoke",
    [string]$LastName = "User",
    [string]$FirestormPath,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Read-IniValue {
    param([string]$Path, [string]$Section, [string]$Key)

    $currentSection = ""
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[([^]]+)\]$') {
            $currentSection = $Matches[1]
            continue
        }
        if ($currentSection -eq $Section -and $trimmed -match '^([^=]+?)\s*=\s*(.*)$' -and
            $Matches[1].Trim() -eq $Key) {
            return $Matches[2]
        }
    }
    throw "Missing [$Section] $Key in '$Path'."
}

function Wait-ServiceReady {
    param([string]$Uri, [System.Diagnostics.Process]$Process)

    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($Process.HasExited) { return $false }
        try {
            $response = Invoke-WebRequest -Uri $Uri -TimeoutSec 1 -SkipHttpErrorCheck
            if ($response.StatusCode -eq 200) { return $true }
        } catch {
            # The listener may not have started yet.
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Stop-SmokeProcess {
    param([System.Diagnostics.Process]$Process)

    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        $Process.WaitForExit()
    }
}

$gridExecutable = Join-Path $repositoryRoot "build\windows-vcpkg\grid\homeworldz-grid.exe"
$regionExecutable = Join-Path $repositoryRoot "build\windows-vcpkg\region\Debug\homeworldz-region.exe"
foreach ($executable in @($gridExecutable, $regionExecutable)) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Required executable '$executable' was not found. Run scripts\build-region.ps1 -Test and build the grid first."
    }
}

if (-not $FirestormPath) {
    $uninstallRoots = @(
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    $entry = Get-ItemProperty $uninstallRoots -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq "FirestormOS-Releasex64" } |
        Select-Object -First 1
    if ($entry -and $entry.DisplayIcon) {
        $FirestormPath = $entry.DisplayIcon.Trim('"')
    }
}
if (-not $ValidateOnly -and (-not $FirestormPath -or -not (Test-Path -LiteralPath $FirestormPath -PathType Leaf))) {
    throw "FirestormOS-Releasex64 was not found. Install the pinned viewer or pass -FirestormPath."
}

$databaseConfig = Join-Path $repositoryRoot "config\db.ini"
$gridConfig = Join-Path $repositoryRoot "config\grid.ini"
$env:HOMEWORLDZ_DATABASE_URL = Read-IniValue $databaseConfig "database" "url"
$serviceToken = Read-IniValue $gridConfig "auth" "service_token"
$env:HOMEWORLDZ_GRID_SERVICE_TOKEN = $serviceToken
$env:HOMEWORLDZ_GRID_URL = "http://127.0.0.1:42000"
$env:HOMEWORLDZ_REGION_PUBLIC_ENDPOINT = "http://127.0.0.1:42001"
$env:HOMEWORLDZ_REGION_DATA_PATH = Join-Path $repositoryRoot "var\region"

$logDirectory = Join-Path $repositoryRoot "var\smoke-test"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$grid = $null
$region = $null
try {
    $grid = Start-Process -FilePath $gridExecutable -WorkingDirectory $repositoryRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory "$stamp-grid.stdout.log") `
        -RedirectStandardError (Join-Path $logDirectory "$stamp-grid.stderr.log")
    if (-not (Wait-ServiceReady "http://127.0.0.1:42000/ready" $grid)) {
        throw "The grid did not become ready. Inspect '$logDirectory'."
    }

    $region = Start-Process -FilePath $regionExecutable -WorkingDirectory $repositoryRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory "$stamp-region.stdout.log") `
        -RedirectStandardError (Join-Path $logDirectory "$stamp-region.stderr.log")
    if (-not (Wait-ServiceReady "http://127.0.0.1:42001/ready" $region)) {
        throw "The region did not become ready. Inspect '$logDirectory'."
    }

    Write-Host "HomeWorldz grid and region are ready on loopback."
    if ($ValidateOnly) {
        Write-Host "Smoke-test launcher validation completed."
        return
    }

    $username = ("$FirstName.$LastName").ToLowerInvariant()
    if ($username -notmatch '^[a-z0-9._-]{3,32}$') {
        throw "The combined development username '$username' is invalid."
    }
    $securePassword = Read-Host "Password for Firestorm user '$FirstName $LastName' (8-128 characters)" -AsSecureString
    $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
        if ($password.Length -lt 8 -or $password.Length -gt 128) {
            throw "The development-user password must contain 8 to 128 characters."
        }
        $headers = @{ Authorization = "Bearer $serviceToken" }
        $body = @{ username = $username; password = $password } | ConvertTo-Json -Compress
        $created = Invoke-WebRequest -Method Post -Uri "http://127.0.0.1:42000/api/v1/users" `
            -Headers $headers -ContentType "application/json" -Body $body -SkipHttpErrorCheck
        if ($created.StatusCode -eq 409) {
            $sessionBody = @{ username = $username; password = $password; sessionSeconds = 300 } |
                ConvertTo-Json -Compress
            $session = Invoke-WebRequest -Method Post -Uri "http://127.0.0.1:42000/api/v1/sessions" `
                -Headers $headers -ContentType "application/json" -Body $sessionBody -SkipHttpErrorCheck
            if ($session.StatusCode -ne 201) {
                throw "Development user '$username' exists, but the supplied password is not valid."
            }
            $sessionId = ($session.Content | ConvertFrom-Json).id
            Invoke-WebRequest -Method Delete -Uri "http://127.0.0.1:42000/api/v1/sessions/$sessionId" `
                -Headers $headers -SkipHttpErrorCheck | Out-Null
        } elseif ($created.StatusCode -ne 201) {
            throw "Development-user creation failed with HTTP $($created.StatusCode): $($created.Content)"
        }
    } finally {
        if ($passwordPointer -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
        }
        $password = $null
        $body = $null
    }

    Write-Host "Launching Firestorm for '$FirstName $LastName'."
    Write-Host "Keep this terminal open. Exit Firestorm when the login, disconnect, and reconnect checks are complete."
    Start-Process -FilePath $FirestormPath -ArgumentList @("--loginuri", "http://127.0.0.1:42000/login") -Wait
} finally {
    Stop-SmokeProcess $region
    Stop-SmokeProcess $grid
}
