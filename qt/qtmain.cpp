#include "lc_global.h"
#include "lc_application.h"
#include "lc_qupdatedialog.h"
#include "lc_mainwindow.h"
#include "project.h"
#include "lc_colors.h"
#include "lc_partselectionwidget.h"
#include <QApplication>
#include <locale.h>

#ifdef Q_OS_WIN

#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#include <direct.h>
#include <shlobj.h>
#pragma warning(pop)

#ifdef UNICODE
#ifndef _UNICODE
#define _UNICODE
#endif
#endif

#include <tchar.h>

static TCHAR minidumpPath[_MAX_PATH];

static LONG WINAPI lcSehHandler(PEXCEPTION_POINTERS exceptionPointers)
{ 
	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	HMODULE dbgHelp = LoadLibrary(TEXT("dbghelp.dll"));

	if (dbgHelp == nullptr)
		return EXCEPTION_EXECUTE_HANDLER;

	HANDLE file = CreateFile(minidumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
		return EXCEPTION_EXECUTE_HANDLER;

	typedef BOOL (WINAPI *LPMINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType, CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, CONST PMINIDUMP_USER_STREAM_INFORMATION UserEncoderParam, CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
	LPMINIDUMPWRITEDUMP miniDumpWriteDump = (LPMINIDUMPWRITEDUMP)GetProcAddress(dbgHelp, "MiniDumpWriteDump");
	if (!miniDumpWriteDump)
		return EXCEPTION_EXECUTE_HANDLER;

	MINIDUMP_EXCEPTION_INFORMATION mei;

	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = exceptionPointers;
	mei.ClientPointers = TRUE;

	BOOL writeDump = miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, exceptionPointers ? &mei : nullptr, nullptr, nullptr);

	CloseHandle(file);
	FreeLibrary(dbgHelp);

	if (writeDump)
	{
		TCHAR message[_MAX_PATH + 256];
		lstrcpy(message, TEXT("LeoCAD just crashed. Crash information was saved to the file '"));
		lstrcat(message, minidumpPath);
		lstrcat(message, TEXT("', please send it to the developers for debugging."));

		MessageBox(nullptr, message, TEXT("LeoCAD"), MB_OK);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

static void lcSehInit()
{
	if (SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, minidumpPath) == S_OK)
	{
		lstrcat(minidumpPath, TEXT("\\LeoCAD\\"));
		_tmkdir(minidumpPath);
		lstrcat(minidumpPath, TEXT("minidump.dmp"));
	}

	SetUnhandledExceptionFilter(lcSehHandler);
}

static void lcRegisterShellFileTypes()
{
	TCHAR modulePath[_MAX_PATH], longModulePath[_MAX_PATH];
	TCHAR temp[2*_MAX_PATH];

	GetModuleFileName(nullptr, longModulePath, _MAX_PATH);
	if (GetShortPathName(longModulePath, modulePath, _MAX_PATH) == 0)
		lstrcpy(modulePath, longModulePath);

	if (RegSetValue(HKEY_CLASSES_ROOT, TEXT("LeoCAD.Project"), REG_SZ, TEXT("LeoCAD Project"), lstrlen(TEXT("LeoCAD Project")) * sizeof(TCHAR)) != ERROR_SUCCESS)
		return;

	lstrcpy(temp, modulePath);
	lstrcat(temp, TEXT(",0"));
	if (RegSetValue(HKEY_CLASSES_ROOT, TEXT("LeoCAD.Project\\DefaultIcon"), REG_SZ, temp, lstrlen(temp) * sizeof(TCHAR)) != ERROR_SUCCESS)
		return;

	lstrcpy(temp, modulePath);
	lstrcat(temp, TEXT(" \"%1\""));
	if (RegSetValue(HKEY_CLASSES_ROOT, TEXT("LeoCAD.Project\\shell\\open\\command"), REG_SZ, temp, lstrlen(temp) * sizeof(TCHAR)) != ERROR_SUCCESS)
		return;

	LONG size = 2 * _MAX_PATH;
	LONG result = RegQueryValue(HKEY_CLASSES_ROOT, TEXT(".lcd"), temp, &size);

	if (result != ERROR_SUCCESS || !lstrlen(temp) || lstrcmp(temp, TEXT("LeoCAD.Project")))
	{
		if (RegSetValue(HKEY_CLASSES_ROOT, TEXT(".lcd"), REG_SZ, TEXT("LeoCAD.Project"), lstrlen(TEXT("LeoCAD.Project")) * sizeof(TCHAR)) != ERROR_SUCCESS)
			return;

		HKEY key;
		DWORD disposition = 0;

		if (RegCreateKeyEx(HKEY_CLASSES_ROOT, TEXT(".lcd\\ShellNew"), 0, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ, nullptr, &key, &disposition) != ERROR_SUCCESS)
			return;

		result = RegSetValueEx(key, TEXT("NullFile"), 0, REG_SZ, (CONST BYTE*)TEXT(""), (lstrlen(TEXT("")) + 1) * sizeof(TCHAR));

		if (RegCloseKey(key) != ERROR_SUCCESS || result != ERROR_SUCCESS)
			return;
	}
}

#endif

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
	app.setApplicationDisplayName("LeoCAD");
#endif

	QCoreApplication::setOrganizationDomain("leocad.org");
	QCoreApplication::setOrganizationName("LeoCAD Software");
	QCoreApplication::setApplicationName("LeoCAD");
	QCoreApplication::setApplicationVersion(LC_VERSION_TEXT);

	QTranslator Translator;
	Translator.load(QString("leocad_") + QLocale::system().name().section('_', 0, 0) + ".qm", ":/resources");
	app.installTranslator(&Translator);

	qRegisterMetaTypeStreamOperators<QList<int> >("QList<int>");

	g_App = new lcApplication();

#if defined(Q_OS_WIN)
	char libPath[LC_MAXPATH], *ptr;
	strcpy(libPath, argv[0]);
	ptr = strrchr(libPath,'\\');
	if (ptr)
		*(++ptr) = 0;

	lcRegisterShellFileTypes();
	lcSehInit();
#elif defined(Q_OS_MAC)
	QDir bundlePath = QDir(QCoreApplication::applicationDirPath());
	bundlePath.cdUp();
	bundlePath.cdUp();
	bundlePath = QDir::cleanPath(bundlePath.absolutePath() + "/Contents/Resources/");
	QByteArray pathArray = bundlePath.absolutePath().toLocal8Bit();
	const char* libPath = pathArray.data();
#else
	const char* libPath = LC_INSTALL_PREFIX "/share/leocad/";
#endif

#ifdef LC_LDRAW_LIBRARY_PATH
	const char* LDrawPath = LC_LDRAW_LIBRARY_PATH;
#else
	const char* LDrawPath = nullptr;
#endif
	
	setlocale(LC_NUMERIC, "C");

	bool ShowWindow;
	if (!g_App->Initialize(argc, argv, libPath, LDrawPath, ShowWindow))
		return 1;

	int ExecReturn = 0;

	if (ShowWindow)
	{
		gMainWindow->SetColorIndex(lcGetColorIndex(4));
		gMainWindow->GetPartSelectionWidget()->SetDefaultPart();
		gMainWindow->UpdateRecentFiles();
		gMainWindow->show();

#if !LC_DISABLE_UPDATE_CHECK
		lcDoInitialUpdateCheck();
#endif

		ExecReturn = app.exec();
	}

	delete gMainWindow;
	gMainWindow = nullptr;
	delete g_App;
	g_App = nullptr;

	return ExecReturn;
}
