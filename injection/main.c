#include "resource.h"

#include <windows.h>
#include <time.h>

#define WINDOW_CLASS                  "SpelunkyInjector"
#define CONTROL_LABEL                 "Static"
#define CONTROL_TEXT_EDIT             "Edit"
#define CONTROL_BUTTON                "Button"
#define TEXT_WINDOW_TITLE             "Ghostlunky"
#define TEXT_LABEL_SEED               "Seed:"
#define TEXT_CHECKBOX_UPDATE_ON_DEATH "Update on death"
#define TEXT_BUTTON_APPLY             "Apply seed"
#define TEXT_BUTTON_GENERATE_RANDOM   "Generate random seed"
#define TEXT_BUTTON_DISABLE           "Disable seed"
#define TEXT_LABEL_CREDITS            "Ghostlunky by Andrej Suvorau (https://www.suvrik.com/)"
#define TEXT_ERROR                    "Error!"
#define TEXT_INVALID_SPELUNKY         "Spelunky executable's bytecode is different from what is expected!"
#define TEXT_FAILED_CHANGE_MEMORY     "Failed to change the process memory!"
#define TEXT_FAILED_INTERFACE         "Failed to initialize interface!"
#define TEXT_FAILED_CREATE_THREAD     "Failed to create a separate thread!"

HWND window;
HWND edit_seed;
HWND checkbox_update;
HWND button_apply;
HWND button_generate;
HWND button_disable;
CHAR seed_buffer[64];
BOOL patched = FALSE;

BYTE OLD_LEVEL_RANDOM_BYTECODE[] = {
        0x8D, 0x54, 0x24, 0x30, // lea  edx, [esp+30h]
        0x52,                   // push edx
        0xFF, 0xD6,             // call esi
        0x8B, 0x44, 0x24, 0x30  // mov  eax, dword ptr [esp+30h]
};

BYTE NEW_LEVEL_RANDOM_BYTECODE[] = {
        0xBE, 0x90, 0x90, 0x90, 0x90, // mov  esi, [level_generation_seed]
        0xFF, 0xD6,                   // call esi
        0x89, 0x44, 0x24, 0x30        // mov  [esp+30h], eax
};


BYTE OLD_PLAY_GAMEOVER_PROCEDURE_BYTECODE[] = {
        0xC7, 0x46, 0x58, 0x1E, 0x00, 0x00, 0x00, // mov  dword ptr [esi+58h], 1Eh
        0xE8, 0xB3, 0x25, 0x08, 0x00,             // call sub_27BA90
};

BYTE NEW_PLAY_GAMEOVER_PROCEDURE_BYTECODE[] = {
        0xB8, 0x90, 0x90, 0x90, 0x90, // mov  eax, [level_generation_seed]
        0xFF, 0xD0,                   // call eax
};

BYTE ALTAR_RANDOM_BYTECODE[]        = { 0x80, 0xBB, 0x28, 0x06, 0x44, 0x00, 0x00 };   // cmp byte ptr [ebx+440628h], 0
BYTE POT_RANDOM_BYTECODE[]          = { 0x80, 0xBF, 0x28, 0x06, 0x44, 0x00, 0x00 };   // cmp byte ptr [edi+440628h], 0
BYTE CRATE_RANDOM_BYTECODE[]        = { 0x38, 0x9E, 0x28, 0x06, 0x44, 0x00 };         // cmp [esi+440628h], bl
BYTE UNDEFINED1_RANDOM_BYTECODE[]   = { 0x38, 0x9E, 0x28, 0x06, 0x44, 0x00 };         // cmp [esi+440628h], bl
BYTE UNDEFINED2_RANDOM_BYTECODE[]   = { 0x80, 0xBF, 0x28, 0x06, 0x44, 0x00, 0x00 };   // cmp byte ptr [edi+440628h], 0

BYTE NEW_ENTITIES_RANDOM_BYTECODE[] = {
        0x33, 0xC0, // xor eax, eax
        0x40,       // inc eax ; basically sets ZF to 1
};

DWORD hash_seed(LPCSTR seed) {
    DWORD hash = 5381;
    DWORD c;
    while (c = *seed++) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

DWORD get_numeric_seed() {
    GetWindowText(edit_seed, seed_buffer, sizeof(seed_buffer));
    return hash_seed((LPCSTR)seed_buffer);
}

LPCSTR get_random_string(DWORD seed) {
    srand(seed);
    for (unsigned i = 0; i < 8; i++) {
        DWORD value = (DWORD)rand() % (('Z' - 'A') + ('z' - 'a') + ('9' - '0'));
        if (value < 'Z' - 'A') {
            seed_buffer[i] = (CHAR)('A' + value);
        } else if (value >= ('Z' - 'A') && value < ('Z' - 'A') + ('z' - 'a')) {
            seed_buffer[i] = (CHAR)('a' + (value - ('Z' - 'A')));
        } else {
            seed_buffer[i] = (CHAR)('0' + (value - ('Z' - 'A') - ('z' - 'a')));
        }
    }
    seed_buffer[9] = '\0';
    return seed_buffer;
}

__declspec(dllexport) DWORD WINAPI level_generation_seed() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    DWORD file = *(PDWORD)(spelunky + 0x1384B4);
    DWORD current_level = *(PDWORD)(file + 0x4405D4);
    return get_numeric_seed() * current_level;
}

