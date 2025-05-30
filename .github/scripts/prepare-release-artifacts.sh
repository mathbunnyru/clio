#!/bin/bash

set -e -o pipefail

BINARY_NAME = "clio_server"

GITHUB_ARTIFACTS = "$(ls)"

for artifact_name in "${GITHUB_ARTIFACTS}; do
    pushd "${artifact_name}" || exit 1
    zip -r "../${artifact_name}.zip" ./${BINARY_NAME}
    popd || exit 1

    rm "${artifact_name}/${BINARY_NAME}"
    rm -r "${artifact_name}"

    sha256sum "./${artifact_name}.zip" > "./${artifact_name}.zip.sha256sum"
done
