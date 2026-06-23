@echo off
setlocal
chcp 65001 >nul 2>nul
cd /d "%~dp0"

title QSS-M Updater
color 0B

echo ============================================================
echo                🚀 QSS-M Windows Updater
echo ============================================================
echo.
echo 📁 Folder:
echo   %CD%
echo.
echo 🧭 Task:
echo   Download latest successful GitHub Actions artifact for your saved platform
echo   and update your configured QSS-M folder.
echo.

where powershell >nul 2>nul
if errorlevel 1 (
    color 0C
    echo ❌ [ERROR] Windows PowerShell was not found.
    echo.
    echo PowerShell is built into supported Windows versions and is required
    echo to run this self-contained updater.
    echo.
    echo This window will close in 30 seconds...
    timeout /t 30 >nul
    exit /b 1
)

set "UPDATER_SELF=%~f0"
set "UPDATER_PS1=%TEMP%\qssm_updater_%RANDOM%_%RANDOM%.ps1"
set "QSSM_UPDATER_LAUNCHER_NAME=%~nx0"

powershell -NoProfile -ExecutionPolicy Bypass -Command "$self=$env:UPDATER_SELF; $out=$env:UPDATER_PS1; $marker='__QSSM_UPDATER_POWERSHELL_PAYLOAD_BELOW__'; $lines=[IO.File]::ReadAllLines($self); $idx=[Array]::IndexOf($lines,$marker); if ($idx -lt 0) { [Console]::Error.WriteLine('Embedded PowerShell payload marker was not found.'); exit 1 }; if ($idx -ge ($lines.Length - 1)) { [Console]::Error.WriteLine('Embedded PowerShell payload is empty.'); exit 1 }; $payload=$lines[($idx + 1)..($lines.Length - 1)]; $enc=New-Object System.Text.UTF8Encoding $true; [IO.File]::WriteAllLines($out,$payload,$enc)"
if errorlevel 1 (
    color 0C
    echo ❌ [ERROR] Could not unpack the embedded updater code.
    echo.
    echo This window will close in 30 seconds...
    timeout /t 30 >nul
    exit /b 1
)

echo 🚀 [START] Running updater...
echo ------------------------------------------------------------
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%UPDATER_PS1%" %*
set "RC=%ERRORLEVEL%"

if exist "%UPDATER_PS1%" del /q "%UPDATER_PS1%" >nul 2>nul

echo.
echo ------------------------------------------------------------

if not "%RC%"=="0" (
    color 0C
    echo ❌ [FAILED] QSS-M update failed.
    echo.
    echo Tips:
    echo   - Run setup again:
    echo       update_qssm_from_github.bat --setup
    echo   - Paste only the token when prompted
    echo   - Do not include the word Bearer
    echo   - Fine-grained token permissions:
    echo       Metadata: Read-only
    echo       Actions: Read-only
    echo.
    echo If the latest commit is still building, run:
    echo   update_qssm_from_github.bat --allow-older
    echo.
    echo This window will close in 45 seconds...
    timeout /t 45 >nul
    exit /b %RC%
)

color 0A
echo ✅ [SUCCESS] QSS-M update finished.
echo.
echo This window will close automatically in 8 seconds...
timeout /t 8 >nul

exit /b 0

__QSSM_UPDATER_POWERSHELL_PAYLOAD_BELOW__
$ErrorActionPreference = 'Stop'

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
} catch {
}

$ApiRoot = 'https://api.github.com'
$AppName = 'QSSMUpdater'
$ConfigFileName = 'config.json'
$ConfigVersion = 2
$DefaultOwner = 'timbergeron'
$DefaultRepo = 'QSS-M'

$PlatformPresets = @{
    windows = @{
        Label = 'Windows 64-bit'
        WorkflowName = 'Windows CI (MinGW Linux Cross)'
        JobName = 'w64'
        ArtifactNameHint = 'QSS-M-w64.zip'
        LaunchGlob = '*.exe'
    }
    macos = @{
        Label = 'macOS universal'
        WorkflowName = 'macOS CI (Xcode)'
        JobName = 'arm64-universal'
        ArtifactNameHint = 'QSS-M_MacOS.zip'
        LaunchGlob = '*.app'
    }
    linux = @{
        Label = 'Linux 64-bit'
        WorkflowName = 'Linux CI'
        JobName = 'x64'
        ArtifactNameHint = 'QSS-M-l64.zip'
        LaunchGlob = 'qss-m*'
    }
}

function New-UpdaterError {
    param([string]$Message)
    return (New-Object System.InvalidOperationException -ArgumentList $Message)
}

$Script:UiUseColor = [string]::IsNullOrWhiteSpace($env:NO_COLOR)
$Script:UiUseEmoji = -not ([string]$env:QSSM_NO_EMOJI -match '^(1|true|yes|on)$')

function Format-UiText {
    param([string]$Icon, [object]$Message)
    if ($Script:UiUseEmoji -and -not [string]::IsNullOrWhiteSpace($Icon)) {
        return "$Icon $Message"
    }
    return [string]$Message
}

function Write-UiLine {
    param([object]$Message = '', [string]$Icon = '', [string]$Color = $null)
    if ([string]::IsNullOrEmpty([string]$Message) -and [string]::IsNullOrEmpty($Icon)) {
        Write-Host ''
        return
    }
    $text = Format-UiText $Icon $Message
    if ($Script:UiUseColor -and -not [string]::IsNullOrWhiteSpace($Color)) {
        Write-Host $text -ForegroundColor $Color
    } else {
        Write-Host $text
    }
}

function Write-UiError {
    param([object]$Message, [string]$Icon = '❌')
    $text = Format-UiText $Icon $Message
    if ($Script:UiUseColor) {
        try {
            $oldColor = [Console]::ForegroundColor
            [Console]::ForegroundColor = [ConsoleColor]::Red
            [Console]::Error.WriteLine($text)
            [Console]::ForegroundColor = $oldColor
            return
        } catch {
        }
    }
    [Console]::Error.WriteLine($text)
}

function Write-UiSection {
    param([string]$Title, [string]$Icon = '')
    Write-Host ''
    Write-UiLine $Title $Icon 'Cyan'
    Write-UiLine '------------------------------------------------------------' '' 'DarkGray'
}

function Write-UiKv {
    param([string]$Label, [object]$Value, [string]$Icon = '•')
    $marker = ''
    if ($Script:UiUseEmoji -and -not [string]::IsNullOrWhiteSpace($Icon)) {
        $marker = "$Icon "
    }
    if ($Script:UiUseColor) {
        Write-Host -NoNewline ($marker + $Label + ': ') -ForegroundColor Cyan
        Write-Host ([string]$Value)
    } else {
        Write-Host ('{0}{1}: {2}' -f $marker, $Label, $Value)
    }
}

function Read-UiPrompt {
    param([string]$Label, [string]$Suffix = '', [string]$Icon = '❯', [switch]$Secret)
    $prompt = Format-UiText $Icon "$Label$Suffix`: "
    if ($Script:UiUseColor) {
        Write-Host -NoNewline $prompt -ForegroundColor Cyan
    } else {
        Write-Host -NoNewline $prompt
    }
    if ($Secret) {
        return (Read-Host -AsSecureString)
    }
    return (Read-Host)
}

function First-NonEmpty {
    foreach ($value in $args) {
        if ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value)) {
            return $value
        }
    }
    return $null
}

function Get-LauncherName {
    if ($env:QSSM_UPDATER_LAUNCHER_NAME) {
        return $env:QSSM_UPDATER_LAUNCHER_NAME
    }
    return 'update_qssm_from_github.bat'
}

function Show-Help {
    $prog = Get-LauncherName
    $help = @"
usage: $prog [-h] [--setup] [--config CONFIG] [--no-config] [--no-prompt]
             [--platform {linux,macos,windows}] [--owner OWNER] [--repo REPO]
             [--workflow-name WORKFLOW_NAME] [--job-name JOB_NAME]
             [--branch BRANCH] [--dest DEST] [--token TOKEN]
             [--artifact-name-hint ARTIFACT_NAME_HINT] [--include-logs]
             [--launch-glob LAUNCH_GLOB] [--payload-root PAYLOAD_ROOT]
             [--allow-older] [--dry-run] [--keep-temp] [--no-backup]
             [--mirror]

Download latest QSS-M CI artifact for Windows/macOS/Linux and update a local
QSS-M folder.

options:
  -h, --help            show this help message and exit
  --setup               Run first-time setup again and save config.
  --config CONFIG       Override config file path.
  --no-config           Ignore saved config and do not save prompted values.
  --no-prompt           Fail instead of asking interactive setup/token questions.
  --platform VALUE      Artifact platform preset: windows, macos, or linux.
  --owner OWNER
  --repo REPO
  --workflow-name NAME
  --job-name NAME
  --branch BRANCH       Defaults to repo default branch.
  --dest DEST
  --token TOKEN         Overrides saved config, GITHUB_TOKEN, and GitHub CLI auth.
  --artifact-name-hint VALUE
  --include-logs        Allow artifacts with 'log' in the name.
  --launch-glob GLOB    Launchable filename used to locate payload root.
  --payload-root PATH   Optional extracted subfolder to deploy.
  --allow-older         Use newest older success if latest commit has no success.
  --dry-run             Download and inspect, but do not update the destination.
  --keep-temp           Keep downloaded/extracted temp files for debugging.
  --no-backup           Skip the backup prompt and do not back up before updating.
  --mirror              Replace the destination folder exactly instead of merging.
"@
    Write-Host $help
}

