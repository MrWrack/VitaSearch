@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title VitaSearch Proxy Manager RC51

:menu
cls
echo ==========================================
echo       VitaSearch Proxy Manager RC51
echo ==========================================
echo.
echo [1] Install proxy requirements
echo [2] Update proxy requirements
echo [3] Configure API key / Spotify
echo [4] Start VitaSearch proxy
echo [5] Install + configure + start
echo [6] Show network/IP information
echo [0] Exit
echo.
set /p choice=Choose: 
if "%choice%"=="1" goto install
if "%choice%"=="2" goto update
if "%choice%"=="3" goto configure
if "%choice%"=="4" goto start
if "%choice%"=="5" goto all
if "%choice%"=="6" goto network
if "%choice%"=="0" goto end
goto menu

:checknode
where node >nul 2>nul
if errorlevel 1 (
  echo.
  echo ERROR: Node.js is not installed or is not in PATH.
  echo Install the current Node.js LTS release, then run this manager again.
  pause
  exit /b 1
)
where npm >nul 2>nul
if errorlevel 1 (
  echo.
  echo ERROR: npm is not available in PATH.
  pause
  exit /b 1
)
exit /b 0

:install
cls
echo Installing VitaSearch proxy requirements...
call :checknode
if errorlevel 1 goto menu
call npm install
if errorlevel 1 goto npmfail
call npx playwright install chromium
if errorlevel 1 goto playwrightfail
echo.
echo INSTALL COMPLETE.
pause
goto menu

:update
cls
echo Updating VitaSearch proxy requirements...
call :checknode
if errorlevel 1 goto menu
call npm install
if errorlevel 1 goto npmfail
call npm update
if errorlevel 1 goto npmfail
call npx playwright install chromium
if errorlevel 1 goto playwrightfail
echo.
echo UPDATE COMPLETE.
pause
goto menu

:configure
cls
echo VitaSearch proxy configuration
echo.
set "NEW_API_KEY="
set "NEW_SPOTIFY_CLIENT_ID="
set /p NEW_API_KEY=VitaSearch API key: 
if "%NEW_API_KEY%"=="" (
  echo API key cannot be empty.
  pause
  goto menu
)
set /p NEW_SPOTIFY_CLIENT_ID=Spotify Client ID ^(Enter to leave Spotify disabled^): 
(
  echo @echo off
  echo set "VITASEARCH_API_KEY=%NEW_API_KEY%"
  echo set "SPOTIFY_CLIENT_ID=%NEW_SPOTIFY_CLIENT_ID%"
  echo set "SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback"
) > proxy-settings.cmd
echo.
echo Configuration saved locally to proxy-settings.cmd.
echo Do NOT upload or commit that file because it contains your API key.
pause
goto menu

:start
cls
call :checknode
if errorlevel 1 goto menu
if not exist node_modules (
  echo Requirements are not installed yet.
  echo Running install first...
  call npm install || goto npmfail
  call npx playwright install chromium || goto playwrightfail
)
if exist proxy-settings.cmd call proxy-settings.cmd
if not defined VITASEARCH_API_KEY (
  echo No saved API key found.
  set /p VITASEARCH_API_KEY=VitaSearch API key: 
)
if "%VITASEARCH_API_KEY%"=="" (
  echo API key is required.
  pause
  goto menu
)
if not defined SPOTIFY_REDIRECT_URI set "SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback"
echo.
echo Starting VitaSearch proxy...
echo Keep this window open while using VitaSearch on the PS Vita.
echo.
call npm start
echo.
echo Proxy stopped.
pause
goto menu

:all
cls
call :checknode
if errorlevel 1 goto menu
echo [1/3] Installing/updating requirements...
call npm install || goto npmfail
call npx playwright install chromium || goto playwrightfail
echo.
echo [2/3] Configuration...
if not exist proxy-settings.cmd (
  set "NEW_API_KEY="
  set "NEW_SPOTIFY_CLIENT_ID="
  set /p NEW_API_KEY=VitaSearch API key: 
  if "!NEW_API_KEY!"=="" goto keyfail
  set /p NEW_SPOTIFY_CLIENT_ID=Spotify Client ID ^(Enter to leave Spotify disabled^): 
  > proxy-settings.cmd echo @echo off
  >> proxy-settings.cmd echo set "VITASEARCH_API_KEY=!NEW_API_KEY!"
  >> proxy-settings.cmd echo set "SPOTIFY_CLIENT_ID=!NEW_SPOTIFY_CLIENT_ID!"
  >> proxy-settings.cmd echo set "SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback"
)
call proxy-settings.cmd
echo.
echo [3/3] Starting proxy...
echo Keep this window open.
call npm start
pause
goto menu

:network
cls
echo VitaSearch network information
echo.
ipconfig | findstr /I /C:"IPv4 Address" /C:"IPv4-adress" /C:"IPv4"
echo.
echo VitaSearch proxy normally uses port 8080.
echo On the Vita use: http://YOUR_PC_IPV4:8080
echo.
pause
goto menu

:npmfail
echo.
echo ERROR: npm install/update failed. Read the error above.
pause
goto menu

:playwrightfail
echo.
echo ERROR: Playwright Chromium installation failed. Read the error above.
pause
goto menu

:keyfail
echo.
echo ERROR: API key is required.
pause
goto menu

:end
endlocal