typedef VOID(*GameOverUndefinedProcedure)(DWORD, DWORD);

__declspec(dllexport) VOID WINAPI undefined_gameover_procedure_proxy(DWORD a, DWORD b) {
    if (SendMessage(checkbox_update, BM_GETCHECK, 0, 0)) {
        SetWindowTextA(edit_seed, get_random_string(get_numeric_seed()));
        UpdateWindow(window);
    }
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    *(PDWORD)(*(PDWORD)(spelunky + 0x1384B4) + 0x58) = 0x1E;
    ((GameOverUndefinedProcedure)(spelunky + 0xEBA90))(a, b);
}

BOOL replace_memory(LPCVOID old_bytecode, DWORD old_bytecode_size,
                    LPCVOID new_bytecode, DWORD new_bytecode_size,
                    PVOID target_memory) {
    DWORD old_protection;
    if (VirtualProtect(target_memory, old_bytecode_size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        if (memcmp(target_memory, old_bytecode, old_bytecode_size) == 0) {
            memcpy(target_memory, new_bytecode, new_bytecode_size);
            for (DWORD i = 0; old_bytecode_size-- > new_bytecode_size; i++) {
                *((PBYTE)target_memory + new_bytecode_size + i) = 0x90; // nop
            }
            VirtualProtect(target_memory, old_bytecode_size, old_protection, &old_protection);
            return TRUE;
        } else {
            MessageBox(NULL, TEXT_INVALID_SPELUNKY, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
            return FALSE;
        }
    } else {
        MessageBox(NULL, TEXT_FAILED_CHANGE_MEMORY, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
}

VOID restore_memory(LPCVOID old_bytecode, DWORD old_bytecode_size, PVOID target_memory) {
    DWORD old_protection;
    if (VirtualProtect(target_memory, old_bytecode_size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        memcpy(target_memory, old_bytecode, old_bytecode_size);
        VirtualProtect(target_memory, old_bytecode_size, old_protection, &old_protection);
    }
}

BOOL replace_level_random() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    DWORD function_pointer = (DWORD)level_generation_seed;
    memcpy(NEW_LEVEL_RANDOM_BYTECODE + 1, &function_pointer, sizeof(DWORD));
    return replace_memory(OLD_LEVEL_RANDOM_BYTECODE, sizeof(OLD_LEVEL_RANDOM_BYTECODE),
                          NEW_LEVEL_RANDOM_BYTECODE, sizeof(NEW_LEVEL_RANDOM_BYTECODE),
                          (LPVOID)(spelunky + 0x69FA5));
}

VOID restore_level_random() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    restore_memory(OLD_LEVEL_RANDOM_BYTECODE, sizeof(OLD_LEVEL_RANDOM_BYTECODE), (LPVOID)(spelunky + 0x69FA5));
}

BOOL replace_entity_random(LPCVOID old_bytecode, DWORD old_bytecode_size,
                           PVOID target_memory) {
    return replace_memory(old_bytecode, old_bytecode_size,
                          NEW_ENTITIES_RANDOM_BYTECODE, sizeof(NEW_ENTITIES_RANDOM_BYTECODE),
                          target_memory);
}

BOOL replace_entities_random() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    if (!replace_entity_random(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B))) {
        return FALSE;
    }
    if (!replace_entity_random(POT_RANDOM_BYTECODE, sizeof(POT_RANDOM_BYTECODE), (PVOID)(spelunky + 0x21155))) {
        restore_memory(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B));
        return FALSE;
    }
    if (!replace_entity_random(CRATE_RANDOM_BYTECODE, sizeof(CRATE_RANDOM_BYTECODE), (PVOID)(spelunky + 0x32C44))) {
        restore_memory(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B));
        restore_memory(POT_RANDOM_BYTECODE, sizeof(POT_RANDOM_BYTECODE), (PVOID)(spelunky + 0x21155));
        return FALSE;
    }
    if (!replace_entity_random(UNDEFINED1_RANDOM_BYTECODE, sizeof(UNDEFINED1_RANDOM_BYTECODE),
                               (PVOID)(spelunky + 0x3318C))) {
        restore_memory(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B));
        restore_memory(POT_RANDOM_BYTECODE, sizeof(POT_RANDOM_BYTECODE), (PVOID)(spelunky + 0x21155));
        restore_memory(CRATE_RANDOM_BYTECODE, sizeof(CRATE_RANDOM_BYTECODE), (PVOID)(spelunky + 0x32C44));
        return FALSE;
    }
    if (!replace_entity_random(UNDEFINED2_RANDOM_BYTECODE, sizeof(UNDEFINED2_RANDOM_BYTECODE),
                               (PVOID)(spelunky + 0x36712))) {
        restore_memory(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B));
        restore_memory(POT_RANDOM_BYTECODE, sizeof(POT_RANDOM_BYTECODE), (PVOID)(spelunky + 0x21155));
        restore_memory(CRATE_RANDOM_BYTECODE, sizeof(CRATE_RANDOM_BYTECODE), (PVOID)(spelunky + 0x32C44));
        restore_memory(UNDEFINED1_RANDOM_BYTECODE, sizeof(UNDEFINED1_RANDOM_BYTECODE), (PVOID)(spelunky + 0x3318C));
        return FALSE;
    }
    return TRUE;
}

