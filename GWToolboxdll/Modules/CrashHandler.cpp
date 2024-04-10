#include "stdafx.h"

#include <GWCA/Managers/UIMgr.h>

#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Scanner.h>

#include <Modules/CrashHandler.h>
#include <Modules/Resources.h>
#include <GWToolbox.h>
#include <Defines.h>

void CrashHandler::FatalAssert(const char* expr, const char* file, const unsigned line)
{
    __try {
        __debugbreak();
        const size_t len = snprintf(nullptr, 0, "Assertion Error(expr: '%s', file : '%s', line : %u", expr, file, line);
        Instance().tb_exception_message = new char[len + 1];
        snprintf(Instance().tb_exception_message, len + 1, "Assertion Error(expr: '%s', file : '%s', line : %u", expr, file, line);
        throw std::runtime_error(Instance().tb_exception_message);
    } __except (EXCEPT_EXPRESSION_ENTRY) { }

    abort();
}

void CrashHandler::GWCAPanicHandler(
    void*,
    const char* expr,
    const char* file,
    const unsigned int line,
    const char*)
{
    FatalAssert(expr, file, line);
}

void CrashHandler::OnGWCrash(GWDebugInfo* details, const uint32_t param_2, EXCEPTION_POINTERS* pExceptionPointers, char* exception_message, char* exception_file, const uint32_t exception_line)
{
    GW::HookBase::EnterHook();
    if (!Instance().gw_debug_info) {
        Instance().gw_debug_info = details;
    }
    if (!pExceptionPointers) {
        FatalAssert(exception_message, exception_file, exception_line);
    }
    __try {
        // Debug break here to catch stack trace in debug mode before dumping
        __debugbreak();
    } __except (EXCEPTION_CONTINUE_EXECUTION) { }

    // Assertion here will throw a GWToolbox exception if pExceptionPointers isn't found; this will give us the correct call stack for a GW Assertion failure in the subsequent crash dump.
    if (Crash(pExceptionPointers)) {
        abort();
    }
    Instance().gw_debug_info = nullptr;
    Instance().RetHandleCrash(details, param_2, pExceptionPointers, exception_message, exception_file, exception_line);

    GW::HookBase::LeaveHook();
    abort();
}

int failed(const char* failure_message)
{
    wchar_t error_info[512];
    swprintf(error_info, _countof(error_info),
             L"Guild Wars crashed!\n\n"
             "GWToolbox tried to create a crash dump, but failed\n\n"
             "%S\n"
             "GetLastError code: %d\n\n"
             "I don't really know what to do, sorry, contact the developers.\n",
             failure_message, GetLastError());

    MessageBoxW(nullptr, error_info, L"GWToolbox++ crash dump error", 0);
    return 1;
}

LONG WINAPI CrashHandler::Crash(EXCEPTION_POINTERS* pExceptionPointers)
{
    const std::wstring crash_folder = Resources::GetPath(L"crashes");

    const DWORD ProcessId = GetCurrentProcessId();
    const DWORD ThreadId = GetCurrentThreadId();

    // Instead of writing to Crash.dmp, write to gwtoolboxpp/crashes
    SYSTEMTIME stLocalTime;
    GetLocalTime(&stLocalTime);
    wchar_t szFileName[MAX_PATH];
    const auto fn_print = swprintf(szFileName, MAX_PATH, L"%s\\%S%S-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp",
                                   crash_folder.c_str(), GWTOOLBOXDLL_VERSION, GWTOOLBOXDLL_VERSION_BETA, stLocalTime.wYear, stLocalTime.wMonth,
                                   stLocalTime.wDay, stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond, ProcessId, ThreadId);

    MINIDUMP_USER_STREAM_INFORMATION* UserStreamParam = nullptr;
    char* extra_info = nullptr;

    MINIDUMP_EXCEPTION_INFORMATION* ExpParam = nullptr;
    const HANDLE hFile = CreateFileW(
        szFileName, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, 0, nullptr);
    if (!Resources::EnsureFolderExists(crash_folder.c_str())) {
        return failed("Failed to create crash directory");
    }
    if (fn_print < 0) {
        return failed("Failed to swprintf crash file name");
    }
    if (hFile == INVALID_HANDLE_VALUE) {
        return failed("Failed to CreateFileW crash file");
    }
    if (!Resources::EnsureFolderExists(crash_folder.c_str())) {
        return failed("Failed to create crash directory");
    }

    if (fn_print < 0) {
        return failed("Failed to swprintf crash file name");
    }
    if (Instance().gw_debug_info) {
        extra_info = Instance().gw_debug_info->buffer;
    }
    else if (Instance().tb_exception_message) {
        extra_info = Instance().tb_exception_message;
    }
    if (extra_info) {
        UserStreamParam = new MINIDUMP_USER_STREAM_INFORMATION();
        const auto s = new MINIDUMP_USER_STREAM();
        s->Type = CommentStreamA;
        s->Buffer = extra_info;
        s->BufferSize = (strlen(extra_info) + 1) * sizeof(extra_info[0]);
        UserStreamParam->UserStreamCount = 1;
        UserStreamParam->UserStreamArray = s;
    }
    if (pExceptionPointers) {
        ExpParam = new MINIDUMP_EXCEPTION_INFORMATION;
        ExpParam->ThreadId = ThreadId;
        ExpParam->ExceptionPointers = pExceptionPointers;
        ExpParam->ClientPointers = false;
    }
    const BOOL success = MiniDumpWriteDump(
        GetCurrentProcess(), ProcessId, hFile,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs),
        ExpParam, UserStreamParam, nullptr);
    CloseHandle(hFile);

    delete[] Instance().tb_exception_message;

    if (UserStreamParam) {
        delete UserStreamParam->UserStreamArray;
        delete UserStreamParam;
    }
    delete ExpParam;
    if (!success) {
        return failed("Failed to create MiniDumpWriteDump");
    }
    wchar_t error_info[512];
    swprintf(error_info, _countof(error_info), L"Guild Wars crashed!\n\n"
             "GWToolbox created a crash dump for more info\n\n"
             "Crash file created @ %s\n\n", szFileName);
    MessageBoxW(nullptr, error_info, L"GWToolbox++ crash dump created!", 0);
    abort();
}

void CrashHandler::Cleanup()
{
    if (HandleCrash_Func) {
        GW::Hook::RemoveHook(HandleCrash_Func);
        HandleCrash_Func = nullptr;
    }
}

void CrashHandler::Terminate()
{
    ToolboxModule::Terminate();
    Cleanup();
}

void CrashHandler::Initialize()
{
    ToolboxModule::Initialize();
    GW::RegisterPanicHandler(GWCAPanicHandler, nullptr);
    HandleCrash_Func = (HandleCrash_pt)GW::Scanner::Find("\x68\x00\x00\x08\x00\xff\x75\x1c", "xxxxxxxx", -0x4C);
    if (HandleCrash_Func) {
        GW::Hook::CreateHook(HandleCrash_Func, OnGWCrash, (void**)&RetHandleCrash);
        GW::Hook::EnableHooks(HandleCrash_Func);
    }
}