function Parse-Args {
    param([string[]]$Arguments)

    $opts = @{
        Setup = $false
        Config = $null
        NoConfig = $false
        NoPrompt = $false
        Platform = $null
        Owner = $null
        Repo = $null
        WorkflowName = $null
        JobName = $null
        Branch = $null
        Dest = $null
        Token = $null
        ArtifactNameHint = $null
        IncludeLogs = $false
        LaunchGlob = $null
        PayloadRoot = $null
        AllowOlder = $false
        DryRun = $false
        KeepTemp = $false
        NoBackup = $false
        Mirror = $false
        ExeGlob = $null
    }

    $valueOptions = @{
        '--config' = 'Config'
        '--platform' = 'Platform'
        '--owner' = 'Owner'
        '--repo' = 'Repo'
        '--workflow-name' = 'WorkflowName'
        '--job-name' = 'JobName'
        '--branch' = 'Branch'
        '--dest' = 'Dest'
        '--token' = 'Token'
        '--artifact-name-hint' = 'ArtifactNameHint'
        '--launch-glob' = 'LaunchGlob'
        '--payload-root' = 'PayloadRoot'
        '--exe-glob' = 'ExeGlob'
    }

    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        $arg = $Arguments[$i]
        $name = $arg
        $value = $null

        if ($arg -match '^(--[^=]+)=(.*)$') {
            $name = $Matches[1]
            $value = $Matches[2]
        }

        if ($name -eq '-h' -or $name -eq '--help') {
            Show-Help
            exit 0
        } elseif ($valueOptions.ContainsKey($name)) {
            if ($null -eq $value) {
                $i++
                if ($i -ge $Arguments.Count) {
                    throw (New-UpdaterError "Missing value for $name.")
                }
                $value = $Arguments[$i]
            }
            $opts[$valueOptions[$name]] = $value
        } else {
            switch ($name) {
                '--setup' { $opts['Setup'] = $true }
                '--no-config' { $opts['NoConfig'] = $true }
                '--no-prompt' { $opts['NoPrompt'] = $true }
                '--include-logs' { $opts['IncludeLogs'] = $true }
                '--allow-older' { $opts['AllowOlder'] = $true }
                '--dry-run' { $opts['DryRun'] = $true }
                '--keep-temp' { $opts['KeepTemp'] = $true }
                '--no-backup' { $opts['NoBackup'] = $true }
                '--mirror' { $opts['Mirror'] = $true }
                default { throw (New-UpdaterError "Unknown argument: $arg") }
            }
        }
    }

    return $opts
}

function Get-ConfigValue {
    param($Config, [string]$Key)
    if ($null -eq $Config) {
        return $null
    }
    if ($Config -is [hashtable] -and $Config.ContainsKey($Key)) {
        return $Config[$Key]
    }
    return $null
}

function Set-ConfigValue {
    param([hashtable]$Config, [string]$Key, $Value)
    if ($null -eq $Value) {
        if ($Config.ContainsKey($Key)) {
            $Config.Remove($Key)
        }
    } else {
        $Config[$Key] = $Value
    }
}

function ConvertTo-Hashtable {
    param($Object)
    $result = @{}
    if ($null -eq $Object) {
        return $result
    }
    foreach ($prop in $Object.PSObject.Properties) {
        $result[$prop.Name] = $prop.Value
    }
    return $result
}

function Get-DefaultDest {
    try {
        $current = [Environment]::CurrentDirectory
        if (-not [string]::IsNullOrWhiteSpace($current)) {
            return [IO.Path]::GetFullPath($current)
        }
    } catch {
    }
    return [IO.Path]::GetFullPath('.')
}

function Detect-Platform {
    return 'windows'
}

function Normalize-Platform {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return (Detect-Platform)
    }

    $aliases = @{
        win = 'windows'
        windows = 'windows'
        w64 = 'windows'
        mac = 'macos'
        macos = 'macos'
        darwin = 'macos'
        osx = 'macos'
        linux = 'linux'
        l64 = 'linux'
    }

    $key = $Value.Trim().ToLowerInvariant()
    if ($aliases.ContainsKey($key)) {
        return $aliases[$key]
    }

    throw (New-UpdaterError "Unknown platform '$Value'. Use one of: windows, macos, linux.")
}

function Get-ConfigDir {
    if ($env:APPDATA) {
        return (Join-Path $env:APPDATA $AppName)
    }
    return (Join-Path (Join-Path $HOME 'AppData\Roaming') $AppName)
}

function Get-DefaultConfigPath {
    return (Join-Path (Get-ConfigDir) $ConfigFileName)
}

function Expand-PathSafe {
    param([string]$Raw)

    $path = [Environment]::ExpandEnvironmentVariables([string]$Raw)
    if ($path -eq '~') {
        $path = $HOME
    } elseif ($path.StartsWith('~\') -or $path.StartsWith('~/')) {
        $path = Join-Path $HOME $path.Substring(2)
    }

    return [IO.Path]::GetFullPath($path)
}

function Load-Config {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @{}
    }

    try {
        $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
        if ([string]::IsNullOrWhiteSpace($raw)) {
            return @{}
        }
        return (ConvertTo-Hashtable ($raw | ConvertFrom-Json))
    } catch {
        throw (New-UpdaterError "Could not read config file: $Path`n$($_.Exception.Message)")
    }
}

function Save-Config {
    param([string]$Path, [hashtable]$Data)

    $copy = @{}
    foreach ($key in $Data.Keys) {
        $copy[$key] = $Data[$key]
    }
    $copy['config_version'] = $ConfigVersion

    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $tmpPath = "$Path.tmp"
    try {
        $json = $copy | ConvertTo-Json -Depth 6
        Set-Content -LiteralPath $tmpPath -Value $json -Encoding UTF8
        Move-Item -LiteralPath $tmpPath -Destination $Path -Force
    } catch {
        throw (New-UpdaterError "Could not save config file: $Path`n$($_.Exception.Message)")
    }
}

function Prompt-Default {
    param([string]$Label, [string]$Default)
    $suffix = ''
    if ($Default) {
        $suffix = " [$Default]"
    }
    $value = Read-UiPrompt $Label $suffix
    if ($null -eq $value -or [string]::IsNullOrWhiteSpace($value)) {
        return $Default
    }
    return $value.Trim()
}

function Prompt-YesNo {
    param([string]$Label, [bool]$Default = $true)
    $suffix = 'Y/n'
    if (-not $Default) {
        $suffix = 'y/N'
    }
    $raw = Read-UiPrompt $Label " [$suffix]"
    if ($null -eq $raw) {
        return $Default
    }
    $value = $raw.Trim().ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $Default
    }
    return $value.StartsWith('y')
}

function Test-CanPrompt {
    param([hashtable]$Options)
    return ((-not $Options['NoPrompt']) -and [Environment]::UserInteractive)
}

function Prompt-Secret {
    param([string]$Label)
    try {
        $secure = Read-UiPrompt $Label '' '🔒' -Secret
        $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        try {
            $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
            if ($null -eq $plain) {
                return ''
            }
            return $plain.Trim()
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
        }
    } catch {
        $fallback = Read-UiPrompt $Label '' '🔒'
        if ($null -eq $fallback) {
            return ''
        }
        return $fallback.Trim()
    }
}

function Parse-Repo {
    param([string]$Raw)
    $repo = $Raw.Trim().Trim('/')
    if (-not $repo.Contains('/')) {
        throw (New-UpdaterError 'Repository must use owner/repo format, for example timbergeron/QSS-M.')
    }
    $slashIndex = $repo.IndexOf('/')
    $owner = $repo.Substring(0, $slashIndex).Trim()
    $name = $repo.Substring($slashIndex + 1).Trim()
    if (-not $owner -or -not $name) {
        throw (New-UpdaterError 'Repository must use owner/repo format, for example timbergeron/QSS-M.')
    }
    return @($owner, $name)
}

