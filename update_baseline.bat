@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

:: Determine cmake args and backend name
set "CMAKE_BACKEND_ARGS="
set "BACKEND_NAME=opengl"
set "TARGET_SUFFIX=OpenGL"

if /I "%~1"=="USE_OPENGL_SWIFTSHADER" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_SWIFTSHADER=ON"
    set "BACKEND_NAME=opengl-swiftshader"
    set "TARGET_SUFFIX=OpenGL"
)
if /I "%~1"=="USE_VULKAN_SWIFTSHADER" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON"
    set "BACKEND_NAME=vulkan-swiftshader"
    set "TARGET_SUFFIX=Vulkan"
)
if /I "%~1"=="USE_VULKAN" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_VULKAN=ON"
    set "BACKEND_NAME=vulkan"
    set "TARGET_SUFFIX=Vulkan"
)
if /I "%~1"=="USE_OPENGL" (
    set "CMAKE_BACKEND_ARGS="
    set "BACKEND_NAME=opengl"
    set "TARGET_SUFFIX=OpenGL"
)

:: Check if cache is up to date
set "CACHE_VERSION_FILE=test\baseline\.cache\%BACKEND_NAME%\version.json"

if not exist "%CACHE_VERSION_FILE%" goto :do_update

:: Compare cache version with origin/main version
git show origin/main:test/baseline/version.json > "%TEMP%\tgfx_main_version.json" 2>nul
if %errorlevel% neq 0 goto :do_update

fc /b "%TEMP%\tgfx_main_version.json" "%CACHE_VERSION_FILE%" >nul 2>&1
if %errorlevel% equ 0 (
    echo Cache for %BACKEND_NAME% is up to date.
    exit /b 0
)

:do_update
echo ~~~~~~~~~~~~~~~~~~~Update Baseline (%BACKEND_NAME%) Start~~~~~~~~~~~~~~~~~~~~~

:: Save current state
for /f "delims=" %%i in ('git rev-parse --abbrev-ref HEAD') do set "CURRENT_BRANCH=%%i"
for /f "delims=" %%i in ('git rev-parse HEAD') do set "CURRENT_COMMIT=%%i"
for /f "delims=" %%i in ('git stash list') do set "STASH_BEFORE=%%i"
git stash push --include-untracked --quiet
set "STASH_AFTER="
for /f "delims=" %%i in ('git stash list') do set "STASH_AFTER=%%i"

:: Switch to main
git switch main --quiet

:: Install dependencies
for /f "delims=" %%i in ('npm prefix -g') do set "PATH=%%i;!PATH!"
call npm install depsync -g >nul
call depsync

:: Build UpdateBaseline
set "BUILD_DIR=build-update-baseline"
if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
mkdir %BUILD_DIR%
cd %BUILD_DIR%

cmake -G Ninja %CMAKE_BACKEND_ARGS% -DTGFX_SKIP_GENERATE_BASELINE_IMAGES=ON -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON -DCMAKE_BUILD_TYPE=Debug ../
if %errorlevel% neq 0 (
    echo CMake configuration failed
    set "BASELINE_FAILED=true"
    cd ..
    goto :restore
)

cmake --build . --target UpdateBaseline_%TARGET_SUFFIX%
if !errorlevel! neq 0 (
    echo Build failed
    set "BASELINE_FAILED=true"
    cd ..
    goto :restore
)

:: Set up SwiftShader Vulkan library so volk can load it at runtime.
if /I "%BACKEND_ARG%"=="USE_VULKAN_SWIFTSHADER" copy /y "%WORKSPACE%\vendor\swiftshader\win\x64\vk_swiftshader.dll" "%CD%\vulkan-1.dll" >nul 2>&1

UpdateBaseline_%TARGET_SUFFIX%.exe
if !errorlevel! equ 0 (
    echo ~~~~~~~~~~~~~~~~~~~Update Baseline (%BACKEND_NAME%) Success~~~~~~~~~~~~~~~~~~~~~
) else (
    echo ~~~~~~~~~~~~~~~~~~~Update Baseline (%BACKEND_NAME%) Failed~~~~~~~~~~~~~~~~~~
    set "BASELINE_FAILED=true"
)

cd ..

:restore
:: Restore previous state
if "%CURRENT_BRANCH%"=="HEAD" (
    git checkout %CURRENT_COMMIT% --quiet
) else (
    git switch %CURRENT_BRANCH% --quiet
)
if not "!STASH_BEFORE!"=="!STASH_AFTER!" (
    git stash pop --index --quiet
)

call depsync

:: Cleanup
if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
del "%TEMP%\tgfx_main_version.json" >nul 2>&1

if "!BASELINE_FAILED!"=="true" (
    if not exist result mkdir result
    xcopy /s /e /i /q test\out result\out >nul 2>&1
    exit /b 1
)
exit /b 0
