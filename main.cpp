
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <tuple>
#include <tlhelp32.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")

// Modern ve Güvenli JSON Kütüphanesi
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Sol2 C++ Lua Bağlayıcısı
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// CS2-Dumper uyumlu yeni offsets başlık dosyası
#include "offsets.hpp"

// Win32 Kontrol ID'leri
#define IDC_MAIN_EDIT 101
#define IDC_EXECUTE_BTN 102
#define IDC_STOP_BTN 103
#define IDC_AI_INPUT 104
#define IDC_GENERATE_BTN 105
#define IDC_CONSOLE_OUTPUT 106
#define IDC_API_KEY_INPUT 107

// Thread-Safe Haberleşme Mesaj ID'leri
#define WM_APP_LOG             (WM_APP + 1)
#define WM_APP_AI_COMPLETE     (WM_APP + 2)
#define WM_APP_AI_ERROR        (WM_APP + 3)

// Küresel Pencereler
HWND hEditBox = NULL;
HWND hAIInputBox = NULL;
HWND hConsoleBox = NULL;
HWND hApiKeyInput = NULL;
HWND hMainWindow = NULL;

uintptr_t clientAddress = 0;
HANDLE hProcess = NULL;

// Thread ve Script Durum Kontrolleri
std::atomic<bool> isScriptRunning(false);
std::atomic<bool> isAiRequestRunning(false);
std::thread luaWorkerThread;
std::thread aiWorkerThread;

// 3 Boyutlu Koordinat Vektörü
struct Vector3 {
    float x, y, z;
};

// Lua Scriptine Gönderilecek Oyuncu Bilgisi Yapısı
struct LuaPlayer {
    std::string name;
    int health;
    int team;
    bool is_alive;
    float x, y, z;
    bool is_local;
};

// Güvenli Hafıza Okuma Şablonu
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

// Win32 Konsoluna Thread-Safe Log Yazma Yardımcısı
void LogToConsoleThreadSafe(const std::string& message) {
    std::string* msgPtr = new std::string(message);
    if (!PostMessage(hMainWindow, WM_APP_LOG, (WPARAM)msgPtr, 0)) {
        delete msgPtr; 
    }
}

