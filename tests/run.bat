@echo off
title Chess Test Runner
color 0B

echo.
echo ==========================================
echo           CHESS TEST RUNNER
echo ==========================================
echo.

echo [1/2] Compiling test suite...
g++ -std=c++23 -O1 -pipe -I../include tests.cpp ../src/Engine/Chess.cpp -o Test

if errorlevel 1 (
    echo.
    echo [FAILED] Compilation error
    echo ==========================================
    pause
    exit /b
)

echo [OK] Compilation successful
echo.

Test.exe

del /q Test.exe

echo.

pause