function Clean-Token {
    param([string]$Token)
    if ([string]::IsNullOrWhiteSpace($Token)) {
        return $null
    }

    $clean = $Token.Trim().Trim('"').Trim("'").Trim()
    if ($clean.ToLowerInvariant().StartsWith('bearer ')) {
        $clean = $clean.Substring(7).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($clean)) {
        return $null
    }
    return $clean
}

function Get-GhCliToken {
    try {
        $output = & gh auth token 2>$null
        if ($LASTEXITCODE -eq 0 -and $output) {
            return (Clean-Token (($output | Select-Object -First 1).ToString()))
        }
    } catch {
    }
    return $null
}

function Resolve-Token {
    param([string]$CliToken, [string]$ConfigToken, [bool]$IncludePrompt = $false)

    $token = Clean-Token $CliToken
    if ($token) {
        return @{ Token = $token; Source = '--token' }
    }

    $token = Clean-Token $env:GITHUB_TOKEN
    if ($token) {
        return @{ Token = $token; Source = 'GITHUB_TOKEN' }
    }

    $token = Clean-Token $ConfigToken
    if ($token) {
        return @{ Token = $token; Source = 'saved config' }
    }

    $token = Clean-Token (Get-GhCliToken)
    if ($token) {
        return @{ Token = $token; Source = 'gh auth token' }
    }

    if ($IncludePrompt) {
        $token = Clean-Token (Prompt-Secret 'GitHub token')
        if ($token) {
            return @{ Token = $token; Source = 'interactive prompt' }
        }
    }

    return @{ Token = $null; Source = 'none' }
}

function Require-TokenForDownload {
    param([string]$Token, [string]$ConfigPath)
    if ($Token) {
        return
    }

    $prog = Get-LauncherName
    $message = @"
GitHub requires authentication to download Actions artifact ZIPs through the REST API.

Do one of these, then rerun:

  Option A, click/run setup again:
    $prog --setup

  Option B, use GitHub CLI:
    gh auth login

  Option C, set a token in this terminal:
    set GITHUB_TOKEN=github_pat_your_token_here
    $prog

Config path: $ConfigPath
For a fine-grained token, use Metadata: Read-only and Actions: Read-only.
"@
    throw (New-UpdaterError $message)
}

function Invoke-InteractiveSetup {
    param([hashtable]$Options, [hashtable]$Config, [string]$ConfigPath)

    if ($Options['NoPrompt'] -or -not [Environment]::UserInteractive) {
        throw (New-UpdaterError 'First-run setup needs an interactive terminal. Rerun without --no-prompt, or pass --dest and --token/--config from a terminal.')
    }

    Write-UiSection 'QSS-M updater setup' '🛠️'
    Write-UiKv 'Config' $ConfigPath '📄'
    Write-Host ''

    $platformDefault = Normalize-Platform (First-NonEmpty $Options['Platform'] (Get-ConfigValue $Config 'platform'))
    $platformKey = Normalize-Platform (Prompt-Default 'Artifact platform (windows/macos/linux)' $platformDefault)
    $preset = $PlatformPresets[$platformKey]

    $ownerDefault = First-NonEmpty $Options['Owner'] (Get-ConfigValue $Config 'owner') $DefaultOwner
    $repoNameDefault = First-NonEmpty $Options['Repo'] (Get-ConfigValue $Config 'repo') $DefaultRepo
    $repoDefault = '{0}/{1}' -f $ownerDefault, $repoNameDefault
    $repoParts = Parse-Repo (Prompt-Default 'GitHub repository' $repoDefault)
    $owner = $repoParts[0]
    $repo = $repoParts[1]

    $destDefault = Expand-PathSafe ([string](First-NonEmpty $Options['Dest'] (Get-ConfigValue $Config 'dest') (Get-DefaultDest)))
    $destDir = Expand-PathSafe (Prompt-Default 'QSS-M folder to update' $destDefault)

    $newConfig = @{}
    foreach ($key in $Config.Keys) {
        $newConfig[$key] = $Config[$key]
    }
    $newConfig['platform'] = $platformKey
    $newConfig['owner'] = $owner
    $newConfig['repo'] = $repo
    $newConfig['dest'] = $destDir

    Write-UiSection 'Selected preset' '📦'
    Write-UiKv 'Platform' ($preset['Label']) '🖥️'
    Write-UiKv 'Workflow' ($preset['WorkflowName']) '⚙️'
    Write-UiKv 'Job' ($preset['JobName']) '✅'
    Write-UiKv 'Artifact' ($preset['ArtifactNameHint']) '📦'
    Write-Host ''

    $existing = Resolve-Token $Options['Token'] (Get-ConfigValue $newConfig 'token') $false
    if ($existing.Token) {
        Write-UiLine "GitHub token already available from: $($existing.Source)" '🔑' 'Green'
        if (Prompt-YesNo 'Keep using that token source' $true) {
            Save-Config $ConfigPath $newConfig
            return $newConfig
        }
    }

    Write-UiLine 'GitHub Actions artifact downloads require a token.' '🔑' 'Yellow'
    Write-UiLine 'Fine-grained token permissions: Metadata Read-only and Actions Read-only.' 'ℹ️' 'DarkGray'
    $token = Clean-Token (Prompt-Secret 'GitHub token, or Enter to use gh/GITHUB_TOKEN later')
    if ($token) {
        $newConfig['token'] = $token
    } elseif ($newConfig.ContainsKey('token')) {
        $newConfig.Remove('token')
    }

    Save-Config $ConfigPath $newConfig
    return $newConfig
}

function New-ApiUrl {
    param([string]$Path, [hashtable]$Params = $null)

    if ($Path.StartsWith('http://') -or $Path.StartsWith('https://')) {
        $base = $Path
    } else {
        $base = "$ApiRoot$Path"
    }

    if ($null -eq $Params -or $Params.Count -eq 0) {
        return $base
    }

    $pairs = @()
    foreach ($key in $Params.Keys) {
        if ($null -ne $Params[$key]) {
            $pairs += ('{0}={1}' -f [Uri]::EscapeDataString([string]$key), [Uri]::EscapeDataString([string]$Params[$key]))
        }
    }

    return ($base + '?' + ($pairs -join '&'))
}

function New-RequestHeaders {
    param([string]$Token = $null)
    $headers = @{
        Accept = 'application/vnd.github+json'
        'User-Agent' = 'qssm-latest-artifact-downloader'
        'X-GitHub-Api-Version' = '2022-11-28'
    }
    if ($Token) {
        $headers['Authorization'] = "Bearer $Token"
    }
    return $headers
}

function Get-WebErrorBody {
    param($Exception)
    try {
        $response = $Exception.Response
        if ($null -eq $response) {
            return ''
        }
        $stream = $response.GetResponseStream()
        if ($null -eq $stream) {
            return ''
        }
        $reader = New-Object IO.StreamReader -ArgumentList $stream
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } catch {
        return ''
    }
}

function Invoke-GitHubJson {
    param([string]$Path, [string]$Token = $null, [hashtable]$Params = $null)
    $url = New-ApiUrl $Path $Params
    try {
        return Invoke-RestMethod -Uri $url -Headers (New-RequestHeaders $Token) -Method Get -TimeoutSec 60
    } catch {
        $body = Get-WebErrorBody $_.Exception
        $status = ''
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            $status = " HTTP $([int]$_.Exception.Response.StatusCode)"
        }
        throw (New-UpdaterError "GitHub API$status for $url`n$body")
    }
}

function Get-PagedList {
    param([string]$Path, [string]$Key, [string]$Token, [hashtable]$Params = @{}, [int]$MaxPages = 5)

    $requestParams = @{}
    foreach ($name in $Params.Keys) {
        $requestParams[$name] = $Params[$name]
    }
    if (-not $requestParams.ContainsKey('per_page')) {
        $requestParams['per_page'] = 100
    }

    $items = @()
    for ($page = 1; $page -le $MaxPages; $page++) {
        $requestParams['page'] = $page
        $data = Invoke-GitHubJson $Path $Token $requestParams
        $pageItems = @($data.$Key)
        $items += $pageItems
        if ($pageItems.Count -lt [int]$requestParams['per_page']) {
            break
        }
    }

    return $items
}

function Get-DefaultBranch {
    param([string]$Owner, [string]$Repo, [string]$Token)
    $data = Invoke-GitHubJson "/repos/$Owner/$Repo" $Token
    if (-not $data.default_branch) {
        throw (New-UpdaterError 'Could not determine the repo default branch.')
    }
    return [string]$data.default_branch
}

function Get-BranchHeadSha {
    param([string]$Owner, [string]$Repo, [string]$Branch, [string]$Token)
    $quoted = [Uri]::EscapeDataString($Branch)
    $data = Invoke-GitHubJson "/repos/$Owner/$Repo/branches/$quoted" $Token
    if (-not $data.commit.sha) {
        throw (New-UpdaterError "Could not determine latest commit SHA for branch '$Branch'.")
    }
    return [string]$data.commit.sha
}

