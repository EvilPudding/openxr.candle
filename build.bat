@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D %~dp0

set sources=openxr.c xrbody.c
set subdirs=components

set DIR=build
set CFLAGS=/O2 /W1 /D_CRT_SECURE_NO_WARNINGS /IOpenXR-SDK\include /MT
mkdir %DIR%
mkdir %DIR%\xrsdk

CALL ..\candle\vcenv.bat

cmake -B %DIR%\xrsdk OpenXR-SDK
cmake --build %DIR%\xrsdk --config Release

set objects=
FOR %%f IN (%sources%) DO @IF EXIST "%%f" (
	set src=%DIR%\%%f
	CALL set object=%%src:.c=.obj%%
	..\candle\build\datescomp.exe %%f !object! || (
		cl /c "%%f" /Fo"!object!" %CFLAGS% || (
			echo Error compiling %%f
			GOTO END
		)
	)
	CALL set objects=!objects! !object!
)

echo openxr.candle\%DIR%\export.lib > %DIR%\libs

lib !objects! %DIR%\xrsdk\src\loader\Release\openxr_loader.lib /out:"%DIR%\export.lib"
echo resauces > %DIR%\res

:END
