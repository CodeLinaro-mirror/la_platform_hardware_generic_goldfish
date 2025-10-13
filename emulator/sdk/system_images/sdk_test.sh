#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

echo "--- Running sdk_test.sh, with $1 ---"

# The content we are interested in is under the "unpacked" subdirectory
# relative to the repository root.
CONTENT_DIR="$(dirname $1)"

echo "Checking for the existence of the content directory: ${CONTENT_DIR}"
if [ ! -d "${CONTENT_DIR}" ]; then
  echo "ERROR: Content directory ${CONTENT_DIR} not found!"
  exit 1
fi

echo "Configuration: "
echo ""
cat "${CONTENT_DIR}/phone.avd/config.ini"
echo ""
echo "--- sdk_test.sh completed successfully ---"