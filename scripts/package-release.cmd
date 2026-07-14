@echo off
setlocal
cd /d "%~dp0.."
go run ./grid/cmd/package-release %*
exit /b %errorlevel%
