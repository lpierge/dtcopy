@echo off
if not exist C:\BIN\NUL goto error
copy .\Debug\dtcopy.exe C:\BIN
goto done
:error
error: C:\BIN does not exist
goto done
:done