#include <windows.h>
#include <TlHelp32.h>
#include <shlwapi.h>

#define PROCESS_NAME                  "spelunky.exe"
#define KERNEL32                      "kernel32.dll"
#define LOAD_LIBRARY_A                "LoadLibraryA"
#define TEXT_ERROR                    "Error!"
#define LIBRARY_EXTENSION             "dll"
#define TEXT_FAILED_FIND_PROCESS      "Failed to find Spelunky process! Spelunky must be running!"
#define TEXT_FAILED_TAKE_SNAPSHOT     "Failed to take a snapshot of current processes!"
#define TEXT_FAILED_OPEN_PROCESS      "Failed to open Spelunky process!"
#define TEXT_FAILED_KERNEL32          "Failed to get kernel32.dll handle!"
#define TEXT_FAILED_LOAD_LIBRARY      "Failed to get LoadLibraryA address!"
#define TEXT_INVALID_BUNDLE           "Failed to find injectable dll file!"
#define TEXT_FAILED_ALLOCATE_ARGUMENT "Failed to allocate memory for the LoadLibraryA argument!"
#define TEXT_FAILED_WRITE_ARGUMENT    "Failed to write LoadLibraryA argument!"
#define TEXT_FAILED_INJECT            "Failed to inject a thread into Spelunky process!"
#define TEXT_NEED_PRIVILEGES          "Failed to inject Spelunky! Try to run Ghostlunky as administrator!"

BOOL set_needed_privileges() {
    HANDLE token;
    HANDLE current_process = GetCurrentProcess();
    if (OpenProcessToken(current_process, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token)) {
        TOKEN_PRIVILEGES token_privileges;
        token_privileges.PrivilegeCount = 1;
        LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &token_privileges.Privileges[0].Luid);
        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &token_privileges, sizeof(token_privileges), NULL, NULL);
        if (GetLastError() == ERROR_SUCCESS) {
            return TRUE;
        }
    }
    return FALSE;
}

DWORD search_for_process() {
    PROCESSENTRY32 process_entry;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        process_entry.dwSize = sizeof(PROCESSENTRY32);
        BOOL current = Process32First(snapshot, &process_entry);
        while (current != FALSE) {
            CharLowerBuff(process_entry.szExeFile, strlen(process_entry.szExeFile));
            if (strstr(process_entry.szExeFile, PROCESS_NAME)) {
                return process_entry.th32ProcessID;
            }
            current = Process32Next(snapshot, &process_entry);
        }
        CloseHandle(snapshot);
        MessageBox(NULL, TEXT_FAILED_FIND_PROCESS, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    MessageBox(NULL, TEXT_FAILED_TAKE_SNAPSHOT, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
    return 0;
}

BOOL inject_dll(DWORD process_id, HINSTANCE hInstance) {
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);
    if(process != NULL) {
        HANDLE user32dll = GetModuleHandle(KERNEL32);
        if (user32dll != NULL) {
            LPVOID function_pointer = (LPVOID)GetProcAddress(user32dll, LOAD_LIBRARY_A);
            if (function_pointer != NULL) {
                CHAR dll_path[MAX_PATH + 1];
                GetModuleFileName(NULL, dll_path, sizeof(dll_path));
                dll_path[strlen(dll_path) - 3] = '\0'; // cut off "exe" part.
                strcat_s(dll_path, MAX_PATH + 1, LIBRARY_EXTENSION); // replace with dll
                if (PathFileExists(dll_path)) {
                    LPVOID function_argument = (LPVOID)VirtualAllocEx(process, 0, strlen(dll_path),
                                                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                    if (function_argument != NULL) {
                        BOOL injection_result = WriteProcessMemory(process, function_argument,
                                                                   dll_path, strlen(dll_path), 0);
                        if (injection_result != FALSE) {
                            HANDLE thread = CreateRemoteThread(process, 0, 0, (LPTHREAD_START_ROUTINE)function_pointer,
                                                               (LPVOID)function_argument, 0, 0);
                            if (thread != NULL) {
                                CloseHandle(process);
                                return TRUE;
                            } else {
                                MessageBox(NULL, TEXT_FAILED_INJECT, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
                            }
                        } else {
                            MessageBox(NULL, TEXT_FAILED_WRITE_ARGUMENT, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
                        }
                    } else {
                        MessageBox(NULL, TEXT_FAILED_ALLOCATE_ARGUMENT, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
                    }
                } else {
                    MessageBox(NULL, TEXT_INVALID_BUNDLE, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
                }
            } else {
                MessageBox(NULL, TEXT_FAILED_LOAD_LIBRARY, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
            }
        } else {
            MessageBox(NULL, TEXT_FAILED_KERNEL32, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
        }
    } else {
        MessageBox(NULL, TEXT_FAILED_OPEN_PROCESS, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
    }
    CloseHandle(process);
    return FALSE;
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow) {
    BOOL privileges_set = set_needed_privileges();
    DWORD spelunky_process_id = search_for_process();
    if (spelunky_process_id != 0) {
        BOOL injection_result = inject_dll(spelunky_process_id, hInstance);
        if (injection_result != FALSE) {
            return ERROR_SUCCESS;
        }
    }
    if (!privileges_set) {
        MessageBox(NULL, TEXT_NEED_PRIVILEGES, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
    }
    return ERROR_ACCESS_DENIED;
}