function Find-MatchingRun {
    param(
        [string]$Owner,
        [string]$Repo,
        [string]$Branch,
        [string]$HeadSha,
        [string]$WorkflowName,
        [string]$Token,
        [bool]$AllowOlder
    )

    $runs = Get-PagedList "/repos/$Owner/$Repo/actions/runs" 'workflow_runs' $Token @{
        branch = $Branch
        event = 'push'
        status = 'completed'
    } 5

    $matching = @($runs | Where-Object { ([string]$_.name) -ieq $WorkflowName -and $_.event -eq 'push' })
    if ($matching.Count -eq 0) {
        throw (New-UpdaterError "No completed push runs found for workflow '$WorkflowName' on branch '$Branch'.")
    }

    $latestCommitRuns = @($matching | Where-Object { $_.head_sha -eq $HeadSha })
    $successfulLatest = @($latestCommitRuns | Where-Object { $_.conclusion -eq 'success' })
    if ($successfulLatest.Count -gt 0) {
        return $successfulLatest[0]
    }

    if (-not $AllowOlder) {
        $statuses = @($latestCommitRuns | Select-Object -First 5 | ForEach-Object { "run #$($_.run_number) conclusion=$($_.conclusion)" }) -join ', '
        if (-not $statuses) {
            $statuses = 'no completed run found for the latest commit yet'
        }
        throw (New-UpdaterError "The latest commit does not have a successful completed run for this workflow yet.`nLatest commit: $HeadSha`nFound: $statuses`n`nRun again after GitHub Actions finishes, or pass --allow-older to use the newest older successful artifact.")
    }

    $olderSuccesses = @($matching | Where-Object { $_.conclusion -eq 'success' })
    if ($olderSuccesses.Count -eq 0) {
        throw (New-UpdaterError "No successful completed runs found for workflow '$WorkflowName'.")
    }
    return $olderSuccesses[0]
}

function Confirm-JobSuccess {
    param([string]$Owner, [string]$Repo, [int64]$RunId, [string]$JobName, [string]$Token)

    $jobs = Get-PagedList "/repos/$Owner/$Repo/actions/runs/$RunId/jobs" 'jobs' $Token @{} 3
    $exact = @($jobs | Where-Object { ([string]$_.name) -ieq $JobName })
    $partial = @($jobs | Where-Object { ([string]$_.name).ToLowerInvariant().Contains($JobName.ToLowerInvariant()) })
    $candidates = $exact
    if ($candidates.Count -eq 0) {
        $candidates = $partial
    }

    if ($candidates.Count -eq 0) {
        $names = @($jobs | ForEach-Object { "  - $($_.name)" }) -join "`n"
        throw (New-UpdaterError "Could not find job '$JobName' in this workflow run.`nJobs found:`n$names")
    }

    $successful = @($candidates | Where-Object { $_.conclusion -eq 'success' })
    if ($successful.Count -eq 0) {
        $details = @($candidates | ForEach-Object { "  - $($_.name): status=$($_.status) conclusion=$($_.conclusion)" }) -join "`n"
        throw (New-UpdaterError "Matching job was not successful:`n$details")
    }

    return $successful[0]
}

function Get-ArtifactScore {
    param($Artifact, [string]$JobName, [string]$NameHint)

    $name = [string]$Artifact.name
    $nameLc = $name.ToLowerInvariant()
    $jobLc = $JobName.ToLowerInvariant()
    $score = 0

    if ($Artifact.expired) { $score -= 10000 }
    if ($nameLc.Contains('log')) { $score -= 500 }

    if ($NameHint) {
        $hintLc = $NameHint.ToLowerInvariant()
        if ($nameLc -eq $hintLc) { $score += 1000 }
        elseif ($nameLc.Contains($hintLc)) { $score += 500 }
    }

    if ($nameLc -eq $jobLc) { $score += 300 }
    elseif ($nameLc.Contains($jobLc)) { $score += 200 }

    if ($nameLc.Contains('qss-m') -or $nameLc.Contains('qssm')) { $score += 50 }
    if ($nameLc.Contains('win') -or $nameLc.Contains('mingw')) { $score += 25 }
    if ($nameLc.Contains('mac') -or $nameLc.Contains('macos')) { $score += 25 }
    if ($nameLc.Contains('linux') -or $nameLc.Contains('l64')) { $score += 25 }

    return $score
}

function Get-Artifacts {
    param(
        [string]$Owner,
        [string]$Repo,
        [int64]$RunId,
        [string]$Token,
        [string]$JobName,
        [string]$NameHint,
        [bool]$IncludeLogs
    )

    $artifacts = @(Get-PagedList "/repos/$Owner/$Repo/actions/runs/$RunId/artifacts" 'artifacts' $Token @{} 3)
    $artifacts = @($artifacts | Where-Object { -not $_.expired })

    if (-not $IncludeLogs) {
        $artifacts = @($artifacts | Where-Object { -not ([string]$_.name).ToLowerInvariant().Contains('log') })
    }

    if ($NameHint) {
        $hintLc = $NameHint.ToLowerInvariant()
        $hinted = @($artifacts | Where-Object { ([string]$_.name).ToLowerInvariant().Contains($hintLc) })
        if ($hinted.Count -gt 0) {
            $artifacts = $hinted
        }
    } else {
        $jobLc = $JobName.ToLowerInvariant()
        $jobNamed = @($artifacts | Where-Object { ([string]$_.name).ToLowerInvariant().Contains($jobLc) })
        if ($jobNamed.Count -gt 0) {
            $artifacts = $jobNamed
        }
    }

    if ($artifacts.Count -eq 0) {
        if ($NameHint) {
            $hintMsg = " matching artifact hint '$NameHint'"
        } else {
            $hintMsg = " matching job/artifact '$JobName'"
        }
        throw (New-UpdaterError "No non-expired non-log artifacts found for this run$hintMsg.")
    }

    $sorted = @($artifacts | Sort-Object `
        @{ Expression = { Get-ArtifactScore $_ $JobName $NameHint }; Descending = $true }, `
        @{ Expression = { if ($null -ne $_.size_in_bytes) { [int64]$_.size_in_bytes } else { 0 } }; Descending = $true }, `
        @{ Expression = { [string]$_.name }; Descending = $true })

    return @($sorted | Select-Object -First 1)
}

function Copy-ResponseToFile {
    param($Response, [string]$OutPath)
    $parent = Split-Path -Parent $OutPath
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $contentLength = [int64]$Response.ContentLength
    $contentType = [string]$Response.ContentType
    $statusCode = 0
    try {
        if ($Response.StatusCode) {
            $statusCode = [int]$Response.StatusCode
        }
    } catch {
    }

    $responseUri = $null
    try {
        if ($Response.ResponseUri) {
            $responseUri = $Response.ResponseUri.AbsoluteUri
        }
    } catch {
    }

    $inputStream = $Response.GetResponseStream()
    if ($null -eq $inputStream) {
        $Response.Close()
        throw (New-UpdaterError 'Download response did not include a body stream.')
    }
    $outputStream = [IO.File]::Create($OutPath)
    $bytesWritten = [int64]0
    try {
        $buffer = New-Object byte[] 131072
        while ($true) {
            $read = $inputStream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) {
                break
            }
            $outputStream.Write($buffer, 0, $read)
            $bytesWritten += [int64]$read
        }
    } finally {
        $outputStream.Dispose()
        if ($inputStream) { $inputStream.Dispose() }
        $Response.Close()
    }

    if ($contentLength -ge 0 -and $bytesWritten -ne $contentLength) {
        throw (New-UpdaterError "Download ended early. Expected $contentLength bytes, wrote $bytesWritten bytes.")
    }

    return @{
        BytesWritten = $bytesWritten
        ContentLength = $contentLength
        ContentType = $contentType
        StatusCode = $statusCode
        ResponseUri = $responseUri
    }
}

function Download-FinalArtifactUrl {
    param([string]$FinalUrl, [string]$OutPath)
    $req = [Net.HttpWebRequest]::Create($FinalUrl)
    $req.Method = 'GET'
    $req.Accept = 'application/octet-stream'
    $req.UserAgent = 'qssm-latest-artifact-downloader'
    $req.Timeout = 120000
    $req.ReadWriteTimeout = 120000
    $req.AllowAutoRedirect = $true
    try {
        $resp = $req.GetResponse()
        return (Copy-ResponseToFile $resp $OutPath)
    } catch [Net.WebException] {
        $resp = $_.Exception.Response
        $body = Get-WebErrorBody $_.Exception
        if ($resp) {
            throw (New-UpdaterError "Artifact storage download HTTP $([int]$resp.StatusCode)`n$body")
        }
        throw (New-UpdaterError "Network error downloading artifact storage: $($_.Exception.Message)")
    }
}

