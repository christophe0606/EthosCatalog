# Delegate package installation and validation to the shared setup script.
param([switch]$Recreate)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $taskArgs = @('run', '--no-project', '--isolated', '--python', '3.13', 'setup_venv.py', '--uv', '--python', '3.13')
    if ($Recreate) { $taskArgs += '--recreate' }
    & uv @taskArgs
    if ($LASTEXITCODE) { throw 'Python environment setup failed' }
} finally { Pop-Location }
