@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >/dev/null
cl /nologo /O2 /MD probe.c ufbx.c /Fe:probe.exe >build_probe.log 2>&1
