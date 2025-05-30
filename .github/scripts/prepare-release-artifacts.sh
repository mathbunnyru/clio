#!/bin/bash

set -e -o pipefail

BINARY_NAME = "clio_server"

GITHUB_ARTIFACTS = "$(ls)"

for artifact_dir in "${GITHUB_ARTIFACTS}; do
    pushd "${artifact_dir}" || exit 1
    zip -r "../${artifact_dir}.zip" ./${BINARY_NAME}
    popd || exit 1

    rm "${artifact_dir}/${BINARY_NAME}"
    rm -r "${artifact_dir}"

    sha256sum ./${artifact_dir}.zip > ./${artifact_dir}.zip.sha256sum
done
