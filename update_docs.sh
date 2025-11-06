#!/usr/bin/env bash

set -e

echo "1. Generating new documentation..."
doxygen
echo "2. Documentation generated successfully."


echo "3. Replacing old documentation..."
rm -rf docs

mv html/ docs/
echo "Operation complete. Documentation is available in 'docs/'."