function Get-FilePreview {
    param([string]$Path, [int]$MaxBytes = 600)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }

    $stream = [IO.File]::OpenRead($Path)
    try {
        $count = [int][Math]::Min([int64]$MaxBytes, [int64]$stream.Length)
        if ($count -le 0) {
            return ''
        }
        $bytes = New-Object byte[] $count
        $read = $stream.Read($bytes, 0, $count)
        if ($read -le 0) {
            return ''
        }
        $text = [Text.Encoding]::UTF8.GetString($bytes, 0, $read)
        $text = $text -replace '[^\u0009\u000A\u000D\u0020-\u007E]', '.'
        return $text.Trim()
    } finally {
        $stream.Dispose()
    }
}

function Test-ZipReadable {
    param([string]$ZipPath)

    if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) {
        return 'Downloaded file was not created.'
    }

    $item = Get-Item -LiteralPath $ZipPath
    if ($item.Length -lt 22) {
        return "Downloaded file is too small to be a ZIP archive ($($item.Length) bytes)."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = $null
    try {
        $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
        $null = $archive.Entries.Count
        return $null
    } catch {
        return $_.Exception.Message
    } finally {
        if ($archive) {
            $archive.Dispose()
        }
    }
}

function Test-ArtifactDigest {
    param([string]$ZipPath, [string]$ExpectedDigest)

    if ([string]::IsNullOrWhiteSpace($ExpectedDigest)) {
        return $null
    }

    if ($ExpectedDigest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
        return $null
    }

    $expected = $Matches[1].ToLowerInvariant()
    try {
        $actual = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } catch {
        return "Could not compute downloaded artifact SHA-256: $($_.Exception.Message)"
    }

    if ($actual -ne $expected) {
        return "Downloaded artifact SHA-256 mismatch. Expected sha256:$expected but got sha256:$actual."
    }

    return $null
}

function New-InvalidDownloadMessage {
    param(
        [string]$ArtifactName,
        [string]$ZipPath,
        [string]$Problem,
        [hashtable]$DownloadInfo = $null,
        [int64]$ExpectedSize = 0
    )

    $details = @()
    if ($ArtifactName) {
        $details += "Artifact: $ArtifactName"
    }
    if (Test-Path -LiteralPath $ZipPath -PathType Leaf) {
        $details += ("Downloaded bytes: {0:N0}" -f (Get-Item -LiteralPath $ZipPath).Length)
    }
    if ($ExpectedSize -gt 0) {
        $details += ("GitHub metadata size: {0:N0}" -f $ExpectedSize)
    }
    if ($DownloadInfo) {
        if ($DownloadInfo['StatusCode']) {
            $details += "HTTP status: $($DownloadInfo['StatusCode'])"
        }
        if ($DownloadInfo['ContentType']) {
            $details += "Content-Type: $($DownloadInfo['ContentType'])"
        }
        if ($DownloadInfo['ContentLength'] -ge 0) {
            $details += ("Content-Length: {0:N0}" -f [int64]$DownloadInfo['ContentLength'])
        }
    }

    $preview = Get-FilePreview $ZipPath
    if ($preview) {
        $details += "Response starts with:`n$preview"
    }

    return "Downloaded artifact is not valid.`n$Problem`n`n$($details -join "`n")"
}

function Download-RedirectedArtifact {
    param([string]$Url, [string]$OutPath, $Response)

    $location = $Response.Headers['Location']
    $Response.Close()

    if (-not $location) {
        throw (New-UpdaterError 'GitHub artifact download redirected without a Location header.')
    }

    $baseUri = New-Object Uri -ArgumentList $Url
    $finalUri = New-Object Uri -ArgumentList $baseUri, $location
    $final = $finalUri.AbsoluteUri
    return (Download-FinalArtifactUrl $final $OutPath)
}

function Download-GitHubArtifactOnce {
    param([string]$Url, [string]$OutPath, [string]$Token)

    $req = [Net.HttpWebRequest]::Create($Url)
    $req.Method = 'GET'
    $req.Accept = 'application/vnd.github+json'
    $req.UserAgent = 'qssm-latest-artifact-downloader'
    $req.Headers.Add('X-GitHub-Api-Version', '2022-11-28')
    $req.Headers.Add('Authorization', "Bearer $Token")
    $req.Timeout = 60000
    $req.AllowAutoRedirect = $false

    try {
        $resp = $req.GetResponse()
        if (@(301, 302, 303, 307, 308) -contains [int]$resp.StatusCode) {
            return (Download-RedirectedArtifact $Url $OutPath $resp)
        }
        return (Copy-ResponseToFile $resp $OutPath)
    } catch [Net.WebException] {
        $resp = $_.Exception.Response
        if ($resp -and (@(301, 302, 303, 307, 308) -contains [int]$resp.StatusCode)) {
            return (Download-RedirectedArtifact $Url $OutPath $resp)
        }

        $body = Get-WebErrorBody $_.Exception
        if ($resp -and (@(401, 403) -contains [int]$resp.StatusCode)) {
            throw (New-UpdaterError "GitHub artifact download HTTP $([int]$resp.StatusCode).`n`nYour token is missing, expired, malformed, or does not have Actions read access for this repo.`nFor a fine-grained token, use Metadata: Read-only and Actions: Read-only.`n`nResponse:`n$body")
        }
        if ($resp) {
            throw (New-UpdaterError "GitHub artifact download HTTP $([int]$resp.StatusCode)`n$body")
        }
        throw (New-UpdaterError "Network error downloading artifact: $($_.Exception.Message)")
    }
}

function Download-GitHubArtifact {
    param(
        [string]$Url,
        [string]$OutPath,
        [string]$Token,
        [string]$ConfigPath,
        [string]$ArtifactName = $null,
        [int64]$ExpectedSize = 0,
        [string]$ExpectedDigest = $null
    )

    Require-TokenForDownload $Token $ConfigPath

    $lastError = $null
    $attempts = 3
    for ($attempt = 1; $attempt -le $attempts; $attempt++) {
        if (Test-Path -LiteralPath $OutPath -PathType Leaf) {
            Remove-Item -LiteralPath $OutPath -Force -ErrorAction SilentlyContinue
        }

        $downloadInfo = $null
        try {
            $downloadInfo = Download-GitHubArtifactOnce $Url $OutPath $Token
            $zipProblem = Test-ZipReadable $OutPath
            if ($zipProblem) {
                throw (New-UpdaterError (New-InvalidDownloadMessage $ArtifactName $OutPath $zipProblem $downloadInfo $ExpectedSize))
            }

            $digestProblem = Test-ArtifactDigest $OutPath $ExpectedDigest
            if ($digestProblem) {
                throw (New-UpdaterError (New-InvalidDownloadMessage $ArtifactName $OutPath $digestProblem $downloadInfo $ExpectedSize))
            }

            return
        } catch {
            $lastError = $_.Exception
            $message = [string]$lastError.Message
            $retryable = (
                $message.StartsWith('Downloaded artifact is not valid.') -or
                $message.StartsWith('Download ended early.') -or
                $message.StartsWith('Network error downloading artifact') -or
                ($message -match '^GitHub artifact download HTTP 5\d\d') -or
                ($message -match '^Artifact storage download HTTP 5\d\d')
            )
            if (-not $retryable) {
                throw
            }
            if ($attempt -lt $attempts) {
                Write-UiLine "Artifact download failed validation or transfer checks; retrying ($($attempt + 1)/$attempts)..." '⚠️' 'Yellow'
                Start-Sleep -Seconds 2
            }
        }
    }

    throw $lastError
}

function Test-PathInside {
    param([string]$Root, [string]$Target)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
    $targetFull = [IO.Path]::GetFullPath($Target)
    return $targetFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Expand-ZipSafe {
    param([string]$ZipPath, [string]$Dest)

    New-Item -ItemType Directory -Path $Dest -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            $target = Join-Path $Dest $entry.FullName
            if (-not (Test-PathInside $Dest $target)) {
                throw (New-UpdaterError "Blocked unsafe ZIP entry: $($entry.FullName)")
            }
        }

        foreach ($entry in $archive.Entries) {
            $target = Join-Path $Dest $entry.FullName
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\') -or [string]::IsNullOrEmpty($entry.Name)) {
                New-Item -ItemType Directory -Path $target -Force | Out-Null
                continue
            }

            $parent = Split-Path -Parent $target
            if ($parent) {
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
            }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $archive.Dispose()
    }
}

