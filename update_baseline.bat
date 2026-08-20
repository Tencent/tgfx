@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

:: Usage:
::   update_baseline.bat [<BACKEND>] [--skip-images]
::
:: BACKEND (order-insensitive with --skip-images):
::   USE_D3D12                     D3D12 (real GPU)
::   USE_D3D12_WARP                D3D12 (WARP software adapter, used in CI)
::   USE_OPENGL                    OpenGL
::   USE_OPENGL_SWIFTSHADER        OpenGL (SwiftShader)
::   USE_VULKAN                    Vulkan (real GPU)
::   USE_VULKAN_SWIFTSHADER        Vulkan (SwiftShader)
::
:: Options:
::   --skip-images   Skip writing baseline `_base.webp` images. The cache
::                   (md5.json + version.json) is still refreshed, which is
::                   all the CI pipeline needs. Local runs omit this to also
::                   produce baseline-out\ images for visual inspection.

:: Parse arguments: separate the backend keyword from the --skip-images flag so callers may
:: pass them in any order (e.g. `update_baseline.bat --skip-images USE_D3D12`).
set "BACKEND_ARG="
set "SKIP_IMAGES=0"
:parse_args
if "%~1"=="" goto :done_args
if /I "%~1"=="--skip-images" (
    set "SKIP_IMAGES=1"
) else (
    set "BACKEND_ARG=%~1"
)
shift
goto :parse_args
:done_args

:: Determine cmake args and backend name
set "CMAKE_BACKEND_ARGS="
set "BACKEND_NAME=opengl"
set "TARGET_SUFFIX=OpenGL"

if /I "!BACKEND_ARG!"=="USE_OPENGL_SWIFTSHADER" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_SWIFTSHADER=ON"
    set "BACKEND_NAME=opengl-swiftshader"
    set "TARGET_SUFFIX=OpenGL"
)
if /I "!BACKEND_ARG!"=="USE_VULKAN_SWIFTSHADER" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON"
    set "BACKEND_NAME=vulkan-swiftshader"
    set "TARGET_SUFFIX=Vulkan"
)
if /I "!BACKEND_ARG!"=="USE_VULKAN" (
    set "CMAKE_BACKEND_ARGS=-DTGFX_USE_VULKAN=ON"
    set "BACKEND_NAME=vulkan"
    set "TARGET_SUFFIX=Vulkan"
)
if /I "!BACKEND_ARG!"=="USE_OPENGL" (
    set "CMAKE_BACKEND_ARGS="
    set "BACKEND_NAME=opengl"
    set "TARGET_SUFFIX=OpenGL"
)
if /I "!BACKEND_ARG!"=="USE_D3D12" (
   set "CMAKE_BACKEND_ARGS=-DTGFX_USE_D3D12=ON"
   set "BACKEND_NAME=d3d12"
   set "TARGET_SUFFIX=D3D12"
)
if /I "!BACKEND_ARG!"=="USE_D3D12_WARP" (
   set "CMAKE_BACKEND_ARGS=-DTGFX_USE_D3D12=ON -DTGFX_D3D12_USE_WARP=ON"
   set "BACKEND_NAME=d3d12-warp"
   set "TARGET_SUFFIX=D3D12"
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
echo ~~~~~~~~~~~~~~~~~~~Update Baseline ^(%BACKEND_NAME%^) Start~~~~~~~~~~~~~~~~~~~~~

:: Save current state
for /f "delims=" %%i in ('git rev-parse --abbrev-ref HEAD') do set "CURRENT_BRANCH=%%i"
for /f "delims=" %%i in ('git rev-parse HEAD') do set "CURRENT_COMMIT=%%i"
for /f "delims=" %%i in ('git stash list') do set "STASH_BEFORE=%%i"
git stash push --include-untracked --quiet
set "STASH_AFTER="
for /f "delims=" %%i in ('git stash list') do set "STASH_AFTER=%%i"

:: Detach at the baseline ref instead of switching to the local main branch: `git switch main`
:: fails when main is already checked out in another worktree, and that failure used to go
:: unnoticed, so the baseline got generated from the current branch and every comparison became
:: self-referential (all screenshot/PDF tests pass locally while CI fails). Prefer origin/main so
:: the generated cache matches the version.json used by the up-to-date check above.
set "BASELINE_REF=origin/main"
git rev-parse --verify --quiet origin/main >nul 2>&1
if !errorlevel! neq 0 set "BASELINE_REF=main"
git switch --detach !BASELINE_REF! --quiet
if !errorlevel! neq 0 (
    echo ~~~~~~~~~~~~~~~~~~~Update Baseline ^(%BACKEND_NAME%^) Failed: cannot check out !BASELINE_REF!~~~~~~~~~~~~~~~~~~
    set "BASELINE_FAILED=true"
    goto :restore
)

:: Install dependencies
for /f "delims=" %%i in ('npm prefix -g') do set "PATH=%%i;!PATH!"
call npm install depsync -g >nul
call depsync

:: Build UpdateBaseline
set "BUILD_DIR=build-update-baseline"
if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
mkdir %BUILD_DIR%
cd %BUILD_DIR%

:: Windows Debug builds compile far slower than macOS, so this script (which is also driven by
:: CI) builds the baseline generator in Release across all Windows backends to keep the refresh
:: turnaround acceptable. macOS update_baseline.sh keeps Debug for better diagnostics.
set "SKIP_IMAGES_FLAG="
if "!SKIP_IMAGES!"=="1" set "SKIP_IMAGES_FLAG=-DTGFX_SKIP_GENERATE_BASELINE_IMAGES=ON"
cmake -G Ninja %CMAKE_BACKEND_ARGS% %SKIP_IMAGES_FLAG% -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON -DCMAKE_BUILD_TYPE=Release ../
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
if /I "!BACKEND_ARG!"=="USE_VULKAN_SWIFTSHADER" copy /y "%~dp0vendor\swiftshader\win\x64\vk_swiftshader.dll" "%CD%\vulkan-1.dll" >nul 2>&1

UpdateBaseline_%TARGET_SUFFIX%.exe
if !errorlevel! equ 0 (
    echo ~~~~~~~~~~~~~~~~~~~Update Baseline ^(%BACKEND_NAME%^) Success~~~~~~~~~~~~~~~~~~~~~
) else (
    echo ~~~~~~~~~~~~~~~~~~~Update Baseline ^(%BACKEND_NAME%^) Failed~~~~~~~~~~~~~~~~~~
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
