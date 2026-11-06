@echo off
chcp 65001 >nul
echo --------------------
echo Git Push (SSH)
echo --------------------

git diff --quiet --exit-code
if %errorlevel% equ 0 (
    echo No changes detected.
    pause
    exit /b
)

git add .
git commit -m "auto update %date% %time%"
git push

echo Done!
pause
