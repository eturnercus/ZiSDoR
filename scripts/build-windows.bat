@echo off
rem Сборка Windows-версии компилятором MSVC. Нужны Visual Studio 2022 (C++) и CMake.
rem Результат: dist\GDZLauncher\Updater.exe и launcher.dll
rem Адрес API и сервер: set GDZ_API_URL=https://... и set GDZ_SERVER_ADDRESS=host:port перед запуском.
chcp 65001 >nul
setlocal
cd /d "%~dp0.."

cmake -S . -B build -A x64 "-DGDZ_API_URL=%GDZ_API_URL%" "-DGDZ_SERVER_ADDRESS=%GDZ_SERVER_ADDRESS%" || goto :error
cmake --build build --config Release --parallel || goto :error

if exist dist\GDZLauncher rmdir /s /q dist\GDZLauncher
mkdir dist\GDZLauncher
copy /y build\build\Release\Updater.exe dist\GDZLauncher\ >nul || goto :error
copy /y build\build\Release\launcher.dll dist\GDZLauncher\ >nul || goto :error
echo Готово: dist\GDZLauncher\Updater.exe
exit /b 0

:error
echo Сборка не удалась.
exit /b 1