function Expand-NestedZips {
    param([string]$Root, [int]$MaxDepth = 3)

    $seen = @{}
    for ($depth = 0; $depth -lt $MaxDepth; $depth++) {
        $nested = @(Get-ChildItem -LiteralPath $Root -Recurse -Force -Filter '*.zip' -ErrorAction SilentlyContinue | Where-Object { -not $_.PSIsContainer })
        $pending = @($nested | Where-Object { -not $seen.ContainsKey($_.FullName.ToLowerInvariant()) })
        if ($pending.Count -eq 0) {
            return
        }

        foreach ($zip in $pending) {
            $seen[$zip.FullName.ToLowerInvariant()] = $true
            $target = Join-Path $zip.DirectoryName ([IO.Path]::GetFileNameWithoutExtension($zip.Name))
            if (Test-Path -LiteralPath $target) {
                for ($i = 2; $i -lt 1000; $i++) {
                    $candidate = Join-Path $zip.DirectoryName "$([IO.Path]::GetFileNameWithoutExtension($zip.Name))_$i"
                    if (-not (Test-Path -LiteralPath $candidate)) {
                        $target = $candidate
                        break
                    }
                }
            }

            try {
                Expand-ZipSafe $zip.FullName $target
            } catch [IO.InvalidDataException] {
            }
        }
    }
}

function Get-LaunchablePriority {
    param($PathItem)
    $name = $PathItem.Name.ToLowerInvariant()
    $priority = 0
    if (@('qss-m.exe', 'qss-m', 'qss-m.app') -contains $name) { $priority += 100 }
    if ($name.Contains('qss-m')) { $priority += 40 }
    if ($name.Contains('qssm')) { $priority += 30 }
    if ($name.Contains('qss')) { $priority += 20 }
    if ($name.Contains('win') -or $name.Contains('w64') -or $name.Contains('x64') -or $name.Contains('64')) { $priority += 10 }
    return $priority
}

