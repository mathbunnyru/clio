#!/bin/bash

# This script modifies conanfile.py such that the specified version of libXRPL is used.

CURRENT_DIR=$(dirname "$0")
REPO_DIR=$(cd "$CURRENT_DIR/../../" && pwd)

if [[ -z "$1" ]]; then
    cat <<EOF

                                    ERROR
-----------------------------------------------------------------------------
            Version should be passed as first argument to the script.
-----------------------------------------------------------------------------

EOF
    exit 1
fi

VERSION=$1

echo "+ Updating required libXRPL version to $VERSION"

sed -i.bak -E "s|'xrpl/[a-zA-Z0-9\\.\\-]+'|'xrpl/$VERSION'|g" "$REPO_DIR/conanfile.py"
rm "$REPO_DIR/conanfile.py.bak"
