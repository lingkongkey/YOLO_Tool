@echo off
rem 用 VS2019 打开 YOLO_Tool 工程
set "DEVENV=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe"
if not exist "%DEVENV%" (
    echo [错误] 未找到 VS2019: %DEVENV%
    pause
    exit /b 1
)
start "" "%DEVENV%" "%~dp0YOLO_Tool.sln"
