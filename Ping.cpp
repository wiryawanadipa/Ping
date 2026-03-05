#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>
#include <ctime>
#include <iomanip>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <process.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#define COLOR_WHITE  7
#define COLOR_GRAY   8
#define COLOR_GREEN  10
#define COLOR_RED    12
#define COLOR_YELLOW 14

int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
std::string g_logBatch = "";
bool g_loggingEnabled = false;

void SetColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (WORD)color);
}

void ForceSaveLog() {
    if (!g_loggingEnabled || g_logBatch.empty()) return;

    std::time_t now = std::time(nullptr);
    struct tm ltm;
    localtime_s(&ltm, &now);

    std::filesystem::create_directories("logs");
    std::ostringstream fn;
    fn << "logs/ping_" << std::put_time(&ltm, "%Y_%m_%d") << ".txt";

    std::ofstream logFile(fn.str(), std::ios::app);
    if (logFile.is_open()) {
        logFile << g_logBatch;
        logFile.close();
    }
    g_logBatch.clear();
}

void SaveWindowSettings() {
    HWND hWnd = GetConsoleWindow();
    RECT rect;
    if (GetWindowRect(hWnd, &rect)) {
        int currentW = rect.right - rect.left;
        int currentH = rect.bottom - rect.top;
        if (rect.left != lastX || rect.top != lastY || currentW != lastW || currentH != lastH) {
            WritePrivateProfileStringA("Window", "X", std::to_string(rect.left).c_str(), ".\\settings.ini");
            WritePrivateProfileStringA("Window", "Y", std::to_string(rect.top).c_str(), ".\\settings.ini");
            WritePrivateProfileStringA("Window", "W", std::to_string(currentW).c_str(), ".\\settings.ini");
            WritePrivateProfileStringA("Window", "H", std::to_string(currentH).c_str(), ".\\settings.ini");
        }
    }
}

void EnsureSettingsExist() {
    const char* path = ".\\settings.ini";
    bool fileNeedsFullWrite = false;

    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        fileNeedsFullWrite = true;
    }
    else {
        char buffer[255];
        if (GetPrivateProfileStringA("Window", "X", "", buffer, 255, path) == 0) {
            fileNeedsFullWrite = true;
        }
    }

    if (fileNeedsFullWrite) {
        std::ofstream outfile(path, std::ios::trunc);
        outfile << "; ==========================================================\n"
            << "; PING UTILITY - CONFIGURATION GUIDE\n"
            << "; ==========================================================\n"
            << "; COLOR GUIDE:\n"
            << "; - WHITE  : Normal latency (below warning threshold)\n"
            << "; - YELLOW : High latency (exceeds warning threshold)\n"
            << "; - RED    : Request Timed Out (RTO) or Resolve Error\n"
            << "; ==========================================================\n"
            << "; EXITING: Use [ALT + F4] or the 'X' button. \n"
            << "; Position and size are saved automatically on exit.\n"
            << "; ==========================================================\n\n"
            << "[Network]\n"
            << "; Target destination (e.g., 8.8.8.8, 1.1.1.1, or google.com)\n"
            << "IP=1.1.1.1\n"
            << "; Time to wait between pings in milliseconds (1000 = 1 second)\n"
            << "delay=1000\n"
            << "; The 'Yellow Alert' limit. If ping is higher than this, text turns yellow.\n"
            << "warning=50\n\n"
            << "[Logging]\n"
            << "; Set to 'true' to save results to the /logs/ folder\n"
            << "enabled=false\n"
            << "; Number of pings to buffer before writing to the file (prevents lag)\n"
            << "log_interval=30\n\n"
            << "[Window]\n"
            << "; Screen coordinates (X, Y) and window dimensions (W, H)\n"
            << "; NOTE: These values AUTO-SAVE every time the program is closed.\n"
            << "X=200\n"
            << "Y=200\n"
            << "W=188\n"
            << "H=535\n";
        outfile.close();
    }

    char buf[255];
    if (GetPrivateProfileStringA("Logging", "enabled", "", buf, 255, path) == 0)
        WritePrivateProfileStringA("Logging", "enabled", "false", path);
    if (GetPrivateProfileStringA("Logging", "log_interval", "", buf, 255, path) == 0)
        WritePrivateProfileStringA("Logging", "log_interval", "30", path);
}

void LoadWindowSettings() {
    lastX = GetPrivateProfileIntA("Window", "X", CW_USEDEFAULT, ".\\settings.ini");
    lastY = GetPrivateProfileIntA("Window", "Y", CW_USEDEFAULT, ".\\settings.ini");
    lastW = GetPrivateProfileIntA("Window", "W", 188, ".\\settings.ini");
    lastH = GetPrivateProfileIntA("Window", "H", 535, ".\\settings.ini");
    if (lastX != CW_USEDEFAULT) MoveWindow(GetConsoleWindow(), lastX, lastY, lastW, lastH, TRUE);
}

