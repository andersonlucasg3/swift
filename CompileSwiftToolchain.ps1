#!/usr/local/bin/pwsh

using namespace System.IO

if ([string]::IsNullOrEmpty($env:NDK_ROOT)) 
{
    Write-Host "NDK_ROOT is not set. Please set it to the path of your Android NDK installation."
    exit 1
}

if ([string]::IsNullOrEmpty($env:TOOLCHAINS))
{
    Write-Host "TOOLCHAINS is not set. Please set it to the path of your Swift toolchain installation."
    Write-Host "TOOLCHAINS must point to the root path of the Swift toolchain installation."
    exit 1
}

if ([string]::IsNullOrEmpty($env:SWIFT_PATH)) 
{
    Write-Host "SWIFT_PATH is not set. Please set it to the path of your Swift toolchain installation."
    Write-Host "SWIFT_PATH must point to the following `${env:TOOLCHAINS}/usr/bin."
    exit 1
}

$Root = $PSScriptRoot

Push-Location $Root

    $DepsPath = "./dependencies"

    if (-not (Test-Path -Path $DepsPath))
    {
        New-Item -ItemType Directory -Path $DepsPath
    }

    ./utils/update-checkout `
        --clone `
        --tag swift-5.10.1-RELEASE `
        --source-root $DepsPath

    $env:SWIFT_SOURCE_ROOT = Resolve-Path $DepsPath

    ./utils/build-script `
        -R `
        --android `
        --android-ndk $env:NDK_ROOT `
        --android-arch aarch64 `
        --android-api-level 21 `
        --stdlib-deployment-targets=android-aarch64 `
        --native-swift-tools-path=$env:SWIFT_PATH `
        --native-clang-tools-path=$env:SWIFT_PATH `
        --build-swift-tools=0 `
        --build-llvm=0 `
        --skip-build-cmark `
        --skip-test-android `
        --skip-test-osx `
        --skip-ios `
        --skip-watchos `
        --skip-tvos `
        --skip-build-benchmarks

Pop-Location