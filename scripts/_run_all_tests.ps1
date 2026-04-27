$ErrorActionPreference = 'Continue'
$build = 'd:\Others\PolyglotCompiler\build'
$tests = @(
    'test_core.exe', 'test_middle.exe', 'test_backends.exe', 'test_runtime.exe',
    'test_linker.exe', 'test_plugins.exe', 'test_topology.exe', 'test_topology_ui.exe',
    'test_e2e.exe',
    'test_frontend_common.exe', 'test_frontend_cpp.exe', 'test_frontend_python.exe',
    'test_frontend_java.exe', 'test_frontend_dotnet.exe', 'test_frontend_rust.exe',
    'test_frontend_ploy.exe', 'test_frontend_go.exe', 'test_frontend_javascript.exe',
    'test_frontend_ruby.exe',
    'unit_tests.exe', 'integration_tests.exe'
)
$results = @()
foreach ($t in $tests) {
    $exe = Join-Path $build $t
    if (-not (Test-Path $exe)) { continue }
    $out = & $exe 2>&1 | Select-Object -Last 3
    $code = $LASTEXITCODE
    $summary = ($out | Where-Object { $_ -match 'tests passed|test cases|FAILED|assertion' } | Select-Object -First 1)
    if (-not $summary) { $summary = ($out | Select-Object -Last 1) }
    $results += [pscustomobject]@{ Test = $t; Exit = $code; Summary = $summary }
}
$results | Format-Table -AutoSize
$total_fail = ($results | Where-Object { $_.Exit -ne 0 }).Count
Write-Host ""
Write-Host ("==> total {0} test binaries, {1} non-zero exit codes" -f $results.Count, $total_fail)