function Find-Launchables {
    param([string]$Root, [string]$LaunchGlob)
    $launchableMatches = @(Get-ChildItem -LiteralPath $Root -Recurse -Force -ErrorAction SilentlyContinue | Where-Object {
        if ($_.PSIsContainer -and $_.Extension -ieq '.app') {
            $_.Name -like $LaunchGlob
        } elseif (-not $_.PSIsContainer) {
            $_.Name -like $LaunchGlob
        } else {
            $false
        }
    })

    return @($launchableMatches | Sort-Object `
        @{ Expression = { Get-LaunchablePriority $_ }; Descending = $true }, `
        @{ Expression = { if ($_.PSIsContainer) { 0 } else { [int64]$_.Length } }; Descending = $true }, `
        @{ Expression = { $_.Name.ToLowerInvariant() }; Descending = $true })
}

function Get-SingleChildDir {
    param([string]$Path)
    $current = Get-Item -LiteralPath $Path
    for ($i = 0; $i -lt 5; $i++) {
        $children = @(Get-ChildItem -LiteralPath $current.FullName -Force | Where-Object { $_.Name -ne '__MACOSX' })
        $dirs = @($children | Where-Object { $_.PSIsContainer })
        $files = @($children | Where-Object { -not $_.PSIsContainer })
        if ($dirs.Count -eq 1 -and $files.Count -eq 0) {
            $current = $dirs[0]
        } else {
            break
        }
    }
    return $current
}

function Choose-PayloadRoot {
    param([string]$ExtractRoot, [string]$LaunchGlob, [string]$PayloadRoot = $null)

    if ($PayloadRoot) {
        $expandedRaw = [Environment]::ExpandEnvironmentVariables($PayloadRoot)
        if ([IO.Path]::IsPathRooted($expandedRaw)) {
            $candidate = Expand-PathSafe $expandedRaw
        } else {
            $candidate = [IO.Path]::GetFullPath((Join-Path $ExtractRoot $expandedRaw))
        }
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            throw (New-UpdaterError "Requested payload root does not exist or is not a directory: $candidate")
        }
        return @{ Root = $candidate; Launchables = @(Find-Launchables $candidate $LaunchGlob) }
    }

    $launchables = @(Find-Launchables $ExtractRoot $LaunchGlob)
    if ($launchables.Count -gt 0) {
        return @{ Root = $launchables[0].DirectoryName; Launchables = $launchables }
    }

    $topDirs = @(Get-ChildItem -LiteralPath $ExtractRoot -Force | Where-Object { $_.PSIsContainer -and $_.Name -ne '__MACOSX' })
    $topFiles = @(Get-ChildItem -LiteralPath $ExtractRoot -Force | Where-Object { -not $_.PSIsContainer -and $_.Extension -ine '.zip' })
    if ($topDirs.Count -eq 1 -and $topFiles.Count -eq 0) {
        $candidate = Get-SingleChildDir $topDirs[0].FullName
        return @{ Root = $candidate.FullName; Launchables = @(Find-Launchables $candidate.FullName $LaunchGlob) }
    }

    throw (New-UpdaterError "Could not find package contents. No files matching '$LaunchGlob' were found. Use --payload-root to point at the extracted package folder.")
}

function Copy-DirectoryContents {
    param([string]$Source, [string]$Dest)
    New-Item -ItemType Directory -Path $Dest -Force | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $Source -Force) {
        $target = Join-Path $Dest $child.Name
        if ($child.PSIsContainer) {
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                Remove-Item -LiteralPath $target -Force
            }
            Copy-DirectoryContents $child.FullName $target
        } else {
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force
            }
            Copy-Item -LiteralPath $child.FullName -Destination $target -Force
        }
    }
}

function Copy-DirectoryTree {
    param([string]$Source, [string]$Dest)
    if (Test-Path -LiteralPath $Dest) {
        Remove-Item -LiteralPath $Dest -Recurse -Force
    }
    Copy-Item -LiteralPath $Source -Destination $Dest -Recurse -Force
}

function Copy-PayloadToStage {
    param([string]$PayloadRoot, [string]$StageDir)
    if (Test-Path -LiteralPath $StageDir) {
        Remove-Item -LiteralPath $StageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $PayloadRoot -Force) {
        if ($child.Name -eq '__MACOSX') {
            continue
        }
        $target = Join-Path $StageDir $child.Name
        if ($child.PSIsContainer) {
            Copy-DirectoryTree $child.FullName $target
        } else {
            Copy-Item -LiteralPath $child.FullName -Destination $target -Force
        }
    }
}

function Normalize-DirPath {
    param([string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    $trimmed = $full.TrimEnd([char[]]@('\', '/'))
    $rootTrimmed = $root.TrimEnd([char[]]@('\', '/'))
    if ($trimmed.Length -lt $rootTrimmed.Length) {
        return $rootTrimmed
    }
    return $trimmed
}

function Test-DangerousDestination {
    param([string]$DestDir)
    $resolved = Normalize-DirPath $DestDir
    $dangerousDirs = @()

    if (-not [string]::IsNullOrWhiteSpace($HOME)) {
        $dangerousDirs += Normalize-DirPath $HOME
    }

    $desktopRaw = [Environment]::GetFolderPath('Desktop')
    if (-not [string]::IsNullOrWhiteSpace($desktopRaw)) {
        $dangerousDirs += Normalize-DirPath $desktopRaw
    }

    $rootRaw = [IO.Path]::GetPathRoot($resolved)
    if (-not [string]::IsNullOrWhiteSpace($rootRaw)) {
        $dangerousDirs += Normalize-DirPath $rootRaw
    }

    foreach ($dangerousDir in $dangerousDirs) {
        if ($resolved.Equals($dangerousDir, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Get-UniqueBackupPath {
    param([string]$DestDir)
    $parent = Split-Path -Parent $DestDir
    $leaf = Split-Path -Leaf $DestDir
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $base = Join-Path $parent "$leaf.backup-$stamp"
    if (-not (Test-Path -LiteralPath $base)) {
        return $base
    }
    for ($i = 2; $i -lt 1000; $i++) {
        $candidate = "$base-$i"
        if (-not (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    throw (New-UpdaterError "Could not find a unique backup name for $DestDir.")
}

function Get-UniqueBackupZipPath {
    param([string]$DestDir)
    $parent = $DestDir
    $leaf = Split-Path -Leaf $DestDir
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $base = Join-Path $parent "$leaf.backup-$stamp.zip"
    if (-not (Test-Path -LiteralPath $base)) {
        return $base
    }
    for ($i = 2; $i -lt 1000; $i++) {
        $candidate = Join-Path $parent "$leaf.backup-$stamp-$i.zip"
        if (-not (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    throw (New-UpdaterError "Could not find a unique backup ZIP name for $DestDir.")
}

function Copy-ExistingPathToBackupRoot {
    param([string]$ExistingPath, [string]$BackupRoot, [string]$RelativePath)

    if (-not (Test-Path -LiteralPath $ExistingPath)) {
        return 0
    }

    $backupPath = Join-Path $BackupRoot $RelativePath
    if (Test-Path -LiteralPath $ExistingPath -PathType Leaf) {
        $parent = Split-Path -Parent $backupPath
        if ($parent) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item -LiteralPath $ExistingPath -Destination $backupPath -Force
        return 1
    }

    if (Test-Path -LiteralPath $ExistingPath -PathType Container) {
        Copy-DirectoryTree $ExistingPath $backupPath
        $counts = Count-PayloadEntries $backupPath
        return [int]$counts.Files
    }

    return 0
}

function Add-MatchingBackupItems {
    param([string]$SourcePath, [string]$DestPath, [string]$BackupRoot, [string]$RelativePath)

    $sourceItem = Get-Item -LiteralPath $SourcePath
    if (-not $sourceItem.PSIsContainer) {
        return (Copy-ExistingPathToBackupRoot $DestPath $BackupRoot $RelativePath)
    }

    if (Test-Path -LiteralPath $DestPath -PathType Leaf) {
        return (Copy-ExistingPathToBackupRoot $DestPath $BackupRoot $RelativePath)
    }

    if ($sourceItem.Extension -ieq '.app') {
        return (Copy-ExistingPathToBackupRoot $DestPath $BackupRoot $RelativePath)
    }

    if (-not (Test-Path -LiteralPath $DestPath -PathType Container)) {
        return 0
    }

    $backedUp = 0
    foreach ($child in Get-ChildItem -LiteralPath $SourcePath -Force) {
        $childRelative = Join-Path $RelativePath $child.Name
        $childDest = Join-Path $DestPath $child.Name
        $backedUp += Add-MatchingBackupItems $child.FullName $childDest $BackupRoot $childRelative
    }
    return $backedUp
}

function New-SelectiveBackupZip {
    param([string]$StageDir, [string]$DestDir)

    if (-not (Test-Path -LiteralPath $DestDir)) {
        return $null
    }

    $backupPath = Get-UniqueBackupZipPath $DestDir
    $backupRoot = Join-Path ([IO.Path]::GetTempPath()) ('qssm_backup_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

    try {
        $backedUp = 0
        foreach ($child in Get-ChildItem -LiteralPath $StageDir -Force) {
            $destPath = Join-Path $DestDir $child.Name
            $backedUp += Add-MatchingBackupItems $child.FullName $destPath $backupRoot $child.Name
        }

        if ($backedUp -le 0) {
            return $null
        }

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        try {
            [IO.Compression.ZipFile]::CreateFromDirectory($backupRoot, $backupPath, [IO.Compression.CompressionLevel]::Optimal, $false)
        } catch {
            Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
            throw
        }
        return @{ Path = $backupPath; Files = $backedUp }
    } finally {
        Remove-Item -LiteralPath $backupRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Restore-SelectiveBackupZip {
    param([string]$BackupZip, [string]$DestDir)

    if (-not (Test-Path -LiteralPath $BackupZip -PathType Leaf)) {
        return
    }

    $restoreRoot = Join-Path ([IO.Path]::GetTempPath()) ('qssm_restore_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $restoreRoot -Force | Out-Null
    try {
        Expand-ZipSafe $BackupZip $restoreRoot
        Copy-StageContents $restoreRoot $DestDir
    } finally {
        Remove-Item -LiteralPath $restoreRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Replace-Directory {
    param([string]$StageDir, [string]$DestDir, [bool]$MakeBackup)

    $dest = Expand-PathSafe $DestDir
    if (Test-DangerousDestination $dest) {
        throw (New-UpdaterError "Refusing to replace overly broad destination: $dest")
    }
    if (Test-Path -LiteralPath $dest -PathType Leaf) {
        throw (New-UpdaterError "Destination exists but is not a directory: $dest")
    }

    $parent = Split-Path -Parent $dest
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $backupPath = $null
    if (Test-Path -LiteralPath $dest) {
        if ($MakeBackup) {
            $backupPath = Get-UniqueBackupPath $dest
            Move-Item -LiteralPath $dest -Destination $backupPath
        } else {
            Remove-Item -LiteralPath $dest -Recurse -Force
        }
    }

    try {
        Move-Item -LiteralPath $StageDir -Destination $dest
    } catch {
        if ($backupPath -and (Test-Path -LiteralPath $backupPath) -and -not (Test-Path -LiteralPath $dest)) {
            Move-Item -LiteralPath $backupPath -Destination $dest
        }
        throw
    }

    return $backupPath
}

function Copy-StageContents {
    param([string]$StageDir, [string]$DestDir)
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $StageDir -Force) {
        $target = Join-Path $DestDir $child.Name
        if ($child.PSIsContainer) {
            if ((Test-Path -LiteralPath $target) -and ($child.Extension -ieq '.app' -or (Test-Path -LiteralPath $target -PathType Leaf))) {
                Remove-Item -LiteralPath $target -Recurse -Force
            }
            if ($child.Extension -ieq '.app' -or -not (Test-Path -LiteralPath $target)) {
                Copy-DirectoryTree $child.FullName $target
            } else {
                Copy-DirectoryContents $child.FullName $target
            }
        } else {
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force
            }
            Copy-Item -LiteralPath $child.FullName -Destination $target -Force
        }
    }
}

function Merge-Directory {
    param([string]$StageDir, [string]$DestDir, [bool]$MakeBackup)

    $dest = Expand-PathSafe $DestDir
    if (Test-DangerousDestination $dest) {
        throw (New-UpdaterError "Refusing to update overly broad destination: $dest")
    }
    if (Test-Path -LiteralPath $dest -PathType Leaf) {
        throw (New-UpdaterError "Destination exists but is not a directory: $dest")
    }

    $parent = Split-Path -Parent $dest
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $backupInfo = $null
    if ((Test-Path -LiteralPath $dest) -and $MakeBackup) {
        Write-UiLine 'Backing up existing files that this release will overwrite...' '💾' 'Yellow'
        $backupInfo = New-SelectiveBackupZip $StageDir $dest
        if ($backupInfo) {
            Write-UiLine ("Backed up {0:N0} existing file(s)." -f [int]$backupInfo.Files) '✅' 'Green'
        } else {
            Write-UiLine 'No existing release files needed backup.' 'ℹ️' 'DarkGray'
        }
    }

    try {
        Write-UiLine 'Copying release files...' '📁' 'Cyan'
        Copy-StageContents $StageDir $dest
    } catch {
        if ($backupInfo -and (Test-Path -LiteralPath $backupInfo.Path -PathType Leaf)) {
            try {
                Write-UiLine 'Update failed; restoring backed-up files...' '⚠️' 'Yellow'
                Restore-SelectiveBackupZip $backupInfo.Path $dest
            } catch {
                Write-UiLine "Could not restore from backup ZIP: $($_.Exception.Message)" '❌' 'Red'
            }
        }
        throw
    }

    Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($backupInfo) {
        return [string]$backupInfo.Path
    }
    return $null
}

function Deploy-Stage {
    param([string]$StageDir, [string]$DestDir, [bool]$MakeBackup, [bool]$Mirror)
    if ($Mirror) {
        return (Replace-Directory $StageDir $DestDir $MakeBackup)
    }
    return (Merge-Directory $StageDir $DestDir $MakeBackup)
}

function Count-PayloadEntries {
    param([string]$Path)
    $files = 0
    $dirs = 0
    foreach ($child in Get-ChildItem -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue) {
        if ($child.PSIsContainer) {
            $dirs++
        } else {
            $files++
        }
    }
    return @{ Files = $files; Dirs = $dirs }
}

function Get-EffectiveOptions {
    param([hashtable]$Options, [hashtable]$Config)
    $platformKey = Normalize-Platform (First-NonEmpty $Options['Platform'] (Get-ConfigValue $Config 'platform') (Detect-Platform))
    $preset = $PlatformPresets[$platformKey]
    $launchGlob = First-NonEmpty $Options['LaunchGlob'] $Options['ExeGlob'] (Get-ConfigValue $Config 'launch_glob') $preset['LaunchGlob']

    $artifactNameHint = $Options['ArtifactNameHint']
    if ($null -eq $artifactNameHint) {
        $artifactNameHint = First-NonEmpty (Get-ConfigValue $Config 'artifact_name_hint') $preset['ArtifactNameHint']
    }

    return @{
        Platform = $platformKey
        Owner = First-NonEmpty $Options['Owner'] (Get-ConfigValue $Config 'owner') $DefaultOwner
        Repo = First-NonEmpty $Options['Repo'] (Get-ConfigValue $Config 'repo') $DefaultRepo
        WorkflowName = First-NonEmpty $Options['WorkflowName'] (Get-ConfigValue $Config 'workflow_name') $preset['WorkflowName']
        JobName = First-NonEmpty $Options['JobName'] (Get-ConfigValue $Config 'job_name') $preset['JobName']
        ArtifactNameHint = $artifactNameHint
        Dest = Expand-PathSafe ([string](First-NonEmpty $Options['Dest'] (Get-ConfigValue $Config 'dest') (Get-DefaultDest)))
        LaunchGlob = $launchGlob
        PayloadRoot = First-NonEmpty $Options['PayloadRoot'] (Get-ConfigValue $Config 'payload_root')
    }
}

$Options = $null
$tempRoot = $null

try {
    $Options = Parse-Args $args
    $canPrompt = Test-CanPrompt $Options

    if ($Options['Setup'] -and $Options['NoConfig']) {
        throw (New-UpdaterError '--setup cannot be combined with --no-config.')
    }

    if ($Options['Config']) {
        $configPath = Expand-PathSafe $Options['Config']
    } else {
        $configPath = Get-DefaultConfigPath
    }

    $config = @{}
    if (-not $Options['NoConfig']) {
        $config = Load-Config $configPath
    }

    if ($Options['Setup'] -or ((-not $Options['NoConfig']) -and -not (Get-ConfigValue $config 'dest'))) {
        $config = Invoke-InteractiveSetup $Options $config $configPath
    }

    $opts = Get-EffectiveOptions $Options $config
    $tokenInfo = Resolve-Token $Options['Token'] (Get-ConfigValue $config 'token') $canPrompt

    if ($tokenInfo['Source'] -eq 'interactive prompt' -and $tokenInfo['Token'] -and -not $Options['NoConfig']) {
        if (Prompt-YesNo 'Save this token for future runs' $true) {
            $config['token'] = $tokenInfo['Token']
            Save-Config $configPath $config
        }
    }

    $owner = [string]$opts['Owner']
    $repo = [string]$opts['Repo']
    $destDir = [string]$opts['Dest']

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('qssm_artifact_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

    Write-UiSection 'QSS-M update' '🚀'
    Write-UiKv 'Repo' "$owner/$repo" '🔗'
    Write-UiKv 'Platform' "$($opts['Platform']) ($($PlatformPresets[$opts['Platform']]['Label']))" '🖥️'
    Write-UiKv 'Workflow' ($opts['WorkflowName']) '⚙️'
    Write-UiKv 'Required job' ($opts['JobName']) '✅'
    Write-UiKv 'Event' 'push' '📌'
    Write-UiKv 'Auth' ($tokenInfo['Source']) '🔑'
    if (-not $Options['NoConfig']) {
        Write-UiKv 'Config' $configPath '📄'
    }

    $branch = $Options['Branch']
    if (-not $branch) {
        $branch = Get-DefaultBranch $owner $repo $tokenInfo['Token']
    }
    $headSha = Get-BranchHeadSha $owner $repo $branch $tokenInfo['Token']

    Write-UiSection 'GitHub Actions' '🔎'
    Write-UiKv 'Branch' $branch '🌿'
    Write-UiKv 'Latest commit' ($headSha.Substring(0, [Math]::Min(12, $headSha.Length))) '🧬'

    $run = Find-MatchingRun $owner $repo $branch $headSha $opts['WorkflowName'] $tokenInfo['Token'] $Options['AllowOlder']
    $runId = [int64]$run.id
    $runSha = [string]$run.head_sha

    Write-UiKv 'Using run' "#$($run.run_number) id=$runId sha=$($runSha.Substring(0, [Math]::Min(12, $runSha.Length)))" '🏃'
    if ($run.html_url) {
        Write-UiKv 'Run URL' ($run.html_url) '🔗'
    }

    $job = Confirm-JobSuccess $owner $repo $runId $opts['JobName'] $tokenInfo['Token']
    Write-UiLine "Confirmed job success: $($job.name)" '✅' 'Green'

    $artifacts = @(Get-Artifacts $owner $repo $runId $tokenInfo['Token'] $opts['JobName'] $opts['ArtifactNameHint'] $Options['IncludeLogs'])
    Write-UiSection 'Artifact' '📦'
    Write-UiLine 'Artifact selected:' '✅' 'Green'
    foreach ($artifact in $artifacts) {
        $artifactSize = 0
        if ($null -ne $artifact.size_in_bytes) {
            $artifactSize = [int64]$artifact.size_in_bytes
        }
        Write-UiLine ("{0} ({1:N0} bytes)" -f $artifact.name, $artifactSize) '•'
    }

    $extractRoot = Join-Path $tempRoot 'extracted'
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null

    foreach ($artifact in $artifacts) {
        $artifactId = $artifact.id
        $artifactName = [string]$artifact.name
        if (-not $artifactName) {
            $artifactName = "artifact-$artifactId"
        }
        $archiveUrl = [string]$artifact.archive_download_url
        if (-not $archiveUrl) {
            continue
        }
        $expectedSize = 0
        if ($null -ne $artifact.size_in_bytes) {
            $expectedSize = [int64]$artifact.size_in_bytes
        }
        $expectedDigest = [string]$artifact.digest

        $zipPath = Join-Path $tempRoot "$artifactName-$artifactId.zip"
        $artifactExtractDir = Join-Path $extractRoot $artifactName

        Write-UiLine "Downloading artifact: $artifactName" '⬇️' 'Cyan'
        Download-GitHubArtifact $archiveUrl $zipPath $tokenInfo['Token'] $configPath $artifactName $expectedSize $expectedDigest
        Expand-ZipSafe $zipPath $artifactExtractDir
    }

    Expand-NestedZips $extractRoot

    $choice = Choose-PayloadRoot $extractRoot $opts['LaunchGlob'] $opts['PayloadRoot']
    $payloadRoot = [string]$choice.Root
    $launchables = @($choice.Launchables)
    $counts = Count-PayloadEntries $payloadRoot

    Write-UiSection 'Payload' '📁'
    Write-UiKv 'Root' $payloadRoot '📂'
    Write-UiKv 'Contents' ("{0:N0} files, {1:N0} directories" -f $counts.Files, $counts.Dirs) '📊'
    if ($launchables.Count -gt 0) {
        Write-UiLine 'Launchable candidates:' '🚀' 'Green'
        foreach ($path in @($launchables | Select-Object -First 5)) {
            $relative = $path.FullName.Substring($extractRoot.Length).TrimStart([char[]]@('\', '/'))
            Write-UiLine $relative '•'
        }
    }

    $stageDir = Join-Path $tempRoot 'stage'
    Copy-PayloadToStage $payloadRoot $stageDir
    $makeBackup = $false
    $backupTargetExists = [bool](Test-Path -LiteralPath $destDir)

    if ($Options['DryRun']) {
        $action = 'update'
        if ($Options['Mirror']) {
            $action = 'mirror-replace'
        }
        Write-UiSection 'Dry run' '🧪'
        Write-UiLine "Would $action`: $destDir" '🧪' 'Yellow'
        if ($backupTargetExists -and -not $Options['NoBackup']) {
            if ($Options['Mirror']) {
                if ($canPrompt) {
                    Write-UiLine "Would ask whether to back up current folder as: $(Get-UniqueBackupPath $destDir)" '💾' 'Yellow'
                } else {
                    Write-UiLine 'Would skip backup; prompts are disabled and the default is No.' 'ℹ️' 'DarkGray'
                }
            } else {
                if ($canPrompt) {
                    Write-UiLine "Would ask whether to create backup ZIP for overwritten release files: $(Get-UniqueBackupZipPath $destDir)" '💾' 'Yellow'
                } else {
                    Write-UiLine 'Would skip backup; prompts are disabled and the default is No.' 'ℹ️' 'DarkGray'
                }
            }
        }
        exit 0
    }

    if ($backupTargetExists -and -not $Options['NoBackup']) {
        if ($canPrompt) {
            if ($Options['Mirror']) {
                $backupPrompt = "Back up current folder before replacing it"
            } else {
                $backupPrompt = "Back up current files this release will overwrite"
            }
            $makeBackup = Prompt-YesNo $backupPrompt $false
        } else {
            Write-UiLine 'Backup skipped; prompts are disabled and the default is No.' 'ℹ️' 'DarkGray'
        }
    }

    Write-UiSection 'Deploy' '🚚'
    if ($Options['Mirror']) {
        Write-UiLine "Mirror replacing: $destDir" '🚚' 'Cyan'
    } else {
        Write-UiLine "Updating: $destDir" '🚚' 'Cyan'
    }
    $backupPath = Deploy-Stage $stageDir $destDir $makeBackup $Options['Mirror']

    if ($backupPath) {
        Write-UiLine "Backup: $backupPath" '💾' 'Yellow'
    }

    Write-Host ''
    Write-UiLine 'Done.' '✅' 'Green'
    exit 0
} catch {
    [Console]::Error.WriteLine('')
    Write-UiError "ERROR: $($_.Exception.Message)"
    exit 1
} finally {
    if ($tempRoot) {
        if ($Options -and $Options['KeepTemp']) {
            Write-Host ''
            Write-UiLine "Temp files kept at: $tempRoot" '🧰' 'Yellow'
        } else {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
