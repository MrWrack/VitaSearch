@echo off
setlocal
cd /d "%~dp0"
echo VitaSearch RC43 - Spotify Proxy
echo.
set /p VITASEARCH_API_KEY=VitaSearch API key: 
set /p SPOTIFY_CLIENT_ID=Spotify Client ID: 
if "%VITASEARCH_API_KEY%"=="" goto missing
if "%SPOTIFY_CLIENT_ID%"=="" goto missing
set SPOTIFY_REDIRECT_URI=http://127.0.0.1:8080/spotify/callback
if not exist node_modules call npm install
call npx playwright install chromium
npm start
goto end
:missing
echo API key and Spotify Client ID are required.
pause
:end
endlocal
