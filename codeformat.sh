#!/usr/bin/env bash
cd $(dirname $0)

if [[ $(uname) == 'Darwin' ]]; then
  MAC_REQUIRED_TOOLS="python3"
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
  clangformat=`clang-format --version`
  if [[ $clangformat =~ "14." ]]
  then
      echo "----$clangformat----"
  else
      echo "----install clang-format----"
      pip3 install clang-format==14
  fi
fi

echo "----begin to scan code format----"
find include -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find drawers -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find test/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find qt/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find ios/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find mac/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find linux/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find win/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find web/demo/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find android/app/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find inspector/common -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find inspector/client -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i

git diff
result=`git diff`
if [[ $result =~ "diff" ]]
then
    echo "----Failed to pass the code format check----"
    exit 1
else
    echo "----Complete the code format check-----"
fi



