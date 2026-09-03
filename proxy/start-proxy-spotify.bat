@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title VitaSearch Spotify Proxy RC51

:menu
cls
echo =============================================
echo       VitaSearch Spotify Setup - RC51
echo =============================================
echo.
echo [1] First setup - install everything
echo [2] Configure Vita API key + Spotify Client ID
echo [3] Start proxy with Spotify
echo [4] Update npm + Chromium
echo [5] Show saved Spotify configuration
echo [6] Clear Spotify login/token
echo [7] Show PC IPv4 address
echo [0] Exit
echo.
set /p choice=Choose: 
if "%choice%"=="1" goto first
if "%choice%"=="2" goto config
if "%choice%"=="3" goto start
if "%choice%"=="4" goto update
if "%choice%"=="5" goto show
if "%choice%"=="6" goto cleartoken
if "%choice%"=="7" goto ip
if "%choice%"=="0" goto end
goto menu

:check
where node >nul 2>nul || (echo ERROR: Node.js is missing. Install Node.js LTS first.& pause & exit /b 1)
where npm >nul 2>nul || (echo ERROR: npm is missing from PATH.& pause & exit /b 1)
exit /b 0

:first
cls
call :check
if errorlevel 1 goto menu
echo [1/3] Installing Node packages...
call npm install || goto fail
echo.
echo [2/3] Installing Playwright Chromium...
call npx playwright install chromium || goto fail
echo.
echo [3/3] Spotify configuration...
call :writeconfig
if errorlevel 1 goto menu
echo.
echo Setup complete. Spotify redirect URI must be EXACTLY:
echo http://127.0.0.1:8080/spotify/callback
echo.
choice /M "Start proxy now"
if errorlevel 2 goto menu
goto start

:config
cls
call :writeconfig
pause
goto menu

:writeconfig
set "NEW_API="
set "NEW_CLIENT="
echo Enter the SAME VitaSearch API key used on the PS Vita.
set /p NEW_API=VitaSearch API key: 
if "!NEW_API!"=="" (echo ERROR: API key cannot be empty.& exit /b 1)
echo.
echo Paste the Client ID from your Spotify Developer app.
echo Do NOT enter Client Secret.
set /p NEW_CLIENT=Spotify Client ID: 
if "!NEW_CLIENT!"=="" (echo ERROR: Spotify Client ID cannot be empty.& exit /b 1)
> proxy-settings.cmd echo @echo off
>>proxy-settings.cmd echo set "VITASEARCH_API_KEY=!NEW_API!"
>>proxy-settings.cmd echo set "SPOTIFY_CLIENT_ID=!NEW_CLIENT!"
>>proxy-settings.cmd echo set "SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback"
echo.
echo Saved locally. proxy-settings.cmd is gitignored.
echo Redirect URI: http://127.0.0.1:8080/spotify/callback
exit /b 0

:start
cls
call :check
if errorlevel 1 goto menu
if not exist node_modules (call npm install || goto fail & call npx playwright install chromium || goto fail)
if not exist proxy-settings.cmd (
  echo Spotify is not configured yet.
  call :writeconfig
  if errorlevel 1 goto menu
)
call proxy-settings.cmd
if not defined VITASEARCH_API_KEY (echo ERROR: VitaSearch API key missing.& pause&goto menu)
if not defined SPOTIFY_CLIENT_ID (echo ERROR: Spotify Client ID missing.& pause&goto menu)
set "SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback"
echo =============================================
echo VitaSearch RC51 proxy + Spotify starting
echo =============================================
echo Spotify Client ID: !SPOTIFY_CLIENT_ID!
echo Redirect URI: !SPOTIFY_REDIRECT_URI!
echo API key: SET ^(hidden^)
echo.
echo IMPORTANT: Keep this window open while using the Vita.
echo On Vita: Spotify ^> CONNECT
echo.
call npm start
echo.
echo Proxy stopped.
pause
goto menu

:update
cls
call :check
if errorlevel 1 goto menu
call npm install || goto fail
call npm update || goto fail
call npx playwright install chromium || goto fail
echo Update complete.
pause
goto menu

:show
cls
if not exist proxy-settings.cmd (echo Not configured yet.& pause&goto menu)
call proxy-settings.cmd
echo Spotify Client ID: %SPOTIFY_CLIENT_ID%
echo Redirect URI: %SPOTIFY_REDIRECT_URI%
echo VitaSearch API key: SET ^(hidden^)
echo.
echo Spotify Dashboard redirect URI must match exactly:
echo http://127.0.0.1:8080/spotify/callback
pause
goto menu

:cleartoken
cls
if exist spotify-token.json (
 del /q spotify-token.json
 echo Spotify token deleted. You can log in again.
) else (
 echo No saved Spotify token found.
)
pause
goto menu

:ip
cls
ipconfig | findstr /I /C:"IPv4 Address" /C:"IPv4-adress" /C:"IPv4"
echo.
echo Vita Proxy URL should normally be: http://YOUR_PC_IPV4:8080
pause
goto menu

:fail
echo.
echo ERROR: command failed. Read the message above.
pause
goto menu

:end
endlocal
