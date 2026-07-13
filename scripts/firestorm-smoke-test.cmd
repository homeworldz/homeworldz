@echo off
setlocal
cd /d "%~dp0.."
go run ./grid/cmd/firestorm-smoke-test %*
exit /b %errorlevel%
