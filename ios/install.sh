#!/bin/bash -e
cd $(dirname $0)

../sync_deps.sh

# generate tgfx's xcode project.
cmake -G Xcode -B tgfx .. -DCMAKE_TOOLCHAIN_FILE=../../third_party/vendor_tools/ios.toolchain.cmake -DPLATFORM=OS64 -DENABLE_ARC=OFF

# Prepare the workspace in the TEMP directory
NAME="TGFXDemo.xcworkspace"
DATA_FILE="contents.xcworkspacedata"
TEMP_DIR=$(mktemp -d)
mkdir "${TEMP_DIR}/${NAME}"
touch "${TEMP_DIR}/${NAME}/${DATA_FILE}"

echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" >> "${TEMP_DIR}/${NAME}/${DATA_FILE}"
echo "<Workspace version = \"1.0\">" >> "${TEMP_DIR}/${NAME}/${DATA_FILE}"
echo "  <FileRef location = \"group:TGFXDemo.xcodeproj\"></FileRef>" >> "${TEMP_DIR}/${NAME}/${DATA_FILE}"
echo "  <FileRef location = \"group:tgfx/TGFX.xcodeproj\"></FileRef>" >> "${TEMP_DIR}/${NAME}/${DATA_FILE}"
echo "</Workspace>" >> "${TEMP_DIR}/${NAME}/${DATA_FILE}"

# Copy the new one from the TEMP to the current directory
cp -r "${TEMP_DIR}/${NAME}" .

printf "Generated %s.\n" "$NAME"
