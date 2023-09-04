#!/bin/bash 

[ "$TRACE" ] && set -x

# To run this script ( to create release ) on local machine the gh cli must be installed:
#
# sudo dnf install 'dnf-command(config-manager)'
# sudo dnf config-manager --add-repo https://cli.github.com/packages/rpm/gh-cli.repo
# sudo dnf install gh -y

# Use this is the version name if running script on any branch/tag other then vX.X.X, i.e. for testing purposes
RELEASE_DEFAULT_VERSION="v0.1-test"
# Delete these if they already exist
OVERRIDABLE_RELEASES=($RELEASE_DEFAULT_VERSION, "v0.1-beta", "v0.1-edge")

files=

for arg in "$@"
do
   case "$arg" in
        --token=*)
            gh_token="${1#*=}"
            shift
        ;;
        --tag=*)
            tag="${1#*=}"
            shift
        ;;
        --message=*)
            message="${1#*=}"
            shift
        ;;
        --*)
            echo Unknown flag "${1}"
            exit 1
        ;;
        *)
            files="$files ${1}"
            shift
        ;;
    esac
done

if [ -z "$tag" ]; then 
    tag=$RELEASE_DEFAULT_VERSION
fi

if [[ $tag != v* ]]; then 
    echo Triggered by a non-standard version. Using default test version $RELEASE_DEFAULT_VERSION
    tag=$RELEASE_DEFAULT_VERSION
fi   

echo $gh_token | gh auth login --with-token
gh config set prompt disabled

# Github releases require an unique name. If the version is in overridables,
# we will delete the release, no questions asked.
# Otherwise, we will error out.
lookup=$(gh release view $tag --json tagName| jq -r ".tagName")
if [ -n "$lookup" ]; then 
    if [[ ${OVERRIDABLE_RELEASES[@]} =~ $tag ]]; then 
        gh release delete $tag -y
    fi
fi


gh release create "$tag" --draft --title "Kontain $tag" --notes "$message" $files
