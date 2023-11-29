#!/bin/bash -e
cd $(dirname $0)

if [[ $(uname) == 'Darwin' ]]; then
  if [[ $(clang-format --version) =~ "14." ]]
  then
      echo "----`clang-format --version`----"
  else
      if [ ! $(python3 --version) ]; then
          if [ ! $(brew --version) ]; then
            echo "Homebrew not found. Trying to install..."
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
          fi
          echo "python3 not found. Trying to install..."
          brew install python3
      fi
      echo "clang-format 14.0.0 not found. Trying to install..."
      pip3 install clang-format==14
  fi
fi

echo "----begin to scan code format----"
find include/ -iname '*.h' -print0 | xargs clang-format -i
# shellcheck disable=SC2038
find src -name "*.cpp" -print  -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
# shellcheck disable=SC2038
find test \( -path test/framework/lzma \) -prune -o -name "*.cpp" -print  -o -name "*.h" -print | xargs clang-format -i

git diff
result=`git diff`
if [[ $result =~ "diff" ]]
then
    echo "----Failed to pass coding specification----"
    exit 1
else
    echo "----Pass coding specification----"
fi

echo "----Complete the scan code format-----"

