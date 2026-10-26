@echo off
setlocal
rem Prefer `py`: on Windows `python` can resolve to the Microsoft Store stub.
rem %%~$PATH:I searches PATH itself, so this does not depend on where.exe.
set "FW_PY=python"
for %%I in (py.exe) do if not "%%~$PATH:I"=="" set "FW_PY=py"
%FW_PY% "%~dp0fw.py" %*
exit /b %errorlevel%