VOID restore_entities_random() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    restore_memory(ALTAR_RANDOM_BYTECODE, sizeof(ALTAR_RANDOM_BYTECODE), (PVOID)(spelunky + 0x14F3B));
    restore_memory(POT_RANDOM_BYTECODE, sizeof(POT_RANDOM_BYTECODE), (PVOID)(spelunky + 0x21155));
    restore_memory(CRATE_RANDOM_BYTECODE, sizeof(CRATE_RANDOM_BYTECODE), (PVOID)(spelunky + 0x32C44));
    restore_memory(UNDEFINED1_RANDOM_BYTECODE, sizeof(UNDEFINED1_RANDOM_BYTECODE), (PVOID)(spelunky + 0x3318C));
    restore_memory(UNDEFINED2_RANDOM_BYTECODE, sizeof(UNDEFINED2_RANDOM_BYTECODE), (PVOID)(spelunky + 0x36712));
}

BOOL replace_gameover_update() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    DWORD function_pointer = (DWORD)undefined_gameover_procedure_proxy;
    memcpy(NEW_PLAY_GAMEOVER_PROCEDURE_BYTECODE + 1, &function_pointer, sizeof(DWORD));
    return replace_memory(OLD_PLAY_GAMEOVER_PROCEDURE_BYTECODE, sizeof(OLD_PLAY_GAMEOVER_PROCEDURE_BYTECODE),
                          NEW_PLAY_GAMEOVER_PROCEDURE_BYTECODE, sizeof(NEW_PLAY_GAMEOVER_PROCEDURE_BYTECODE),
                          (PVOID)(spelunky + 0x694D1));
}

VOID restore_gameover_update() {
    DWORD spelunky = (DWORD)GetModuleHandle(NULL);
    restore_memory(OLD_PLAY_GAMEOVER_PROCEDURE_BYTECODE, sizeof(OLD_PLAY_GAMEOVER_PROCEDURE_BYTECODE),
                   (PVOID)(spelunky + 0x694D1));
}

VOID handle_interface(PMSG msg) {
    if (msg->hwnd == button_apply && msg->message == WM_LBUTTONUP) {
        if (!patched) {
            if (replace_level_random()) {
                if (replace_entities_random()) {
                    if (replace_gameover_update()) {
                        patched = TRUE;
                    } else {
                        restore_level_random();
                        restore_entities_random();
                    }
                } else {
                    restore_level_random();
                }
            }
        }
        MessageBeep(MB_OK);
    } else if (msg->hwnd == button_disable && msg->message == WM_LBUTTONUP) {
        if (patched) {
            restore_level_random();
            restore_entities_random();
            restore_gameover_update();
            patched = FALSE;
        }
        MessageBeep(MB_OK);
    } else if (msg->hwnd == button_generate && msg->message == WM_LBUTTONUP) {
        SetWindowTextA(edit_seed, get_random_string(get_numeric_seed()));
        MessageBeep(MB_OK);
        UpdateWindow(window);
    } else if (msg->hwnd == checkbox_update && msg->message == WM_LBUTTONUP) {
        if (SendMessage(checkbox_update, BM_GETCHECK, 0, 0)) {
            PostMessage(checkbox_update, BM_SETCHECK, BST_CHECKED, 0);
        } else {
            PostMessage(checkbox_update, BM_SETCHECK, BST_UNCHECKED, 0);
        }
    }
}

