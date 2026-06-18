@echo off
setlocal

echo ============================================
echo   B205 ChatApplication - Windows Build
echo ============================================

where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] gcc not found. Install MinGW-w64 and add it to PATH.
    echo         Download: https://www.mingw-w64.org/
    pause
    exit /b 1
)

if not exist logs mkdir logs

echo.
echo [1/2] Building server...
gcc -Wall -Wextra ^
    server\server.c server\logger.c server\room.c server\client_handler.c ^
    -Icommon -Iserver ^
    -o chat_server.exe ^
    -lws2_32
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed.
    pause
    exit /b 1
)
echo       chat_server.exe  OK

echo.
echo [2/2] Building client...
gcc -Wall -Wextra ^
    client\client.c client\logger.c client\ui.c ^
    -Icommon -Iclient ^
    -o chat_client.exe ^
    -lws2_32
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed.
    pause
    exit /b 1
)
echo       chat_client.exe  OK

echo.
echo ============================================
echo   Build successful!
echo   Run server:   chat_server.exe
echo   Run client:   chat_client.exe
echo ============================================
pause
