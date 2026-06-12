
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <tlhelp32.h>

#pragma comment(lib, "wininet.lib")

// Modern ve Güvenli JSON Kütüphanesi
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Sol2 C++ Lua Binder
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// Win32 Kontrol ID'leri
#define IDC_MAIN_EDIT 101
#define IDC_EXECUTE_BTN 102
#define IDC_STOP_BTN 103
#define IDC_AI_INPUT 104
#define IDC_GENERATE_BTN 105
#define IDC_CONSOLE_OUTPUT 106
#define IDC_API_KEY_INPUT 107

// Thread-Safe Haberleşme İnce Ayarları
#define WM_APP_LOG             (WM_APP + 1)
#define WM_APP_AI_COMPLETE     (WM_APP + 2)
#define WM_APP_AI_ERROR        (WM_APP + 3)

// Küresel Kontrol Nesneleri
HWND hEditBox = NULL;
HWND hAIInputBox = NULL;
HWND hConsoleBox = NULL;
HWND hApiKeyInput = NULL;
HWND hMainWindow = NULL;

uintptr_t clientAddress = 0;
HANDLE hProcess = NULL;

// Thread ve Script Durum Yönetimi
std::atomic<bool> isScriptRunning(false);
std::atomic<bool> isAiRequestRunning(false);
std::thread luaWorkerThread;
std::thread aiWorkerThread;

struct Vector3 {
    float x, y, z;
};

namespace CS2Offsets {
    uintptr_t dwLocalPlayerPawn = 0x1831B18; 
    uintptr_t m_iHealth = 0x334;
    uintptr_t m_iTeamNum = 0x3C3;
    uintptr_t m_vOldOrigin = 0x127C; 
}

template <typename T>
T ReadMemory(uintptr_t address) {
    T buffer;
    if (hProcess != NULL) {
        SIZE_T bytesRead;
        if (ReadProcessMemory(hProcess, (LPCVOID)address, &buffer, sizeof(T), &bytesRead) && bytesRead == sizeof(T)) {
            return buffer;
        }
    }
    return T{};
}

void LogToConsoleThreadSafe(const std::string& message) {
    std::string* msgPtr = new std::string(message);
    if (!PostMessage(hMainWindow, WM_APP_LOG, (WPARAM)msgPtr, 0)) {
        delete msgPtr; 
    }
}

// Groq API Entegrasyon Katmanı (nlohmann/json ile Güvenli Hale Getirildi)
void RequestCodeFromGroqThread(std::string userPrompt, std::string apiKey) {
    isAiRequestRunning = true;
    std::string model = "llama-3.1-70b-versatile"; 
    
    HINTERNET hInternet = InternetOpenA("ExternalExecutor", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        std::string* errMsg = new std::string("-- HATA: Internet baglantisi basarisiz.");
        PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0);
        isAiRequestRunning = false;
        return;
    }

    HINTERNET hConnect = InternetConnectA(hInternet, "api.groq.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        std::string* errMsg = new std::string("-- HATA: Sunucu baglantisi basarisiz.");
        PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0);
        isAiRequestRunning = false;
        return;
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/v1/chat/completions", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        std::string* errMsg = new std::string("-- HATA: Istek baslatilamadi.");
        PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0);
        isAiRequestRunning = false;
        return;
    }

    std::string headers = "Authorization: Bearer " + apiKey + "\r\nContent-Type: application/json\r\n";

    std::string systemPrompt = "Sen bir CS2 Lua script ureticisisin. Sadece LUA KODU don. "
                               "Kesinlikle markdown sembolleri (```lua) veya aciklama yazilari ekleme. Sadece dogrudan calistirilabilir kod gonder. "
                               "Sana saglanan API komutlari sunlardir:\n"
                               "1. CS2.GetLocalPlayerHealth() -> Sayi doner.\n"
                               "2. CS2.GetLocalPlayerTeam() -> Sayi doner (2: Terrorist, 3: Counter-Terrorist).\n"
                               "3. CS2.GetLocalPlayerPos() -> x, y, z koordinatlarini doner.\n"
                               "4. CS2.Sleep(ms) -> Scripti belirtilen milisaniye kadar uyutur. Sonsuz dongulerde mutlaka kullanilmalidir.\n"
                               "5. print(mesaj) -> Konsol ekranina cikti yazar.";

    // nlohmann/json kullanarak güvenli payload oluşturma
    json gPayload;
    gPayload["model"] = model;
    gPayload["messages"] = json::array({
        { {"role", "system"}, {"content", systemPrompt} },
        { {"role", "user"}, {"content", userPrompt} }
    });
    gPayload["temperature"] = 0.1;

    std::string data = gPayload.dump();
    
    BOOL bSend = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)data.c_str(), (DWORD)data.length());
    
    std::string responseData = "";
    if (bSend) {
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            responseData += buffer;
        }
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    try {
        // Gelen yanıtı nlohmann/json ile güvenli bir şekilde ayrıştırıyoruz
        auto parsedJson = json::parse(responseData);
        if (parsedJson.contains("choices") && !parsedJson["choices"].empty()) {
            std::string extractedCode = parsedJson["choices"][0]["message"]["content"].get<std::string>();
            std::string* successCode = new std::string(extractedCode);
            PostMessage(hMainWindow, WM_APP_AI_COMPLETE, (WPARAM)successCode, 0);
        } else {
            std::string* errMsg = new std::string("-- HATA: Yapay zekadan gecersiz veya bos yanit alindi.");
            PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0);
        }
    }
    catch (...) {
        std::string* errMsg = new std::string("-- JSON HATASI: Sunucu yaniti cozumlenemedi.");
        PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0);
    }

    isAiRequestRunning = false;
}

