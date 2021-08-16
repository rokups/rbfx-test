#!/usr/bin/env bash
env
for platform in Linux MacOS Windows;
do
    if [ ! -e SDK-$platform ];
    then
        if [ "$GITHUB_ACTIONS" != "" ];
        then
            # CI itself downloads artifacts as unpacked dirs
            mv ./rbfx-$platform* ./SDK-$platform
        else
            # When testing use zips downloaded from CI
            unzip -n ./rbfx-$platform*.zip -d ./SDK-$platform
        fi
    fi
done
ls -lah

version=$(git describe --abbrev=0 --tags 2>/dev/null)
if [ "$?" -ne "0" ];
then
    version="0.0.10-dev"
fi
version=$(sed 's|^nuget/v\?||g' <<< $version)

case "$version" in
    *-alpha|*-dev)
        sha_short=$(git rev-parse --short HEAD)
        version="$version-$sha_short"
    ;;
esac

echo "Packaging $version"
nuget pack -OutputDirectory ./out -Version $version rbfx.Urho3DNet.nuspec
nuget pack -OutputDirectory ./out -Version $version rbfx.Tools.nuspec
nuget pack -OutputDirectory ./out -Version $version rbfx.CoreData.nuspec
nuget pack -OutputDirectory ./out -Version $version rbfx.Data.nuspec