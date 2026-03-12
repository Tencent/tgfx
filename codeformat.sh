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

echo "----begin to scan code format----"
find include -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find hello2d -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find test/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find qt/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find ios/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find mac/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find linux/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find win/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find web/demo/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i
find android/app/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs $CLANG_FORMAT -i

git diff
result=`git diff`
if [[ $result =~ "diff" ]]
then
    echo "----Failed to pass the code format check----"
    exit 1
else
    echo "----Complete the code format check-----"
fi



