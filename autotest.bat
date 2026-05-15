@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo shell log - autotest start
echo %cd%

:: Initialize MSVC environment if cl.exe is not available
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo ----Setting up MSVC environment----
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
            if exist "!VCVARS!" (
                call "!VCVARS!"
                goto :env_ready
            )
        )
    )
    for %%p in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
        for %%v in (2022 2019) do (
            for %%e in (Enterprise Professional Community BuildTools) do (
                set "VCVARS=%%~p\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
                if exist "!VCVARS!" (
                    call "!VCVARS!"
                    goto :env_ready
                )
            )
        )
    )
    echo ERROR: Could not find Visual Studio installation.
    exit /b 1
)
:env_ready

set COMPLIE_RESULT=true
set WORKSPACE=%cd%

:: Clean and create directories
if exist result rd /s /q result
mkdir result
if exist build rd /s /q build
mkdir build
cd build

:: Configure CMake with Ninja
set "USE_SWIFTSHADER_FLAG="
if "%1"=="USE_SWIFTSHADER" set "USE_SWIFTSHADER_FLAG=-DTGFX_USE_SWIFTSHADER=ON"
cmake -G Ninja %USE_SWIFTSHADER_FLAG% -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..
if %errorlevel% equ 0 (
    echo ~~~~~~~~~~~~~~~~~~~CMakeLists OK~~~~~~~~~~~~~~~~~~
) else (
    echo ~~~~~~~~~~~~~~~~~~~CMakeLists error~~~~~~~~~~~~~~~~~~
    exit /b 1
)

:: Build TGFXFullTest
cmake --build . --target TGFXFullTest
if %errorlevel% equ 0 (
    echo ~~~~~~~~~~~~~~~~~~~TGFXFullTest make successed~~~~~~~~~~~~~~~~~~
) else (
    echo ~~~~~~~~~~~~~~~~~~~TGFXFullTest make error~~~~~~~~~~~~~~~~~~
    exit /b 1
)

:: Copy SwiftShader DLLs to build directory if needed
if "%1"=="USE_SWIFTSHADER" (
    copy /y "%WORKSPACE%\vendor\swiftshader\win\x64\*.dll" "%WORKSPACE%\build\" >nul 2>&1
)

:: Run tests
TGFXFullTest.exe --gtest_output=json:TGFXFullTest.json
if %errorlevel% equ 0 (
    echo ~~~~~~~~~~~~~~~~~~~TGFXFullTest successed~~~~~~~~~~~~~~~~~~
) else (
    echo ~~~~~~~~~~~~~~~~~~~TGFXFullTest Failed~~~~~~~~~~~~~~~~~~
    set COMPLIE_RESULT=false
)

:: Copy results
copy /y "%WORKSPACE%\build\*.json" "%WORKSPACE%\result\" >nul 2>&1

cd ..

xcopy /s /e /i /q "%WORKSPACE%\test\out" "%WORKSPACE%\result\out" >nul 2>&1

if "!COMPLIE_RESULT!"=="false" (
    exit /b 1
)