// Groq Yapay Zeka API Bağlantı Kanalı
void RequestCodeFromGroqThread(std::string userPrompt, std::string apiKey) {
    isAiRequestRunning = true;
    std::string model = "llama-3.1-70b-versatile"; 
    
    HINTERNET hInternet = InternetOpenA("ExternalExecutor", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        std::string* errMsg = new std::string("-- HATA: Internet baglantisi kurulurken hata olustu.");
        if (!PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0)) delete errMsg;
        isAiRequestRunning = false;
        return;
    }

    HINTERNET hConnect = InternetConnectA(hInternet, "api.groq.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        std::string* errMsg = new std::string("-- HATA: API sunucusu ile baglanti kurulamadi.");
        if (!PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0)) delete errMsg;
        isAiRequestRunning = false;
        return;
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/v1/chat/completions", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        std::string* errMsg = new std::string("-- HATA: HTTP istek protokolu baslatilamadi.");
        if (!PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0)) delete errMsg;
        isAiRequestRunning = false;
        return;
    }

    std::string headers = "Authorization: Bearer " + apiKey + "\r\nContent-Type: application/json\r\n";

    std::string systemPrompt = 
        "Sen mukemmel bir Counter-Strike 2 (CS2) Lua script gelistiricisisin. Sadece LUA KODU uretmelisin.\n"
        "Kesinlikle aciklama yazma, markdown (```lua) etiketleri kullanma. Sadece dogrudan calisabilir Lua kodu don.\n\n"
        "Sistemimizdeki Tum CS2 Lua API Yetenekleri:\n"
        "1. CS2.GetLocalPlayerHealth() -> Lokal oyuncunun canini doner (int).\n"
        "2. CS2.GetLocalPlayerTeam() -> Lokal oyuncunun takimini doner (2: Terrorist, 3: Counter-Terrorist).\n"
        "3. CS2.GetLocalPlayerPos() -> Kendi pozisyonumuzu doner: x, y, z (3 float degeri).\n"
        "4. CS2.GetPlayers() -> Lobideki tum aktif oyuncularin listesini (table) doner. Listelenen her oyuncunun altinda su veriler vardir:\n"
        "   - player.name (string - Oyuncu adi)\n"
        "   - player.health (int - Can)\n"
        "   - player.team (int - 2:T, 3:CT)\n"
        "   - player.is_alive (boolean - Canli mi)\n"
        "   - player.x, player.y, player.z (float - Koordinatlar)\n"
        "   - player.is_local (boolean - Biz miyiz)\n"
        "5. CS2.Sleep(ms) -> Kod akisini milisaniye bazinda bekletir. Dongulerde mutlak suretle kullanilmalidir!\n"
        "6. print(mesaj) -> Konsol ekranina cikti basar.\n\n"
        "Arka plandaki C++ mimarisi CS2 Dumper offset haritasiyla tam uyumludur. Pawn nesnesi m_vOldOrigin ve m_iHealth offsetleri uzerinden guvenle okunmaktadir.";

    json gPayload;
    gPayload["model"] = model;
    gPayload["messages"] = json::array({
        { {"role", "system"}, {"content", systemPrompt} },
        { {"role", "user"}, {"content", userPrompt} }
    });
    gPayload["temperature"] = 0.15;

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
        auto parsedJson = json::parse(responseData);
        if (parsedJson.contains("choices") && !parsedJson["choices"].empty()) {
            std::string extractedCode = parsedJson["choices"][0]["message"]["content"].get<std::string>();
            std::string* successCode = new std::string(extractedCode);
            if (!PostMessage(hMainWindow, WM_APP_AI_COMPLETE, (WPARAM)successCode, 0)) delete successCode;
        } else {
            std::string* errMsg = new std::string("-- HATA: Yapay zekadan bos veya hatali veri dondu.");
            if (!PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0)) delete errMsg;
        }
    }
    catch (...) {
        std::string* errMsg = new std::string("-- JSON HATASI: Groq API sunucu yaniti parse edilemedi.");
        if (!PostMessage(hMainWindow, WM_APP_AI_ERROR, (WPARAM)errMsg, 0)) delete errMsg;
    }

    isAiRequestRunning = false;
}

// Oyuna Hafıza Seviyesinde Bağlanma Algoritması
bool AttachToCS2() {
    PROCESSENTRY32 entry = { sizeof(PROCESSENTRY32) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, "cs2.exe") == 0) {
                // PROCESS_QUERY_INFORMATION modül listelemesi için gereklidir
                hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, entry.th32ProcessID);
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

