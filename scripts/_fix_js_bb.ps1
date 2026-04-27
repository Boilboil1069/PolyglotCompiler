$p = 'd:\Others\PolyglotCompiler\frontends\javascript\src\lowering\lowering.cpp'
$c = Get-Content -Raw -LiteralPath $p
$c2 = [regex]::Replace($c, '(\w+_bb)\.get\(\)', '$1')
if ($c -ne $c2) {
    Set-Content -LiteralPath $p -Value $c2 -NoNewline
    Write-Host 'patched'
} else {
    Write-Host 'no change'
}
