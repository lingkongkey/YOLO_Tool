@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo  YOLO_Tool 一键提交并推送
echo ==========================================

set "MSG=%~1"
if "%MSG%"=="" set /p "MSG=请输入提交说明（直接回车使用时间戳）: "
if "%MSG%"=="" set "MSG=update %date% %time%"

git add -A
git -c user.name=lingkongkey -c user.email=lingkongkey@users.noreply.github.com commit -m "%MSG%"
if errorlevel 1 (
	echo.
	echo 没有新的改动需要提交，直接推送已有提交...
)

echo.
echo 正在推送到 origin/master ...
git push origin master
if errorlevel 1 (
	echo.
	echo 推送失败：请检查网络或代理后重试。
	pause
	exit /b 1
)

echo.
echo 提交并推送完成。
pause