// Lua Script Çalıştırma ve API Bağlantısı
void ExecuteLuaWorker(std::string scriptCode) {
    isScriptRunning = true;
    LogToConsoleThreadSafe("[Sistem] Lua script baslatiliyor...");

    sol::state luaState;
    luaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    // print yönlendirmesi
    luaState["print"] = [](sol::variadic_args args) {
        std::ostringstream oss;
        for (auto v : args) {
            std::string s = v.as<std::string>();
            oss << s << " ";
        }
        LogToConsoleThreadSafe(oss.str());
    };

    auto cs2 = luaState.create_table("CS2");

    // LuaPlayer nesne yapısını Sol2'ye tanıtıyoruz
    luaState.new_usertype<LuaPlayer>("LuaPlayer",
        "name", &LuaPlayer::name,
        "health", &LuaPlayer::health,
        "team", &LuaPlayer::team,
        "is_alive", &LuaPlayer::is_alive,
        "x", &LuaPlayer::x,
        "y", &LuaPlayer::y,
        "z", &LuaPlayer::z,
        "is_local", &LuaPlayer::is_local
    );

    // Kendi Canımız
    cs2["GetLocalPlayerHealth"] = []() -> int {
        if (!clientAddress) return 0;
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPlayerPawn) return 0;
        return ReadMemory<int>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
    };

    // Kendi Takımımız
    cs2["GetLocalPlayerTeam"] = []() -> int {
        if (!clientAddress) return 0;
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPlayerPawn) return 0;
        return ReadMemory<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
    };

    // Kendi Pozisyonumuz
    cs2["GetLocalPlayerPos"] = []() -> std::tuple<float, float, float> {
        if (!clientAddress) return { 0.0f, 0.0f, 0.0f };
        uintptr_t localPlayerPawn = ReadMemory<uintptr_t>(clientAddress + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPlayerPawn) return { 0.0f, 0.0f, 0.0f };
        Vector3 pos = ReadMemory<Vector3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
        return { pos.x, pos.y, pos.z };
    };

    // Gelişmiş CS2 Entity List Taraması
    cs2["GetPlayers"] = []() -> sol::as_table_t<std::vector<LuaPlayer>> {
        std::vector<LuaPlayer> players;
        if (!clientAddress || !hProcess) return sol::as_table(players);

        uintptr_t entityList = ReadMemory<uintptr_t>(clientAddress + cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) return sol::as_table(players);

        uintptr_t localPlayerController = ReadMemory<uintptr_t>(clientAddress + cs2_dumper::offsets::client_dll::dwLocalPlayerController);

        for (int i = 0; i < 64; ++i) {
            uintptr_t listEntry = ReadMemory<uintptr_t>(entityList + 0x10 + 8 * ((i & 0x7FFF) >> 9));
            if (!listEntry) continue;

            uintptr_t playerController = ReadMemory<uintptr_t>(listEntry + 120 * (i & 0x1FF));
            if (!playerController) continue;

            char nameBuf[128] = { 0 };
            ReadProcessMemory(hProcess, (LPCVOID)(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName), nameBuf, sizeof(nameBuf) - 1, NULL);
            std::string playerName(nameBuf);
            if (playerName.empty()) continue;

            uint32_t pawnHandle = ReadMemory<uint32_t>(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
            if (!pawnHandle) continue;

            uintptr_t listEntryPawn = ReadMemory<uintptr_t>(entityList + 0x8 * ((pawnHandle & 0x7FFF) >> 9) + 0x10);
            if (!listEntryPawn) continue;

            uintptr_t playerPawn = ReadMemory<uintptr_t>(listEntryPawn + 120 * (pawnHandle & 0x1FF));
            if (!playerPawn) continue;

            int health = ReadMemory<int>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            uint8_t team = ReadMemory<uint8_t>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            uint8_t lifeState = ReadMemory<uint8_t>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState);
            Vector3 pos = ReadMemory<Vector3>(playerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);

            LuaPlayer lp;
            lp.name = playerName;
            lp.health = health;
            lp.team = team;
            lp.is_alive = (lifeState == 0 && health > 0);
            lp.x = pos.x;
            lp.y = pos.y;
            lp.z = pos.z;
            lp.is_local = (playerController == localPlayerController);

            players.push_back(lp);
        }
        return sol::as_table(players); // ipairs döngüsü için sol::as_table sarmalı şarttır
    };

    // Güvenli Gecikme Uyku Modülü
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
        LogToConsoleThreadSafe("[LUA HATASI] Bilinmeyen bir hata meydana geldi.");
    }

    isScriptRunning = false;
}

