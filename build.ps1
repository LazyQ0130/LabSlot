# Build from the project root; optionally run P0, P1, P2, P3 and P4 integration suites.
# Parameters: -Test enables tests; -Compiler selects gcc.exe. Exit nonzero on failure.
param(
    [switch]$Test,
    [string]$Compiler = 'gcc'
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand -and $Compiler -eq 'gcc') {
    # Prefer GCC on PATH, then the portable toolchain installed for this project.
    $cachedCompiler = Join-Path $env:USERPROFILE '.cache\labslot-toolchain\w64devkit\bin\gcc.exe'
    if (Test-Path -LiteralPath $cachedCompiler -PathType Leaf) {
        $Compiler = $cachedCompiler
    } else {
        throw 'GCC was not found. Install MinGW-w64 or pass -Compiler with the gcc.exe path.'
    }
} elseif ($null -ne $compilerCommand) {
    $Compiler = $compilerCommand.Source
}

Push-Location -LiteralPath $PSScriptRoot
try {
    $sourceFiles = @(Get-ChildItem -LiteralPath 'src' -Filter '*.c' |
        Sort-Object Name | ForEach-Object { $_.FullName })
    & $Compiler '-std=c11' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-Iinclude' @sourceFiles '-o' 'smart-lab.exe'
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host 'Build OK: smart-lab.exe (C11, warnings treated as errors)'
    if ($Test) {
        # Python is a development dependency; the C executable does not need it.
        $env:PYTHONIOENCODING = 'utf-8'
        foreach ($testScript in @('tests/test_p0.py', 'tests/test_p1.py', 'tests/test_p2.py', 'tests/test_p3.py', 'tests/test_p4.py')) {
            & python $testScript '--executable' 'smart-lab.exe' '--cc' $Compiler
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
        }
    }
} finally {
    Pop-Location
}
