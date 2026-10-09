param(
 [string]$NdkRoot='C:\Users\yang\AppData\Local\Android\Sdk\ndk-bundle',
 [string]$JdkRoot='C:\Program Files\Java\jdk-17',
 [string]$OutputDir=(Join-Path $PSScriptRoot '.test-build')
)
$ErrorActionPreference='Stop'
$bin=Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin'
$sysroot=Join-Path (Split-Path $bin -Parent) 'sysroot'
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$source=Join-Path $PSScriptRoot 'avs3a_reference.c'
foreach($target in @('aarch64-linux-android24','armv7a-linux-androideabi24')) {
 foreach($language in @('c','c++')) {
  $std=if($language -eq 'c'){'c11'}else{'c++17'}
  & (Join-Path $bin 'clang.exe') "--target=$target" "--sysroot=$sysroot" -x $language "-std=$std" -Wall -Wextra -Werror -fsyntax-only $source
  if($LASTEXITCODE){throw "layout/reference compile failed: $target/$language"}
  Write-Output "layout assertions passed: $target/$language"
 }
}
& (Join-Path $bin 'clang.exe') --target=x86_64-w64-windows-gnu -std=c11 -O2 -ffreestanding -fno-builtin -c $source -o (Join-Path $OutputDir 'reference.obj')
if($LASTEXITCODE){throw 'host reference compile failed'}
& (Join-Path $bin 'ld.lld.exe') -flavor link /dll /noentry /machine:x64 /export:avs3a_crc16 /export:avs3a_parse_header /export:avs3a_validate_frame /export:avs3a_prepare_new_decoder (Join-Path $OutputDir 'reference.obj') "/out:$(Join-Path $OutputDir 'reference.dll')"
if($LASTEXITCODE){throw 'host reference link failed'}
& node (Join-Path $PSScriptRoot 'make-vectors.cjs') $OutputDir
if($LASTEXITCODE){throw 'vector/table checks failed'}
& (Join-Path $JdkRoot 'bin\javac.exe') --add-modules jdk.incubator.foreign -encoding UTF-8 -d $OutputDir (Join-Path $PSScriptRoot 'ReferenceCheck.java')
if($LASTEXITCODE){throw 'reference test javac failed'}
$result = & (Join-Path $JdkRoot 'bin\java.exe') --add-modules jdk.incubator.foreign --enable-native-access=ALL-UNNAMED -cp $OutputDir ReferenceCheck (Join-Path $OutputDir 'reference.dll') (Join-Path $OutputDir 'synthetic-header-cases.tsv')
if($LASTEXITCODE){throw 'reference C tests failed'}
Write-Output $result
$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $OutputDir 'reference-test-result.txt'),(($result -join "`n")+"`n"),$utf8)
$inputs=@('avs3a_reference.c','avs3a_reference.h','avs3a_vendor_layout.h','ReferenceCheck.java','make-vectors.cjs','run-tests.ps1')
$hashes=@{}
foreach($name in $inputs){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
$report=[ordered]@{
 kind='C_REFERENCE_ONLY'; status='PASS'; vendorExecuted=$false; deviceValidation='NOT_RUN'
 targets=@('aarch64-linux-android24/c11','aarch64-linux-android24/c++17','armv7a-linux-androideabi24/c11','armv7a-linux-androideabi24/c++17')
 runtime='Windows x64 own reference DLL via JDK17 FFM'; result=($result -join "`n")
 clang=(& (Join-Path $bin 'clang.exe') --version | Select-Object -First 1)
 node=(& node --version); inputSha256=$hashes
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'reference-test-report.json'),($report|ConvertTo-Json -Depth 8),$utf8)