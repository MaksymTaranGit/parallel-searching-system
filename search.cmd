@echo off
setlocal

pushd "%~dp0"

docker compose exec -T search-server /app/search %*
set EXIT_CODE=%ERRORLEVEL%

popd
exit /b %EXIT_CODE%