// Thread-Safe Win32 Pencere Mesaj Yöneticisi
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hEditBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "-- Lua scriptinizi buraya yazabilirsiniz\nwhile true do\n    local players = CS2.GetPlayers()\n    for _, p in ipairs(players) do\n        if p.is_alive and not p.is_local then\n            print(\"DUSMAN: \" .. p.name .. \" | CAN: \" .. p.health)\n        end\n    end\n    CS2.Sleep(2000)\nend",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            10, 35, 400, 390, hwnd, (HMENU)IDC_MAIN_EDIT, GetModuleHandle(NULL), NULL);

        CreateWindowEx(NULL, "BUTTON", "Scripti Calistir", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 435, 195, 40, hwnd, (HMENU)IDC_EXECUTE_BTN, GetModuleHandle(NULL), NULL);
        CreateWindowEx(NULL, "BUTTON", "Scripti Durdur", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 215, 435, 195, 40, hwnd, (HMENU)IDC_STOP_BTN, GetModuleHandle(NULL), NULL);

        hApiKeyInput = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 430, 30, 390, 25, hwnd, (HMENU)IDC_API_KEY_INPUT, GetModuleHandle(NULL), NULL);
        hAIInputBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "Oyundaki tum canli dusmanlari tara ve isimlerini ekrana bastir.", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 430, 85, 390, 85, hwnd, (HMENU)IDC_AI_INPUT, GetModuleHandle(NULL), NULL);
        CreateWindowEx(NULL, "BUTTON", "Yapay Zekayla Kod Uret", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 430, 175, 390, 35, hwnd, (HMENU)IDC_GENERATE_BTN, GetModuleHandle(NULL), NULL);

        hConsoleBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 430, 235, 390, 240, hwnd, (HMENU)IDC_CONSOLE_OUTPUT, GetModuleHandle(NULL), NULL);
        
        SendMessage(hConsoleBox, EM_LIMITTEXT, 0, 0);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EXECUTE_BTN) {
            if (isScriptRunning) {
                MessageBoxA(hwnd, "Zaten calisan aktif bir script mevcut!", "Bilgi", MB_OK | MB_ICONWARNING);
                break;
            }

            int length = GetWindowTextLength(hEditBox);
            if (length > 0) {
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
                LogToConsoleThreadSafe("[Sistem] Durdurma sinyali iletildi...");
            }
        }
        else if (LOWORD(wParam) == IDC_GENERATE_BTN) {
            if (isAiRequestRunning) break;

            int keyLength = GetWindowTextLength(hApiKeyInput);
            int promptLength = GetWindowTextLength(hAIInputBox);

            if (keyLength <= 0 || promptLength <= 0) {
                MessageBoxA(hwnd, "API Key veya Prompt girdisi bos birakilamaz!", "Hata", MB_OK | MB_ICONERROR);
                break;
            }

            std::string promptText(promptLength + 1, '\0');
            GetWindowTextA(hAIInputBox, &promptText[0], promptLength + 1);
            promptText.resize(promptLength);

            std::string apiKey(keyLength + 1, '\0');
            GetWindowTextA(hApiKeyInput, &apiKey[0], keyLength + 1);
            apiKey.resize(keyLength);

            SetWindowTextA(GetDlgItem(hwnd, IDC_GENERATE_BTN), "Yapay Zeka Script Kodluyor...");
            EnableWindow(GetDlgItem(hwnd, IDC_GENERATE_BTN), FALSE);

            if (aiWorkerThread.joinable()) aiWorkerThread.join();
            aiWorkerThread = std::thread(RequestCodeFromGroqThread, promptText, apiKey);
            aiWorkerThread.detach();
        }
        break;

    case WM_APP_LOG: {
        std::string* msgPtr = (std::string*)wParam;
        if (msgPtr) {
            int currentLength = GetWindowTextLength(hConsoleBox);
            if (currentLength > 50000) { 
                SetWindowTextA(hConsoleBox, "[Sistem] Bellek tasmamasi için konsol temizlendi...\r\n");
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
        LogToConsoleThreadSafe("[Sistem] Yapay zeka kodu basariyla sol panele yukledi!");
        break;
    }

    case WM_APP_AI_ERROR: {
        std::string* errPtr = (std::string*)wParam;
        if (errPtr) {
            LogToConsoleThreadSafe(*errPtr);
            delete errPtr;
        }
        SetWindowTextA(GetDlgItem(hwnd, IDC_GENERATE_BTN), "Yapay Zekayla Kod Uret");
        EnableWindow(GetDlgItem(hwnd, IDC_GENERATE_BTN), TRUE);
        break;
    }

    case WM_DESTROY:
        isScriptRunning = false;
        isAiRequestRunning = false;
        
        // Thread'leri agresif bir şekilde sonlandırıp join ediyoruz
        if (luaWorkerThread.joinable()) luaWorkerThread.join();
        if (aiWorkerTh
