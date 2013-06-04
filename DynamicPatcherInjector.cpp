﻿// created by i-saint
// distributed under Creative Commons Attribution (CC BY) license.
// https://github.com/i-saint/DynamicObjLoader

#pragma comment(lib, "psapi.lib")
#include <windows.h>
#include <psapi.h>
#include <string>
#include "dpInternal.h"

#ifdef _M_IX64
#   define InjectorDLL       "DynamicPatcherInjectorDLL64.dll"
#   define PatcherDLL        "DynamicPatcher64.dll"
#else // _M_IX86
#   define InjectorDLL       "DynamicPatcherInjectorDLL.dll"
#   define PatcherDLL        "DynamicPatcher.dll"
#endif // _M_IX64

dpConfigFile g_option;

// VirtualAllocEx で dll の path を対象プロセスに書き込み、
// CreateRemoteThread で対象プロセスにスレッドを作って、↑で書き込んだ dll path をパラメータにして LoadLibraryA を呼ぶ。
// 結果、対象プロセス内で任意の dll を実行させる。 
bool InjectDLL(HANDLE hProcess, const char* dllname)
{
    SIZE_T bytesRet = 0;
    DWORD oldProtect = 0;
    LPVOID remote_addr = NULL;
    HANDLE hThread = NULL;
    size_t len = strlen(dllname) + 1;

    remote_addr = ::VirtualAllocEx(hProcess, 0, 1024, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if(remote_addr==NULL) { return false; }
    ::VirtualProtectEx(hProcess, remote_addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    ::WriteProcessMemory(hProcess, remote_addr, dllname, len, &bytesRet);
    ::VirtualProtectEx(hProcess, remote_addr, len, oldProtect, &oldProtect);

    hThread = ::CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)((void*)&LoadLibraryA), remote_addr, 0, NULL);
    ::WaitForSingleObject(hThread, INFINITE); 
    ::VirtualFreeEx(hProcess, remote_addr, 0, MEM_RELEASE);
    return true;
}

bool TryInject(const char *process_name, const char *dllname)
{
    char dll_fullpath[MAX_PATH];
    DWORD aProcesses[1024], cbNeeded, cProcesses;
    unsigned int i;
    if(!::GetFullPathNameA(dllname, _countof(dll_fullpath), dll_fullpath, nullptr)) {
        return false;
    }

    if(!::EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) {
        return false;
    }

    cProcesses = cbNeeded / sizeof(DWORD);
    for(i=0; i<cProcesses; i++) {
        if( aProcesses[i] == 0 ) { continue; }
        HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, aProcesses[i]);
        if(hProcess==NULL) { continue; }

        char szProcessName[MAX_PATH] = "\0";
        HMODULE hMod;
        DWORD cbNeeded2;
        if(::EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded2)) {
            ::GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName)/sizeof(TCHAR));
            if( strstr(szProcessName, process_name)!=NULL) {
                if(InjectDLL(hProcess, dll_fullpath)) {
                    printf("injection completed %s -> %s\n", process_name, dllname);
                    fflush(stdout);
                    return true;
                }
            }
        }
        ::CloseHandle(hProcess);
    }

    return false;
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE prev, LPWSTR cmd, int show)
{
    bool ok = true;
    if(__argc>=2) { ok=g_option.load(__wargv[1]) ;}
    else          { ok=g_option.load(); }
    if(!ok) { return 1; }

    char exe_path[MAX_PATH];
    std::string exe_dir;
    if(TryInject(g_option.target_process.c_str(), (exe_dir+PatcherDLL).c_str())) {
        std::string conf_path;
        dpSeparateFileExt(g_option.target_process.c_str(), &conf_path, nullptr);
        conf_path += "dpconf";
        g_option.copy(conf_path.c_str());

        dpGetMainModulePath(exe_path, _countof(exe_path));
        dpSeparateDirFile(exe_path, &exe_dir, nullptr);
        TryInject(g_option.target_process.c_str(), (exe_dir+InjectorDLL).c_str());
    }
    return 0;
}
