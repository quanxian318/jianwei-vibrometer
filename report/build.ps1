$ErrorActionPreference = 'Stop'
$tex = Get-Command xelatex -ErrorAction SilentlyContinue
if ($tex) { $engine = $tex.Source }
else { $engine = Join-Path $env:LOCALAPPDATA 'Programs\MiKTeX\miktex\bin\x64\xelatex.exe' }
if (-not (Test-Path -LiteralPath $engine)) { throw 'XeLaTeX not found.' }
$build = Join-Path $PSScriptRoot '.build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location (Join-Path $PSScriptRoot 'source')
try {
    for ($pass = 1; $pass -le 3; $pass++) {
        & $engine -interaction=nonstopmode -halt-on-error "-output-directory=$build" main.tex
        if ($LASTEXITCODE -ne 0) { throw "XeLaTeX failed on pass $pass. See .build/main.log." }
    }
    Copy-Item -LiteralPath (Join-Path $build 'main.pdf') -Destination (Join-Path $PSScriptRoot 'technical_report.pdf') -Force
}
finally { Pop-Location }
