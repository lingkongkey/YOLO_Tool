@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

echo ==========================================
echo  YOLO_Tool 一键提交并推送
echo ==========================================
cd /d "%~dp0"

:: ========== 配置区，按需修改 ==========
set "GIT_NAME=lingkongkey"
set "GIT_MAIL=lingkongkey@users.noreply.github.com"
set "REMOTE_URL=https://github.com/lingkongkey/YOLO_Tool.git"
set "BRANCH=master"
:: =======================================

set "MSG=%~1"
if "%MSG%"=="" set /p "MSG=请输入提交说明（直接回车使用时间戳）: "
if "%MSG%"=="" (
    set "MSG=update %date% %time%"
    :: 把时间里的冒号替换成横杠，避免观感问题
    set "MSG=!MSG::=-!"
)

:: 初始化（不存在.git文件夹才init）
if not exist ".git" (
    echo 未检测到git仓库，执行 git init
    git init
)

:: 检查origin是否存在
git remote | findstr /i "origin" >nul
if %errorlevel% neq 0 (
    echo origin 不存在，添加远程仓库：%REMOTE_URL%
    git remote add origin "%REMOTE_URL%"
) else (
    echo origin 已存在
)

echo.
echo git add -A
git add -A

echo.
echo 提交信息：!MSG!
git -c user.name=%GIT_NAME% -c user.email=%GIT_MAIL% commit -m "!MSG!"
set COMMIT_ERR=%errorlevel%

if %COMMIT_ERR% neq 0 (
    echo.
    echo [提示] 没有新改动，跳过commit，尝试推送
)

echo.
echo 正在推送到 origin/%BRANCH% ...
:: 第一次推送带上 -u 建立关联，后续也兼容
git push -u origin %BRANCH%
if errorlevel 1 (
    echo.
    echo ? 推送失败：检查仓库地址、网络、分支名、是否有冲突
    pause
    exit /b 1
)

echo.
echo ? 提交并推送完成。
pause
