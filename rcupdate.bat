@echo off
setlocal
cd "%~dp0"
for /f %%X in ('git describe') do set GIT_DESCRIBE=%%X
for /f "delims=v,- tokens=1,3" %%A in ('git describe') do set GIT_VERSION=%%A.%%B
echo GIT_DESCRIBE=%GIT_DESCRIBE%, GIT_VERSION=%GIT_VERSION%
rcedit-x64.exe "%1" ^
--set-product-version "%GIT_VERSION%" ^
--set-file-version "%GIT_VERSION%" ^
--set-version-string "FileDescription" "WebView2Browser" ^
--set-version-string "ProductName" "WebView2Browser" ^
--set-version-string "ProductVersion" "%GIT_DESCRIBE%" ^
--set-version-string "LegalCopyright" "SPDX-Licence-Identifier: MIT" ^
--set-icon "WebView2Browser.ico" 
