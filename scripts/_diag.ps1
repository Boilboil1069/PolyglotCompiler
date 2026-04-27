$p = 'd:\Others\PolyglotCompiler\frontends\javascript\src\lowering\lowering.cpp'
$lines = Get-Content -LiteralPath $p
$line192 = $lines[191]
Write-Host ("Line 192: {0}" -f $line192)
$bytes = [System.Text.Encoding]::UTF8.GetBytes($line192)
$hex = ($bytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
Write-Host ("hex: {0}" -f $hex)
