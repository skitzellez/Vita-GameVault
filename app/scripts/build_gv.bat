@echo off
rem Game Vault · PS Vita: Release x64 build + package signed with Game Vault's certificate (VS 2022 Build Tools).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for /f "usebackq delims=" %%p in (`powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\GamerDad\XboxEmu\wt-gv-systems\GameVault\pfx_password.ps1"`) do set "PFXPW=%%p"
msbuild "%~dp0..\VitoxM2Probe.vcxproj" /nologo /m:1 /v:minimal /p:Configuration=Release /p:Platform=x64 /p:PackageCertificatePassword=%PFXPW% %*
set RC=%ERRORLEVEL%
echo EXIT=%RC%
exit /b %RC%