void HideFromTaskbar(HWND hWnd) {
    long exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);
}

BOOL WINAPI ConsoleHandler(DWORD dwType) {
    if (dwType == CTRL_CLOSE_EVENT) {
        ForceSaveLog();
        SaveWindowSettings();
        return TRUE;
    }
    return FALSE;
}

void SetupConsole() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo = { 100, FALSE };
    SetConsoleCursorInfo(hOut, &cursorInfo);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, (mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
}

unsigned long ResolveHostname(const std::string& hostname) {
    addrinfo hints = { 0 }, * res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0 || res == nullptr)
        return INADDR_NONE;
    unsigned long ip = ((sockaddr_in*)res->ai_addr)->sin_addr.S_un.S_addr;
    freeaddrinfo(res);
    return ip;
}

int main() {
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\MyUniquePingUtilityMutex");

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    HWND hWndInit = GetConsoleWindow();
    char className[256];
    GetClassNameA(hWndInit, className, sizeof(className));

    if (std::string(className) != "ConsoleWindowClass") {
        char exepath[MAX_PATH];
        GetModuleFileNameA(NULL, exepath, MAX_PATH);
        _spawnl(_P_NOWAIT, "C:\\Windows\\System32\\conhost.exe", "conhost.exe", exepath, NULL);
        return 0;
    }

    EnsureSettingsExist();
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    LoadWindowSettings();
    HideFromTaskbar(GetConsoleWindow());
    SetupConsole();

    SetConsoleTitleA("PING");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    char buffer[255];
    GetPrivateProfileStringA("Network", "IP", "1.1.1.1", buffer, 255, ".\\settings.ini");
    std::string targetStr = buffer;

    int pingDelay = GetPrivateProfileIntA("Network", "delay", 1000, ".\\settings.ini");
    int warningLimit = GetPrivateProfileIntA("Network", "warning", 50, ".\\settings.ini");

    GetPrivateProfileStringA("Logging", "enabled", "false", buffer, 255, ".\\settings.ini");
    g_loggingEnabled = (_stricmp(buffer, "true") == 0);
    int logInterval = GetPrivateProfileIntA("Logging", "log_interval", 30, ".\\settings.ini");

    unsigned long ipaddr = ResolveHostname(targetStr);
    if (ipaddr == INADDR_NONE) {
        SetColor(COLOR_RED);    std::cerr << " Resolve Error.\n Check settings.ini.\n Error Code: " << WSAGetLastError() << std::endl;
        SetColor(COLOR_WHITE);  std::system("pause > nul");
        WSACleanup();
        return 1;
    }

    HANDLE hIcmpFile = IcmpCreateFile();
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + 32 + 8;
    LPVOID replyBuffer = malloc(replySize);
    if (replyBuffer == NULL) {
        IcmpCloseHandle(hIcmpFile);
        WSACleanup();
        return 1;
    }
    char sendData[] = "PingData";

    int logCounter = 0;

    SetColor(COLOR_WHITE);
    std::cout << " =================" << std::endl;
    SetColor(COLOR_GREEN); std::cout << "  " << targetStr << "\n";
    SetColor(COLOR_WHITE); std::cout << " =================\n" << std::endl;

    while (true) {
        DWORD dwRetVal = IcmpSendEcho(hIcmpFile, ipaddr, sendData, (WORD)sizeof(sendData), NULL, replyBuffer, replySize, 1000);

        std::time_t now = std::time(nullptr);
        struct tm ltm;
        localtime_s(&ltm, &now);

        SetColor(COLOR_WHITE);  std::cout << std::put_time(&ltm, " %H");
        SetColor(COLOR_GRAY);   std::cout << ":";
        SetColor(COLOR_WHITE);  std::cout << std::put_time(&ltm, "%M");
        SetColor(COLOR_GRAY);   std::cout << ":";
        SetColor(COLOR_WHITE);  std::cout << std::put_time(&ltm, "%S    ");

        std::string resultText;
        if (dwRetVal != 0) {
            PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
            int rtt = (int)pEchoReply->RoundTripTime;
            resultText = std::to_string(rtt);
            if (rtt > warningLimit) SetColor(COLOR_YELLOW);
            std::cout << resultText << "\n";
        }
        else {
            resultText = "RTO";
            SetColor(COLOR_RED);    std::cout << resultText << "\n";
        }

        if (g_loggingEnabled) {
            std::ostringstream ss;
            ss << std::put_time(&ltm, "%H:%M:%S") << " : " << resultText << "\n";
            g_logBatch += ss.str();
            logCounter++;

            if (logCounter >= logInterval) {
                ForceSaveLog();
                logCounter = 0;
            }
        }

        Sleep(pingDelay);
    }

    free(replyBuffer);
    IcmpCloseHandle(hIcmpFile);
    WSACleanup();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}