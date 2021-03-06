#!/bin/bash

# Using set -u doesn't work well with rbenv
set -eo pipefail

echo "--- Pre-setup"

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)     platform="linux";;
    Darwin*)    platform="mac";;
    *)          exit 1
esac

buildkite-agent artifact download "_out_/$platform/sorbet" .

SRB_SORBET_EXE="$(realpath "_out_/$platform/sorbet")"
export SRB_SORBET_EXE
chmod +x "$SRB_SORBET_EXE"

echo "--- setup :ruby:"

# rbenv init has uninitialized variables
eval "$(rbenv init -)"

rbenv shell 2.4.3

echo "+++ tests"

# TODO(jez) test/snapshot/driver.sh is not currently capable of testing the actual gem.
# This currently tests that local development isn't broken.

gems/sorbet/test/snapshot/driver.sh --verbose
