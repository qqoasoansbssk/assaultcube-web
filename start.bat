@echo off
rem Starts the local server and opens the game in the default browser.
cd /d "%~dp0"
if not exist node_modules\ws (
  echo Installing server dependency...
  call npm install --no-audit --no-fund
)
echo AssaultCube Web: http://localhost:8080/
start "" http://localhost:8080/
node serve.mjs 8080
