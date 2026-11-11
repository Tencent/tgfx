#!/usr/bin/env bash
cd $(dirname $0)

CLANG_FORMAT="${HOME}/.local/bin/clang-format"
if [ ! -f "$CLANG_FORMAT" ]; then
  CLANG_FORMAT=$(which clang-format 2>/dev/null || true)
fi

if [[ $(uname) == 'Darwin' ]]; then
  clangformat=$($CLANG_FORMAT --version 2>/dev/null)
  if [[ $clangformat =~ "14." ]]; then
    echo "----$clangformat----"
  else
    MAC_REQUIRED_TOOLS="python3 pipx"
    for TOOL in ${MAC_REQUIRED_TOOLS[@]}; do
      if [ ! $(which $TOOL) ]; then
        if [ ! $(which brew) ]; then
          echo "Homebrew not found. Trying to install..."
          /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" ||
            exit 1
        fi
        echo "$TOOL not found. Trying to install..."
        brew install $TOOL || exit 1
      fi
    done
    echo "----install clang-format----"
    pipx install clang-format==14
    CLANG_FORMAT="${HOME}/.local/bin/clang-format"
  fi
fi

if [ -z "$CLANG_FORMAT" ] || [ ! -x "$CLANG_FORMAT" ]; then
  echo "clang-format not found; install clang-format 14 to run this check" >&2
  exit 1
fi

# The directories this check covers: the project's own sources.
#   - vendor/ and third_party/ are third-party trees, never formatted here.
#   - web/ (besides demo/src) and ohos/ need toolchains this repository cannot build locally, so
#     covering them would mean reformatting sources no local build can verify. They stay out until
#     someone can build them.
DIRS=(
  include
  src
  hello2d
  test/src
  test/shader_tool
  test/minimal
  tools
  qt/src
  ios/Hello2D
  mac/Hello2D
  linux/src
  win/src
  web/demo/src
  android/app/src
)

files=()
for dir in "${DIRS[@]}"; do
  if [ ! -d "$dir" ]; then
    continue
  fi
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$dir" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.mm" \
    -o -name "*.m" \))
done

if [ ${#files[@]} -eq 0 ]; then
  echo "No source files found to check" >&2
  exit 1
fi

echo "----begin to scan code format----"
# The verdict comes from clang-format itself, not from the working tree's git state: a local run
# with uncommitted work and a clean CI checkout reach the same answer, and only real formatting
# violations fail the check.
report=$("$CLANG_FORMAT" --dry-run -Werror "${files[@]}" 2>&1)
status=$?
if [ $status -eq 0 ]; then
  echo "----Complete the code format check-----"
  exit 0
fi

echo "$report" | head -n 40
echo "----Failed to pass the code format check----"
# Format the offenders in place so running this locally fixes what it reports; the exit code still
# says the tree was not clean when the check started.
"$CLANG_FORMAT" -i "${files[@]}"
exit 1
