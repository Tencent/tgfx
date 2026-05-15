@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

:: Try to find clang-format in PATH
where clang-format >nul 2>&1
if %errorlevel% equ 0 (
    for /f "delims=" %%i in ('where clang-format') do set "CLANG_FORMAT=%%i"
    goto :check_version
)

:: Check common pip install locations
for %%p in (Python312 Python311 Python310 Python39) do (
    if exist "%USERPROFILE%\AppData\Roaming\Python\%%p\Scripts\clang-format.exe" (
        set "CLANG_FORMAT=%USERPROFILE%\AppData\Roaming\Python\%%p\Scripts\clang-format.exe"
        goto :check_version
    )
)
if exist "%USERPROFILE%\.local\bin\clang-format.exe" (
    set "CLANG_FORMAT=%USERPROFILE%\.local\bin\clang-format.exe"
    goto :check_version
)

:: Not found, install it
echo ----clang-format not found, installing via pip----
pip install clang-format==14.0.6
if %errorlevel% neq 0 (
    echo Failed to install clang-format.
    exit /b 1
)
where clang-format >nul 2>&1
if %errorlevel% equ 0 (
    for /f "delims=" %%i in ('where clang-format') do set "CLANG_FORMAT=%%i"
    goto :check_version
)
echo Failed to locate clang-format after installation.
exit /b 1

:check_version
:: Get only the version number (third token) to avoid parentheses in full output
for /f "tokens=3" %%v in ('"!CLANG_FORMAT!" --version 2^>nul') do set "CF_VER=%%v"
echo ----clang-format %CF_VER%----

:: Check if version starts with "14."
if "!CF_VER:~0,3!"=="14." goto :do_format

echo WARNING: clang-format version 14 is recommended. Current: %CF_VER%
echo Installing clang-format 14 via pip...
pip install clang-format==14.0.6
if %errorlevel% neq 0 (
    echo Failed to install clang-format 14. Continuing with current version.
) else (
    where clang-format >nul 2>&1
    if %errorlevel% equ 0 (
        for /f "delims=" %%i in ('where clang-format') do set "CLANG_FORMAT=%%i"
    )
)

:do_format
echo ----begin to scan code format----

:: Use call :format_dir to avoid nested for /r with %%variable path issue.
:: Windows batch for /r does not expand %%d from an outer for loop correctly.
call :format_dir include
call :format_dir src
call :format_dir hello2d
call :format_dir test\src
call :format_dir qt\src
call :format_dir ios\Hello2D
call :format_dir mac\Hello2D
call :format_dir linux\src
call :format_dir win\src
call :format_dir web\demo\src
call :format_dir android\app\src

echo ----Complete the code format----
exit /b 0

:format_dir
if not exist "%~1" exit /b 0
for /r "%~1" %%f in (*.cpp *.c *.h *.mm *.m) do (
    "!CLANG_FORMAT!" -i "%%f"
)
exit /b 0