bool AttachToCS2() {
    PROCESSENTRY32 entry = { sizeof(PROCESSENTRY32) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, "cs2.exe") == 0) {
                hProcess = OpenProcess(PROCESS_VM_READ, FALSE, entry.th32ProcessID);
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!hProcess) return false;

    HANDLE modSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId(hProcess));
    MODULEENTRY32 modEntry = { sizeof(MODULEENTRY32) };
    if (Module32First(modSnapshot, &modEntry)) {
        do {
            if (_stricmp(modEntry.szModule, "client.dll") == 0) {
                clientAddress = (uintptr_t)modEntry.modBaseAddr;
                break;
            }
        } while (Module32Next(modSnapshot, &modEntry));
    }
    CloseHandle(modSnapshot);
    return clientAddress != 0;
}

void ExecuteLuaWorker(std::string scriptCode) {
    isScriptRunning = true;
    LogToConsoleThreadSafe("[Sistem] Script baslatiliyor...");

    sol::state luaState;
    luaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    luaState["print"] = [](sol::variadic_args args) {
        std::ostringstream oss;
        for (auto v : args) {
            std::string s = v.as<std::string>();
            oss << s << " ";
        }
        LogToConsoleThreadSafe(oss.str());
    };

    auto cs2 = luaState.create_table("CS2");

    cs2["GetLocalPlayerHealth"] = []() -> int {
        if (!clientAddress) return 0;
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + CS2Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return 0;
        return ReadMemory<int>(localPlayerPawn + CS2Offsets::m_iHealth);
    };

    cs2["GetLocalPlayerTeam"] = []() -> int {
        if (!clientAddress) return 0;
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + CS2Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return 0;
        return ReadMemory<int>(localPlayerPawn + CS2Offsets::m_iTeamNum);
    };

    cs2["GetLocalPlayerPos"] = []() -> std::tuple<float, float, float> {
        if (!clientAddress) return { 0.0f, 0.0f, 0.0f };
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + CS2Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return { 0.0f, 0.0f, 0.0f };
        Vector3 pos = ReadMemory<Vector3>(localPlayerPawn + CS2Offsets::m_vOldOrigin);
        return { pos.x, pos.y, pos.z };
    };

    cs2["Sleep"] = [](int milliseconds) {
        int steps = milliseconds / 10;
        for (int i = 0; i < steps; ++i) {
            if (!isScriptRunning) {
                throw std::runtime_error("Script Kullanici Tarafindan Durduruldu");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    try {
        luaState.script(scriptCode);
        LogToConsoleThreadSafe("[Sistem] Script basariyla tamamlandi.");
    }
    catch (const std::exception& e) {
        std::string errStr = e.what();
        if (errStr.find("Script Kullanici Tarafindan Durduruldu") != std::string::npos) {
            LogToConsoleThreadSafe("[Sistem] Script zorla durduruldu.");
        } else {
            LogToConsoleThreadSafe("[LUA HATASI] " + errStr);
        }
    }
    catch (...) {
        LogToConsoleThreadSafe("[LUA HATASI] Bilinmeyen bir hata olustu.");
    }

    isScriptRunning = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hEditBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "-- Lua kodunuzu buraya yazin\nwhile true do\n    local hp = CS2.GetLocalPlayerHealth()\n    print(\"Mevcut Can: \" .. hp)\n    CS2.Sleep(1000) \nend",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            10, 35, 400, 390, hwnd, (HMENU)IDC_MAIN_EDIT, GetModuleHandle(NULL), NULL);

        CreateWindowEx(NULL, "BUTTON", "Scripti Calistir", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 435, 195, 40, hwnd, (HMENU)IDC_EXECUTE_BTN, GetModuleHandle(NULL), NULL);
        CreateWindowEx(NULL, "BUTTON", "Scripti Durdur", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 215, 435, 195, 40, hwnd, (HMENU)IDC_STOP_BTN, GetModuleHandle(NULL), NULL);

        hApiKeyInput = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 430, 30, 390, 25, hwnd, (HMENU)IDC_API_KEY_INPUT, GetModuleHandle(NULL), NULL);
        hAIInputBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "Canimiz 20'nin altina dustugunde konsola uyari basan bir dongu yaz.", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 430, 85, 390, 85, hwnd, (HMENU)IDC_AI_INPUT, GetModuleHandle(NULL), NULL);
        CreateWindowEx(NULL, "BUTTON", "Yapay Zekayla Kod Uret", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 430, 175, 390, 35, hwnd, (HMENU)IDC_GENERATE_BTN, GetModuleHandle(NULL), NULL);

        hConsoleBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 430, 235, 390, 240, hwnd, (HMENU)IDC_CONSOLE_OUTPUT, GetModuleHandle(NULL), NULL);
        
        // DÜZELTME B: Edit Control metin limiti maksimuma çıkartıldı (Şişme Engellendi)
        SendMessage(hConsoleBox, EM_LIMITTEXT, 0, 0);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EXECUTE_BTN) {
            if (isScriptRunning) {
                MessageBoxA(hwnd, "Zaten calismakta olan bir script var!", "Bilgi", MB_OK | MB_ICONWARNING);
                break;
            }

            int length = GetWindowTextLength(hEditBox);
            if (length > 0) {
                // DÜZELTME C: Sınır taşması (Buffer Overflow) düzeltildi. length + 1 boyut ayrılıp resize yapılıyor.
                std::string scriptCode(length + 1, '\0');
                GetWindowTextA(hEditBox, &scriptCode[0], length + 1);
                scriptCode.resize(length);

                if (luaWorkerThread.joinable()) luaWorkerThread.join();
                luaWorkerThread = std::thread(ExecuteLuaWorker, scriptCode);
                luaWorkerThread.detach();
            }
        }
        else if (LOWORD(wParam) == IDC_STOP_BTN) {
            if (isScriptRunning) {
                isScriptRunning = false; 
                LogToConsoleThreadSafe("[Sistem] Durdurma sinyali gonderildi...");
            }
        }
        else if (LOWORD(wParam) == IDC_GENERATE_BTN) {
            if (isAiRequestRunning) break;

            int keyLength = GetWindowTextLength(hApiKeyInput);
            int promptLength = GetWindowTextLength(hAIInputBox);

            if (keyLength <= 0 || promptLength <= 0) {
                MessageBoxA(hwnd, "API Key veya Prompt alani bos birakilamaz!", "Hata", MB_OK | MB_ICONERROR);
                break;
            }

            // DÜZELTME C: Güvenli String Okuma Mimarisi
            std::string promptText(promptLength + 1, '\0');
            GetWindowTextA(hAIInputBox, &promptText[0], promptLength + 1);
            promptText.resize(promptLength);

            std::string apiKey(keyLength + 1, '\0');
            GetWindowTextA(hApiKeyInput, &apiKey[0], keyLength + 1);
            apiKey.resize(keyLength);

            SetWindowTextA(GetDlgItem(hwnd, IDC_GENERATE_BTN), "Yapay Zeka Kod Uretiyor...");
            EnableWindow(GetDlgItem(hwnd, IDC_GENERATE_BTN), FALSE);

            if (aiWorkerThread.joinable()) aiWorkerThread.join();
            aiWorkerThread = std::thread(RequestCodeFromGroqThread, promptText, apiKey);
            aiWorkerThread.detach();
        }
        break;

    case WM_APP_LOG: {
        std::string* msgPtr = (std::string*)wParam;
        if (msgPtr) {
            // Üst Sınır Mekanizması: Konsol çok şişerse temizle
            int currentLength = GetWindowTextLength(hConsoleBox);
            if (currentLength > 50000) { 
                SetWindowTextA(hConsoleBox, "[Sistem] Konsol hafizasi temizlendi...\r\n");
                currentLength = GetWindowTextLength(hConsoleBox);
            }

            std::string formatted = *msgPtr + "\r\n";
            SendMessage(hConsoleBox, EM_SETSEL, (WPARAM)currentLength, (LPARAM)currentLength);
            SendMessage(hConsoleBox, EM_REPLACESEL, FALSE, (LPARAM)formatted.c_str());
            delete msgPtr; 
        }
        break;
    }

    case WM_APP_AI_COMPLETE: {
        std::string* codePtr = (std::string*)wParam;
        if (codePtr) {
            SetWindowTextA(hEditBox, codePtr->c_str());
            delete codePtr;
        }
        SetWindowTextA(GetDlgItem(hwnd, IDC_GENERATE_BTN), "Yapay Zekayla Kod Uret");
        EnableWindow(GetDlgItem(hwnd, IDC_GENERATE_BTN), TRUE);
        LogToConsoleThreadSafe("[Sistem] Kod basariyla uretildi!");
        break;
    }

    case WM_APP_AI_ERROR: {
        std::string* errPtr = (std::string*)wParam;
        if (errPtr) {
            SetWindowTextA(hEditBox, errPtr->c_str());
            delete errPtr;
        }
        SetWindowTextA(GetDlgItem(hwnd, IDC_GENERATE_BTN), "Yapay Zekayla Kod Uret");
        EnableWindow(GetDlgItem(hwnd, IDC_GENERATE_BTN), TRUE);
        break;
    }

    case WM_DESTROY:
        isScriptRunning = false;
        
        // DÜZELTME A: Agresif Kapatma ve Bellek Sızıntısı Engelleme
        if (luaWorkerThread.joinable()) luaWorkerThread.join();
        if (aiWorkerThread.joinable()) aiWorkerThread.join();

        // Kuyrukta kalan ve işlenmeyen tüm heap pointer'ları yakalayıp yok ediyoruz
        MSG dummyMsg;
        while (PeekMessage(&dummyMsg, hwnd, WM_APP_LOG, WM_APP_AI_ERROR, PM_REMOVE)) {
            if (dummyMsg.wParam) {
                std::string* leakPtr = (std::string*)dummyMsg.wParam;
                delete leakPtr;
            }
        }

        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    AttachToCS2();

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "SecureExternalAIExecutor";

    if (!RegisterClassEx(&wc)) return 0;

    hMainWindow = CreateWindowEx(NULL, "SecureExternalAIExecutor", "CS2 Thread-Safe AI Dashboard v3.0",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 530, NULL, NULL, hInstance, NULL);

    if (!hMainWindow) return 0;

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hProcess) CloseHandle(hProcess);
    return (int)msg.wParam;
}



