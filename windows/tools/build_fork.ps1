param([ValidateRange(1,32)][int]$Jobs=8)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot=Join-Path $repoRoot 'build'
$buildDir=Join-Path $buildRoot 'windows'
$upstreamDir=Join-Path $buildRoot 'upstream'
$inputs=Join-Path $buildRoot 'inputs'
$cmake=(Get-Command cmake -ErrorAction SilentlyContinue).Source
if(-not $cmake){
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(-not (Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio C++ Build Tools and CMake first.'}
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
if(-not (Test-Path -LiteralPath $cmake)){throw 'CMake was not found in the Visual Studio installation.'}
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'

# The public source omits the Mac-exported shell data. Reuse the original
# binary resources from the pinned official package; never its executable.
New-Item -ItemType Directory -Force -Path $upstreamDir,(Join-Path $inputs 'fixtures'),(Join-Path $inputs 'runtime-compiled-animation') | Out-Null
$zip=Join-Path $upstreamDir 'portable.zip'
if(-not (Test-Path -LiteralPath $zip)){
    Invoke-WebRequest 'https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.3.1/EndfieldHUD-1.3.1-build20-windows-x64-portable.zip' -OutFile $zip
}
if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne '14CF329133D40B04B91ED88DE66B5B6711BDD1A77CB8C072F6D973FF593FB9DA'){
    throw 'The official runtime-resource package failed its SHA-256 check.'
}
$portable=Join-Path $upstreamDir 'portable'
Expand-Archive -LiteralPath $zip -DestinationPath $portable -Force
$resources=Join-Path $portable 'EndfieldHUD/resources'
$shell=Join-Path $resources 'shell'
Copy-Item (Join-Path $shell 'runtime-input/*') (Join-Path $inputs 'runtime-compiled-animation') -Recurse -Force
Copy-Item (Join-Path $shell 'catalog.ehscene') (Join-Path $inputs 'profile-outline-catalog.ehscene') -Force
Copy-Item (Join-Path $shell 'cursor.png') (Join-Path $inputs 'source-cursor.png') -Force
Copy-Item (Join-Path $shell 'watch-blur.json') (Join-Path $inputs 'fixtures/watch-blur.json') -Force
& $cmake -S (Join-Path $repoRoot 'windows') -B $buildDir -A x64 -DBUILD_TESTING=ON -DEHUD_DEVELOPER_SYMBOLS=OFF "-DEHUD_APP_INPUT_DIR=$inputs" "-DEHUD_MAP_RESOURCE_DIR=$(Join-Path $resources 'map')" "-DEHUD_ORBIPOM_RESOURCE_DIR=$(Join-Path $resources 'orbipom')"
if($LASTEXITCODE){throw 'CMake configuration failed.'}
& $cmake --build $buildDir --config Release --target EndfieldHUD resource_policy_tests overlay_host_tests settings_tests settings_canvas_tests localization_tests data_store_tests settings_save_queue_tests application_package_tests packaging_format_tests packaging_installer_tests desktop_shortcut_sync_tests app_shortcut_tests app_shortcut_service_tests app_shortcut_tasks_tests app_shortcut_icons_tests app_shortcut_canvas_tests app_shortcut_navigation_tests app_shortcut_relink_tests --parallel $Jobs -- /nodeReuse:false
if($LASTEXITCODE){throw 'Compilation failed.'}
& $ctest --test-dir $buildDir -C Release -R '^(resource_policy_contracts|overlay_host_contracts|settings_contracts|settings_canvas_contracts|localization_contracts|data_store_contracts|settings_save_queue_contracts|application_package_contracts|packaging_format_contracts|packaging_installer_contracts|desktop_shortcut_sync_contracts|app_shortcut.*contracts)$' --output-on-failure
if($LASTEXITCODE){throw 'The fork checks failed.'}
# This verifies the resource manifest and installer payload; it opens no HUD.
& $cmake --build $buildDir --config Release --target endfieldhud_release --parallel $Jobs -- /nodeReuse:false
if($LASTEXITCODE){throw 'Release packaging failed.'}
Write-Output "Installer and portable ZIP: $(Join-Path $buildDir 'packaging/dist')"
