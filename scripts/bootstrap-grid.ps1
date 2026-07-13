[CmdletBinding()]
param(
    [string]$HostName = "localhost",
    [ValidateRange(1, 65535)]
    [int]$Port = 5432,
    [string]$AdminUser = "postgres",
    [string]$AdminDatabase = "postgres",
    [string]$ApplicationUser = "homeworldz",
    [string]$ApplicationDatabase = "homeworldz",
    [string]$PsqlPath
)

$ErrorActionPreference = "Stop"

function Find-Psql {
    if ($PsqlPath) {
        if (-not (Test-Path -LiteralPath $PsqlPath -PathType Leaf)) {
            throw "psql was not found at '$PsqlPath'."
        }
        return (Resolve-Path -LiteralPath $PsqlPath).Path
    }

    $command = Get-Command psql -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $installRoot = Join-Path $env:ProgramFiles "PostgreSQL"
    if (Test-Path -LiteralPath $installRoot) {
        $candidate = Get-ChildItem -LiteralPath $installRoot -Directory |
            Where-Object { $_.Name -match '^\d+$' } |
            Sort-Object { [int]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\psql.exe" } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }

    throw "psql was not found. Add PostgreSQL's bin directory to PATH or pass -PsqlPath."
}

function ConvertFrom-SecurePassword([Security.SecureString]$SecurePassword) {
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecurePassword)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
}

function ConvertTo-SqlLiteral([string]$Value) {
    return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-Psql {
    param(
        [string]$Executable,
        [string]$Password,
        [string]$User,
        [string]$Database,
        [string[]]$Arguments,
        [string]$InputSql
    )

    $previousPassword = $env:PGPASSWORD
    try {
        $env:PGPASSWORD = $Password
        if ($InputSql) {
            $InputSql | & $Executable -X -v ON_ERROR_STOP=1 -h $HostName -p $Port -U $User -d $Database @Arguments
        }
        else {
            & $Executable -X -v ON_ERROR_STOP=1 -h $HostName -p $Port -U $User -d $Database @Arguments
        }
        if ($LASTEXITCODE -ne 0) {
            throw "psql exited with code $LASTEXITCODE."
        }
    }
    finally {
        $env:PGPASSWORD = $previousPassword
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$migration = Join-Path $repositoryRoot "db\migrations\000001_initial.up.sql"
if (-not (Test-Path -LiteralPath $migration -PathType Leaf)) {
    throw "Migration file was not found at '$migration'."
}

$psql = Find-Psql
$adminSecure = Read-Host "PostgreSQL password for '$AdminUser'" -AsSecureString
$applicationSecure = Read-Host "Password to assign to '$ApplicationUser'" -AsSecureString
$applicationConfirm = Read-Host "Confirm password for '$ApplicationUser'" -AsSecureString

$adminPassword = ConvertFrom-SecurePassword $adminSecure
$applicationPassword = ConvertFrom-SecurePassword $applicationSecure
$confirmation = ConvertFrom-SecurePassword $applicationConfirm

try {
    if ([string]::IsNullOrWhiteSpace($applicationPassword)) {
        throw "The application password cannot be empty."
    }
    if ($applicationPassword -cne $confirmation) {
        throw "The application passwords do not match."
    }

    Write-Host "Checking PostgreSQL connection..."
    Invoke-Psql -Executable $psql -Password $adminPassword -User $AdminUser `
        -Database $AdminDatabase -Arguments @("-q", "-c", "SELECT 1;")

    $previousPassword = $env:PGPASSWORD
    try {
        $env:PGPASSWORD = $adminPassword
        $roleExists = (@(& $psql -X -qAt -v ON_ERROR_STOP=1 -h $HostName -p $Port -U $AdminUser -d $AdminDatabase -c "SELECT 1 FROM pg_roles WHERE rolname = $(ConvertTo-SqlLiteral $ApplicationUser);") -join "").Trim()
        if ($LASTEXITCODE -ne 0) { throw "Could not query PostgreSQL roles." }
    }
    finally {
        $env:PGPASSWORD = $previousPassword
    }

    $roleName = '"' + $ApplicationUser.Replace('"', '""') + '"'
    $passwordLiteral = ConvertTo-SqlLiteral $applicationPassword
    if ($roleExists -eq "1") {
        Write-Host "Updating role '$ApplicationUser'..."
        Invoke-Psql -Executable $psql -Password $adminPassword -User $AdminUser `
            -Database $AdminDatabase -Arguments @("-q") `
            -InputSql "ALTER ROLE $roleName LOGIN PASSWORD $passwordLiteral;"
    }
    else {
        Write-Host "Creating role '$ApplicationUser'..."
        Invoke-Psql -Executable $psql -Password $adminPassword -User $AdminUser `
            -Database $AdminDatabase -Arguments @("-q") `
            -InputSql "CREATE ROLE $roleName LOGIN PASSWORD $passwordLiteral;"
    }

    $previousPassword = $env:PGPASSWORD
    try {
        $env:PGPASSWORD = $adminPassword
        $databaseExists = (@(& $psql -X -qAt -v ON_ERROR_STOP=1 -h $HostName -p $Port -U $AdminUser -d $AdminDatabase -c "SELECT 1 FROM pg_database WHERE datname = $(ConvertTo-SqlLiteral $ApplicationDatabase);") -join "").Trim()
        if ($LASTEXITCODE -ne 0) { throw "Could not query PostgreSQL databases." }
    }
    finally {
        $env:PGPASSWORD = $previousPassword
    }

    if ($databaseExists -ne "1") {
        Write-Host "Creating database '$ApplicationDatabase'..."
        $databaseName = '"' + $ApplicationDatabase.Replace('"', '""') + '"'
        Invoke-Psql -Executable $psql -Password $adminPassword -User $AdminUser `
            -Database $AdminDatabase `
            -Arguments @("-q", "-c", "CREATE DATABASE $databaseName OWNER $roleName;")
    }
    else {
        Write-Host "Database '$ApplicationDatabase' already exists."
    }

    $previousPassword = $env:PGPASSWORD
    try {
        $env:PGPASSWORD = $applicationPassword
        $migrationApplied = (@(& $psql -X -qAt -h $HostName -p $Port -U $ApplicationUser -d $ApplicationDatabase -c "SELECT to_regclass('public.schema_metadata');" 2>$null) -join "").Trim()
    }
    finally {
        $env:PGPASSWORD = $previousPassword
    }

    if ($migrationApplied -eq "schema_metadata") {
        Write-Host "Initial migration is already applied."
    }
    else {
        Write-Host "Applying initial migration..."
        Invoke-Psql -Executable $psql -Password $applicationPassword `
            -User $ApplicationUser -Database $ApplicationDatabase `
            -Arguments @("-q", "-f", $migration)
    }

    Write-Host "HomeWorldz PostgreSQL bootstrap completed."
    Write-Host "Set HOMEWORLDZ_DATABASE_URL with the application password before starting the grid service."
}
finally {
    $adminPassword = $null
    $applicationPassword = $null
    $confirmation = $null
}
