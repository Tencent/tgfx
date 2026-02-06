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
  
  # Check for clang-format availability
  if ! command -v clang-format &> /dev/null; then
    echo "clang-format not found. Trying to install..."
    # Try brew first
    if command -v brew &> /dev/null; then
      brew install clang-format
    else
      # Fallback to pip3 with proper error handling
      if command -v pip3 &> /dev/null; then
        pip3 install clang-format==14 --break-system-packages 2>/dev/null || \
        pip3 install --user clang-format==14 2>/dev/null || \
        echo "Warning: Could not install clang-format via pip3"
      fi
    fi
  fi
  
  # Check clang-format version and provide info
  if command -v clang-format &> /dev/null; then
    clangformat=`clang-format --version`
    echo "----Using $clangformat----"
    if [[ $clangformat =~ "14." ]]; then
      echo "----Preferred clang-format version 14 detected----"
    else
      echo "----Note: Using clang-format version other than 14, formatting may differ slightly----"
    fi
  else
    echo "----Warning: clang-format not available, skipping format check----"
    echo "----Complete the code format check (skipped)----"
    exit 0
  fi
fi

echo "----begin to scan code format----"
find include -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find hello2d -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find test/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find qt/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find ios/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find mac/Hello2D -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find linux/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find win/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find web/demo/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i
find android/app/src -name "*.cpp" -print -o -name "*.c" -print -o -name "*.h" -print  -o -name "*.mm" -print  -o -name "*.m" -print | xargs clang-format -i

git diff
result=`git diff`
if [[ $result =~ "diff" ]]
then
    echo "----Failed to pass the code format check----"
    exit 1
else
    echo "----Complete the code format check-----"
fi



