@echo off
call "D:\Program Files\Microsoft Visual Studio\2026\insider\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d d:\Others\PolyglotCompiler\build
cmake --build . --target integration_tests 2>&1
