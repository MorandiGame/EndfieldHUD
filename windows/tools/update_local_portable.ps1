param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$Destination='D:\EndfieldHUD'
)
$ErrorActionPreference='Stop'
$packageRoot=(Resolve-Path -LiteralPath $PackageDirectory).Path
$destinationRoot=[IO.Path]::GetFullPath($Destination).TrimEnd('\')
$programEntries=@('EndfieldHUD.exe','THIRD-PARTY-NOTICES.txt','fonts','resources')
if($destinationRoot -eq [IO.Path]::GetPathRoot($destinationRoot).TrimEnd('\')){throw 'Choose an application folder, not a drive root.'}
if($packageRoot.TrimEnd('\') -eq $destinationRoot){throw 'The package and destination must be different folders.'}
foreach($entry in $programEntries){if(-not (Test-Path -LiteralPath (Join-Path $packageRoot $entry))){throw "Package is missing $entry"}}
$verification=Start-Process -FilePath (Join-Path $packageRoot 'EndfieldHUD.exe') -ArgumentList '--verify-package' -WindowStyle Hidden -PassThru
if(-not $verification.WaitForExit(30000) -or $verification.ExitCode -ne 0){throw 'The new package failed verification.'}

if(-not ('EndfieldHUDLocalUpdate.RestartManager' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Text;
using System.Runtime.InteropServices;
namespace EndfieldHUDLocalUpdate {
  public static class RestartManager {
    [StructLayout(LayoutKind.Sequential)] public struct UniqueProcess {
      public uint processId;
      public System.Runtime.InteropServices.ComTypes.FILETIME startTime;
    }
    [DllImport("rstrtmgr.dll", CharSet=CharSet.Unicode)] static extern int RmStartSession(out uint session, uint flags, StringBuilder key);
    [DllImport("rstrtmgr.dll", CharSet=CharSet.Unicode)] static extern int RmRegisterResources(uint session,uint files,string[] names,uint count,UniqueProcess[] processes,uint services,string[] serviceNames);
    [DllImport("rstrtmgr.dll")] static extern int RmShutdown(uint session,uint flags,IntPtr callback);
    [DllImport("rstrtmgr.dll")] static extern int RmEndSession(uint session);
    public static void Stop(Process process) {
      uint session;
      int error=RmStartSession(out session,0,new StringBuilder(33));
      if(error!=0)throw new System.ComponentModel.Win32Exception(error);
      try {
        long stamp=process.StartTime.ToUniversalTime().ToFileTimeUtc();
        var unique=new UniqueProcess{processId=(uint)process.Id,
          startTime=new System.Runtime.InteropServices.ComTypes.FILETIME{dwLowDateTime=unchecked((int)stamp),dwHighDateTime=(int)(stamp>>32)}};
        error=RmRegisterResources(session,0,null,1,new[]{unique},0,null);
        if(error==0)error=RmShutdown(session,0,IntPtr.Zero); // graceful only; flushes the app's documents
        if(error!=0)throw new System.ComponentModel.Win32Exception(error);
        if(!process.WaitForExit(15000))throw new InvalidOperationException("EndfieldHUD did not finish saving and exiting; no files replaced.");
      } finally { RmEndSession(session); }
    }
  }
}
'@
}
$destinationExe=Join-Path $destinationRoot 'EndfieldHUD.exe'
$running=@(Get-Process EndfieldHUD -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $destinationExe})
foreach($process in $running){[EndfieldHUDLocalUpdate.RestartManager]::Stop($process)}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$backup=Join-Path $repoRoot ('build/deploy-backups/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force -Path $backup,$destinationRoot | Out-Null
# Preferences and all user documents stay in LocalAppData, outside the four
# package entries. Compare saved JSON files while the app is stopped.
$dataRoot=Join-Path $env:LOCALAPPDATA 'EndfieldHUD'
$saved=@{}
if(Test-Path -LiteralPath $dataRoot){
    foreach($file in Get-ChildItem -LiteralPath $dataRoot -Recurse -File -Filter '*.json'){
        $saved[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    $settings=Join-Path $dataRoot 'v1/settings.json'
    if(Test-Path -LiteralPath $settings){Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'settings-before.json')}
}
try {
    foreach($entry in $programEntries){
        $old=Join-Path $destinationRoot $entry
        if(Test-Path -LiteralPath $old){Copy-Item -LiteralPath $old -Destination $backup -Recurse -Force}
    }
    foreach($entry in $programEntries){Copy-Item -LiteralPath (Join-Path $packageRoot $entry) -Destination $destinationRoot -Recurse -Force}
    foreach($path in $saved.Keys){
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $saved[$path]){throw 'A saved configuration or document changed during replacement.'}
    }
    $installed=Start-Process -FilePath $destinationExe -ArgumentList '--verify-package' -WindowStyle Hidden -PassThru
    if(-not $installed.WaitForExit(30000) -or $installed.ExitCode -ne 0){throw 'The copied package failed verification.'}
} catch {
    foreach($entry in $programEntries){
        $old=Join-Path $backup $entry
        if(Test-Path -LiteralPath $old){Copy-Item -LiteralPath $old -Destination $destinationRoot -Recurse -Force}
    }
    if($running.Count){Start-Process -FilePath $destinationExe -ArgumentList '--login' -WindowStyle Hidden | Out-Null}
    throw
}
$launched=Start-Process -FilePath $destinationExe -WorkingDirectory $destinationRoot -ArgumentList '--login' -WindowStyle Hidden -PassThru
if($launched.WaitForExit(10000)){throw "Updated application exited early ($($launched.ExitCode)). Previous package is in $backup"}
Write-Output "Updated $destinationRoot; package verified; $($saved.Count) saved JSON files unchanged; running PID $($launched.Id)."
Write-Output "Previous program and settings backup: $backup"