LRESULT CALLBACK window_process(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch(message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

void set_client_size(HWND window, int width, int height)  {
    RECT client_size, window_size;
    POINT border_size;
    GetClientRect(window, &client_size);
    GetWindowRect(window, &window_size);
    border_size.x = (window_size.right - window_size.left) - client_size.right;
    border_size.y = (window_size.bottom - window_size.top) - client_size.bottom;
    MoveWindow(window, window_size.left, window_size.top, width + border_size.x, height + border_size.y, TRUE);
}

BOOL init_interface(HINSTANCE hInstance) {
    WNDCLASSEX wcl;
    wcl.cbSize        = sizeof(WNDCLASSEX);
    wcl.style         = CS_HREDRAW | CS_VREDRAW;
    wcl.lpfnWndProc   = window_process;
    wcl.cbClsExtra    = 0;
    wcl.cbWndExtra    = 0;
    wcl.hInstance     = hInstance;
    wcl.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcl.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wcl.hbrBackground = CreateSolidBrush(RGB(240, 240, 240));
    wcl.lpszMenuName  = NULL;
    wcl.lpszClassName = WINDOW_CLASS;
    wcl.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassEx(&wcl);
    window = CreateWindowEx(0, WINDOW_CLASS, TEXT_WINDOW_TITLE,
                            (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
                            0, 0, 100, 100, NULL, NULL, hInstance, NULL);
    set_client_size(window, 260, 200);
    if (window != NULL) {
        if (CreateWindowEx(0, CONTROL_LABEL, TEXT_LABEL_SEED, WS_CHILD | WS_VISIBLE,
                           5, 5, 250, 20, window, NULL, hInstance, NULL)) {
            edit_seed = CreateWindowEx(0, CONTROL_TEXT_EDIT, get_random_string((DWORD)time(NULL)),
                                       WS_CHILD | WS_VISIBLE | WS_BORDER, 5, 25, 250, 20,
                                       window, NULL, hInstance, NULL);
            if (edit_seed != NULL) {
                checkbox_update = CreateWindowEx(0, CONTROL_BUTTON, TEXT_CHECKBOX_UPDATE_ON_DEATH,
                                                 BS_CHECKBOX | WS_CHILD | WS_VISIBLE, 5, 50, 250, 20,
                                                 window, NULL, hInstance, NULL);
                PostMessage(checkbox_update, BM_SETCHECK, BST_CHECKED, 0);
                if (checkbox_update != NULL) {
                    button_apply = CreateWindowEx(0, CONTROL_BUTTON, TEXT_BUTTON_APPLY, WS_CHILD | WS_VISIBLE,
                                                 5, 75, 250, 30, window, NULL, hInstance, NULL);
                    if (button_apply != NULL) {
                        button_generate = CreateWindowEx(0, CONTROL_BUTTON, TEXT_BUTTON_GENERATE_RANDOM,
                                                         WS_CHILD | WS_VISIBLE, 5, 110, 250, 20,
                                                         window, NULL, hInstance, NULL);
                        if (button_generate != NULL) {
                            button_disable = CreateWindowEx(0, CONTROL_BUTTON, TEXT_BUTTON_DISABLE,
                                                            WS_CHILD | WS_VISIBLE, 5, 135, 250, 20,
                                                            window, NULL, hInstance, NULL);
                            if (button_disable != NULL) {
                                if (CreateWindowEx(0, CONTROL_LABEL, TEXT_LABEL_CREDITS,
                                                   WS_CHILD | WS_VISIBLE | SS_CENTER, 5, 160, 250, 35,
                                                   window, NULL, hInstance, NULL)) {
                                    ShowWindow(window, SW_SHOW);
                                    UpdateWindow(window);
                                    SwitchToThisWindow(window, FALSE);
                                    return TRUE;
                                }
                            }
                        }
                    }
                }

            }
        }
    }
    return FALSE;
}

DWORD WINAPI main_thread(LPVOID hInstance) {
    if (init_interface((HINSTANCE)hInstance) == TRUE) {
        MSG message;
        while (GetMessage(&message, NULL, 0, 0)) {
            handle_interface(&message);
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return message.wParam;
    } else {
        MessageBox(NULL, TEXT_FAILED_INTERFACE, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            if (CreateThread(NULL, 0, main_thread, (LPVOID) hInstance, 0, NULL) != NULL) {
                DisableThreadLibraryCalls(hInstance);
                return TRUE;
            } else {
                MessageBox(NULL, TEXT_FAILED_CREATE_THREAD, TEXT_ERROR, MB_ICONEXCLAMATION | MB_OK);
                return FALSE;
            }
        }
        case DLL_PROCESS_DETACH:
            restore_level_random();
            restore_entities_random();
            restore_gameover_update();
            patched = FALSE;
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        default: break; // not interested.
    }
    return TRUE;
}
