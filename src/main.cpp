#include <windows.h>
#include <windowsx.h>
#include <winsvc.h>
#include <d3d11.h>
#include <netfw.h>
#include <shlobj.h>
#include <tchar.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

enum class ServiceMarker
{
    Unknown,
    InitiallyActive,
    ActivatedNow,
    StillDisabled,
    FirmwareAction
};

struct ServiceEntry
{
    std::string displayName;
    std::vector<std::string> serviceNames;
    std::string activeServiceName;
    std::string status;
    ImVec4 statusColor;
    ServiceMarker marker = ServiceMarker::Unknown;
    std::string markerTooltip;
};

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;
static bool g_mainWindowSized = false;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CenterWindow(HWND hwnd, int width, int height)
{
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, 24, 24), TRUE);
}

static std::wstring ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    return result;
}

static bool EqualsIgnoreCase(const std::string& left, const char* right)
{
    return _stricmp(left.c_str(), right) == 0;
}

static bool IsActiveStatus(const std::string& status)
{
    return status == "Ativado" || status == "Scan concluido" || status == "Sysmon instalado" || status == "Sysmon atualizado";
}

static bool IsDisabledOrFailedStatus(const std::string& status)
{
    return status == "Desativado" ||
        status == "Nao encontrado" ||
        status == "Nao automatizavel" ||
        status == "Firmware/UEFI" ||
        status.find("Erro") == 0;
}

static std::wstring QuoteArgument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'"')
            quoted += L"\\\"";
        else
            quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

static std::string CombineMessages(const std::vector<std::string>& messages)
{
    std::string combined;
    for (const std::string& message : messages)
    {
        if (message.empty())
            continue;
        if (!combined.empty())
            combined += " | ";
        combined += message;
    }
    return combined.empty() ? "Concluido" : combined;
}

static std::string ToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};

    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

static std::string UrlEncodeUtf8(const std::string& value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char ch : value)
    {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')
        {
            encoded.push_back(static_cast<char>(ch));
        }
        else if (ch == ' ')
        {
            encoded.push_back('+');
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(hex[(ch >> 4) & 0x0F]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

static std::wstring ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName)
{
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || size <= sizeof(wchar_t))
        return {};

    std::wstring value(size / sizeof(wchar_t), L'\0');
    status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, value.data(), &size);
    if (status != ERROR_SUCCESS)
        return {};

    while (!value.empty() && value.back() == L'\0')
        value.pop_back();
    return value;
}

static std::wstring MotherboardSearchText()
{
    constexpr const wchar_t* biosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    std::wstring manufacturer = ReadRegistryString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardManufacturer");
    std::wstring product = ReadRegistryString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardProduct");

    if (manufacturer.empty())
        manufacturer = ReadRegistryString(HKEY_LOCAL_MACHINE, biosKey, L"SystemManufacturer");
    if (product.empty())
        product = ReadRegistryString(HKEY_LOCAL_MACHINE, biosKey, L"SystemProductName");

    std::wstring query;
    if (!product.empty())
        query += product + L" ";
    if (!manufacturer.empty())
        query += manufacturer + L" ";
    query += L"como ativar secure boot";
    return query;
}

static void OpenSecureBootSearch()
{
    const std::string encodedQuery = UrlEncodeUtf8(ToUtf8(MotherboardSearchText()));
    const std::wstring url = ToWide("https://www.google.com/search?q=" + encodedQuery);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static bool CurrentExecutableIsNamed(const wchar_t* expectedName)
{
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        return false;

    const wchar_t* fileName = wcsrchr(path, L'\\');
    fileName = fileName ? fileName + 1 : path;
    return _wcsicmp(fileName, expectedName) == 0;
}

static bool IsVisualTestMode()
{
    return CurrentExecutableIsNamed(L"teste.exe");
}

static std::string ServiceStatusName(DWORD status)
{
    switch (status)
    {
    case SERVICE_RUNNING:
        return "Ativado";
    case SERVICE_STOPPED:
        return "Desativado";
    case SERVICE_PAUSED:
        return "Pausado";
    case SERVICE_START_PENDING:
        return "Ativando...";
    case SERVICE_STOP_PENDING:
        return "Desativando...";
    case SERVICE_PAUSE_PENDING:
        return "Pausando...";
    case SERVICE_CONTINUE_PENDING:
        return "Retomando...";
    default:
        return "Desconhecido";
    }
}

static ImVec4 ServiceStatusColor(const std::string& status)
{
    if (IsActiveStatus(status))
        return ImVec4(0.20f, 0.82f, 0.46f, 1.00f);
    if (status == "Desativado")
        return ImVec4(0.95f, 0.42f, 0.36f, 1.00f);
    if (status == "Pausado" || status == "Protecao parcial")
        return ImVec4(0.98f, 0.70f, 0.22f, 1.00f);
    if (status.find("Erro") == 0 || status == "Nao encontrado" || status == "TPM nao e 2.0")
        return ImVec4(0.98f, 0.32f, 0.39f, 1.00f);
    if (status == "Nao automatizavel" || status == "Ative na BIOS/UEFI" || status == "UEFI indisponivel" || status == "TPM 2.0 precisa inicializar" || status == "TPM requer firmware")
        return ImVec4(0.98f, 0.70f, 0.22f, 1.00f);
    return ImVec4(0.45f, 0.64f, 0.95f, 1.00f);
}

static std::vector<std::string> MakeAliases(const std::string& serviceName)
{
    if (EqualsIgnoreCase(serviceName, "TPM") || EqualsIgnoreCase(serviceName, "TPM 2.0"))
        return {"TBS"};
    if (EqualsIgnoreCase(serviceName, "SysMon") || EqualsIgnoreCase(serviceName, "Sysmon"))
        return {"Sysmon64", "Sysmon"};
    if (EqualsIgnoreCase(serviceName, "Windows Firewall"))
        return {"mpssvc"};
    if (EqualsIgnoreCase(serviceName, "Windows Security"))
        return {"SecurityHealthService", "wscsvc", "WinDefend", "WdNisSvc"};
    if (EqualsIgnoreCase(serviceName, "Defender Quick Scan"))
        return {};
    return {serviceName};
}

static ServiceEntry MakeServiceEntry(const std::string& displayName, std::vector<std::string> aliases = {})
{
    if (aliases.empty())
        aliases = MakeAliases(displayName);
    return {displayName, aliases, "", "Verificando...", ImVec4(0.45f, 0.64f, 0.95f, 1.00f)};
}

static std::vector<ServiceEntry> RequiredServices()
{
    return {
        MakeServiceEntry("PcaSvc"),
        MakeServiceEntry("DPS"),
        MakeServiceEntry("DiagTrack"),
        MakeServiceEntry("SysMain"),
        MakeServiceEntry("SysMon", {"Sysmon64", "Sysmon"}),
        MakeServiceEntry("EventLog"),
        {"Secure Boot", {}, "", "Verificando...", ImVec4(0.45f, 0.64f, 0.95f, 1.00f)},
        MakeServiceEntry("TPM 2.0", {"TBS"}),
        MakeServiceEntry("Windows Firewall", {"mpssvc"}),
        MakeServiceEntry("Windows Security", {"SecurityHealthService", "wscsvc", "WinDefend", "WdNisSvc"}),
        MakeServiceEntry("Defender Quick Scan", {}),
        {"NTFS Journal", {}, "", "Verificando...", ImVec4(0.45f, 0.64f, 0.95f, 1.00f)}
    };
}

static std::string QueryTpm20Status();
static std::string EnableTpm20(ServiceEntry& entry);
static std::string QueryNtfsJournalStatus();
static std::string EnsureNtfsJournalOnFixedDrives();
static std::string QuerySecureBootStatus();
static std::string QueryWindowsFirewallStatus();
static std::string EnableWindowsFirewall(ServiceEntry& entry);
static std::string QueryWindowsSecurityStatus();
static std::string EnableWindowsSecurity(ServiceEntry& entry);
static std::string RunDefenderQuickScan(ServiceEntry& entry);
static std::string EnsureSysmonFilesExtracted();
static std::string EnsureSysmonInstalled(ServiceEntry& entry);

static SC_HANDLE OpenFirstExistingService(SC_HANDLE manager, const std::vector<std::string>& serviceNames, DWORD access, std::string& activeName)
{
    for (const std::string& serviceName : serviceNames)
    {
        if (serviceName.empty())
            continue;

        const std::wstring wideName = ToWide(serviceName);
        SC_HANDLE service = OpenServiceW(manager, wideName.c_str(), access);
        if (service)
        {
            activeName = serviceName;
            return service;
        }
    }
    return nullptr;
}

static bool IsServiceAutoStart(SC_HANDLE service)
{
    DWORD bytesNeeded = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytesNeeded == 0)
        return false;

    std::vector<BYTE> buffer(bytesNeeded);
    QUERY_SERVICE_CONFIGW* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(service, config, bytesNeeded, &bytesNeeded))
        return false;

    return config->dwStartType == SERVICE_AUTO_START;
}

static bool EnsureServiceAutoStart(SC_HANDLE service)
{
    if (IsServiceAutoStart(service))
        return true;

    return ChangeServiceConfigW(
        service,
        SERVICE_NO_CHANGE,
        SERVICE_AUTO_START,
        SERVICE_NO_CHANGE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr) != FALSE;
}

static bool EnsureServiceAutoStartByName(SC_HANDLE manager, const std::string& serviceName)
{
    const std::wstring wideName = ToWide(serviceName);
    SC_HANDLE configService = OpenServiceW(manager, wideName.c_str(), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (configService)
    {
        const bool ok = EnsureServiceAutoStart(configService);
        CloseServiceHandle(configService);
        return ok;
    }

    configService = OpenServiceW(manager, wideName.c_str(), SERVICE_QUERY_CONFIG);
    if (configService)
    {
        const bool alreadyAuto = IsServiceAutoStart(configService);
        CloseServiceHandle(configService);
        return alreadyAuto;
    }

    return false;
}

static std::string WaitForServiceRunning(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
        return "Erro ao verificar";

    if (status.dwCurrentState == SERVICE_RUNNING)
        return "Ativado";

    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
        return "Erro ao ativar";

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        Sleep(350);
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
            break;
        if (status.dwCurrentState == SERVICE_RUNNING)
            return "Ativado";
    }

    return ServiceStatusName(status.dwCurrentState);
}

static std::string QueryServiceStatusText(ServiceEntry& entry)
{
    if (EqualsIgnoreCase(entry.displayName, "TPM 2.0"))
    {
        entry.activeServiceName = "TBS";
        return QueryTpm20Status();
    }

    if (EqualsIgnoreCase(entry.displayName, "NTFS Journal"))
        return QueryNtfsJournalStatus();

    if (EqualsIgnoreCase(entry.displayName, "Secure Boot"))
        return QuerySecureBootStatus();

    if (EqualsIgnoreCase(entry.displayName, "Windows Firewall"))
        return QueryWindowsFirewallStatus();

    if (EqualsIgnoreCase(entry.displayName, "Windows Security"))
        return QueryWindowsSecurityStatus();

    if (EqualsIgnoreCase(entry.displayName, "Defender Quick Scan"))
        return "Pendente";

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return "Erro: execute como administrador";

    if (entry.serviceNames.empty())
    {
        CloseServiceHandle(manager);
        return "Nao automatizavel";
    }

    std::string activeName;
    SC_HANDLE service = OpenFirstExistingService(manager, entry.serviceNames, SERVICE_QUERY_STATUS, activeName);
    if (!service)
    {
        CloseServiceHandle(manager);
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? "Nao encontrado" : "Erro ao abrir";
    }
    entry.activeServiceName = activeName;

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const BOOL ok = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded);

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!ok)
        return "Erro ao verificar";

    return ServiceStatusName(status.dwCurrentState);
}

static std::string EnableAndStartWindowsService(ServiceEntry& entry)
{
    if (EqualsIgnoreCase(entry.displayName, "TPM 2.0"))
        return EnableTpm20(entry);

    if (EqualsIgnoreCase(entry.displayName, "NTFS Journal"))
    {
        EnsureNtfsJournalOnFixedDrives();
        return QueryNtfsJournalStatus();
    }

    if (EqualsIgnoreCase(entry.displayName, "Secure Boot"))
        return QuerySecureBootStatus();

    if (EqualsIgnoreCase(entry.displayName, "Windows Firewall"))
        return EnableWindowsFirewall(entry);

    if (EqualsIgnoreCase(entry.displayName, "Windows Security"))
        return EnableWindowsSecurity(entry);

    if (EqualsIgnoreCase(entry.displayName, "Defender Quick Scan"))
        return RunDefenderQuickScan(entry);

    if (EqualsIgnoreCase(entry.displayName, "SysMon") || EqualsIgnoreCase(entry.displayName, "Sysmon"))
    {
        const std::string sysmonStatus = EnsureSysmonInstalled(entry);
        if (sysmonStatus.find("Erro") == 0)
            return sysmonStatus;
    }

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return "Erro: execute como administrador";

    if (entry.serviceNames.empty())
    {
        CloseServiceHandle(manager);
        return "Firmware/UEFI";
    }

    std::string activeName;
    SC_HANDLE service = OpenFirstExistingService(manager, entry.serviceNames, SERVICE_QUERY_STATUS, activeName);
    if (!service)
    {
        CloseServiceHandle(manager);
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? "Nao encontrado" : "Erro ao abrir";
    }
    entry.activeServiceName = activeName;

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return "Erro ao verificar";
    }

    if (!EnsureServiceAutoStartByName(manager, activeName))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return "Erro: automatico";
    }

    if (status.dwCurrentState == SERVICE_RUNNING)
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return "Ativado";
    }

    CloseServiceHandle(service);
    service = OpenFirstExistingService(manager, {activeName}, SERVICE_QUERY_STATUS | SERVICE_START, activeName);
    if (!service)
    {
        CloseServiceHandle(manager);
        return "Erro ao abrir";
    }

    const std::string startStatus = WaitForServiceRunning(service);

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return startStatus;
}

static DWORD RunProcessExitCode(
    const std::wstring& executablePath,
    const std::wstring& arguments,
    const std::wstring& workingDirectory = {},
    DWORD timeoutMs = INFINITE)
{
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    std::wstring mutableCommandLine = QuoteArgument(executablePath);
    if (!arguments.empty())
        mutableCommandLine += L" " + arguments;

    const BOOL created = CreateProcessW(
        executablePath.c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup,
        &process);

    if (!created)
        return 999;

    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 124);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 124;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

static DWORD RunCommandLineExitCode(const std::wstring& commandLine, DWORD timeoutMs = INFINITE)
{
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    std::wstring mutableCommandLine = commandLine;
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);

    if (!created)
        return 999;

    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 124);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 124;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

static bool RunHiddenProcess(const std::wstring& commandLine)
{
    return RunCommandLineExitCode(commandLine) == 0;
}

static std::wstring PowerShellExePath()
{
    wchar_t windowsPath[MAX_PATH]{};
    if (GetWindowsDirectoryW(windowsPath, MAX_PATH) == 0)
        return L"powershell.exe";

    std::wstring path = windowsPath;
    path += L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    return path;
}

static DWORD RunPowerShellExitCode(const std::wstring& command, DWORD timeoutMs = 120000)
{
    return RunProcessExitCode(
        PowerShellExePath(),
        L"-NoProfile -NonInteractive -Command " + QuoteArgument(command),
        {},
        timeoutMs);
}

static std::string QueryTpm20Status()
{
    const DWORD code = RunPowerShellExitCode(
        L"$ErrorActionPreference='SilentlyContinue'; "
        L"$t=Get-Tpm; "
        L"$w=Get-CimInstance -Namespace root\\CIMV2\\Security\\MicrosoftTpm -ClassName Win32_Tpm; "
        L"if($t.TpmPresent -and $t.TpmReady -and ($w.SpecVersion -match '2.0')) { exit 0 } "
        L"elseif($t.TpmPresent -and ($w.SpecVersion -match '2.0')) { exit 2 } "
        L"elseif($t.TpmPresent) { exit 3 } "
        L"else { exit 4 }");

    if (code == 0)
        return "Ativado";
    if (code == 2)
        return "TPM 2.0 precisa inicializar";
    if (code == 3)
        return "TPM nao e 2.0";
    if (code == 4)
        return "Firmware/UEFI";
    return "Erro ao verificar";
}

static std::string QuerySecureBootStatus()
{
    if (IsVisualTestMode())
        return "Desativado";

    const DWORD code = RunPowerShellExitCode(
        L"$ErrorActionPreference='Stop'; "
        L"try { if (Confirm-SecureBootUEFI) { exit 0 } else { exit 1 } } "
        L"catch { exit 2 }");

    if (code == 0)
        return "Ativado";
    if (code == 1)
        return "Desativado";
    return "Ative na BIOS/UEFI";
}

static std::string EnableTpm20(ServiceEntry& entry)
{
    ServiceEntry tbsService = MakeServiceEntry("TPM Base Services", {"TBS"});
    const std::string serviceStatus = EnableAndStartWindowsService(tbsService);
    entry.activeServiceName = tbsService.activeServiceName.empty() ? "TBS" : tbsService.activeServiceName;
    const DWORD initCode = RunPowerShellExitCode(
        L"$ErrorActionPreference='SilentlyContinue'; "
        L"if(Get-Command Initialize-Tpm) { Initialize-Tpm -AllowClear:$false | Out-Null }; "
        L"exit 0");

    const std::string tpmStatus = QueryTpm20Status();
    if (tpmStatus == "Ativado")
        return "Ativado";
    if (initCode != 0)
        return serviceStatus == "Ativado" ? "TPM requer firmware" : serviceStatus;
    return tpmStatus;
}

static std::vector<std::wstring> FixedDriveRoots()
{
    std::vector<std::wstring> roots;
    const DWORD mask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
    {
        if ((mask & (1 << (letter - L'A'))) == 0)
            continue;

        wchar_t root[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            continue;

        wchar_t fileSystemName[MAX_PATH]{};
        if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fileSystemName, MAX_PATH))
            continue;

        if (_wcsicmp(fileSystemName, L"NTFS") == 0)
            roots.push_back(std::wstring(root, 2));
    }
    return roots;
}

static std::string EnsureNtfsJournalOnFixedDrives()
{
    int enabled = 0;
    int alreadyEnabled = 0;
    int failed = 0;

    for (const std::wstring& drive : FixedDriveRoots())
    {
        if (RunHiddenProcess(L"fsutil usn queryjournal " + drive))
        {
            ++alreadyEnabled;
            continue;
        }

        if (RunHiddenProcess(L"fsutil usn createjournal m=0x800000 a=0x100000 " + drive))
            ++enabled;
        else
            ++failed;
    }

    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "NTFS Journal: %d ativado(s), %d ja ativo(s), %d falha(s)", enabled, alreadyEnabled, failed);
    return buffer;
}

static std::string QueryNtfsJournalStatus()
{
    const std::vector<std::wstring> drives = FixedDriveRoots();
    if (drives.empty())
        return "Nao encontrado";

    int enabled = 0;
    for (const std::wstring& drive : drives)
    {
        if (RunHiddenProcess(L"fsutil usn queryjournal " + drive))
            ++enabled;
    }

    if (enabled == static_cast<int>(drives.size()))
        return "Ativado";
    if (enabled == 0)
        return "Desativado";
    return "Parcial";
}

static bool ServiceExists(const std::vector<std::string>& serviceNames, std::string& activeName)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return false;

    SC_HANDLE service = OpenFirstExistingService(manager, serviceNames, SERVICE_QUERY_STATUS, activeName);
    if (!service)
    {
        CloseServiceHandle(manager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

struct SysmonFileSet
{
    std::wstring folder;
    std::wstring executable;
    std::wstring config;
    std::wstring eula;
};

static SysmonFileSet GetSysmonFileSet()
{
    PWSTR roamingPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roamingPath);
    if (FAILED(hr) || !roamingPath)
        return {};

    std::wstring folder = roamingPath;
    CoTaskMemFree(roamingPath);

    folder += L"\\Sysmon";
    return {
        folder,
        folder + L"\\Sysmon.exe",
        folder + L"\\cfg.xml",
        folder + L"\\Eula.txt"
    };
}

static bool EnsureDirectoryExists(const std::wstring& path)
{
    if (CreateDirectoryW(path.c_str(), nullptr))
        return true;

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool WriteResourceToFile(int resourceId, const std::wstring& outputPath)
{
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource)
        return false;

    HGLOBAL loadedResource = LoadResource(module, resource);
    if (!loadedResource)
        return false;

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0)
        return false;

    void* resourceData = LockResource(loadedResource);
    if (!resourceData)
        return false;

    HANDLE file = CreateFileW(
        outputPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, resourceData, resourceSize, &written, nullptr);
    CloseHandle(file);
    return ok && written == resourceSize;
}

static std::string EnsureSysmonFilesExtracted()
{
    const SysmonFileSet files = GetSysmonFileSet();
    if (files.folder.empty())
        return "Erro: AppData indisponivel";

    if (!EnsureDirectoryExists(files.folder))
        return "Erro: pasta Sysmon";

    if (!WriteResourceToFile(IDR_SYSMON_EXE, files.executable))
        return "Erro: Sysmon.exe";

    if (!WriteResourceToFile(IDR_CFG_XML, files.config))
        return "Erro: cfg.xml";

    if (!WriteResourceToFile(IDR_EULA_TXT, files.eula))
        return "Erro: Eula.txt";

    return "Sysmon assets prontos";
}

static std::string EnsureSysmonInstalled(ServiceEntry& entry)
{
    const std::string extractStatus = EnsureSysmonFilesExtracted();
    if (extractStatus.find("Erro") == 0)
        return extractStatus;

    const SysmonFileSet files = GetSysmonFileSet();
    std::string activeName;
    const bool alreadyInstalled = ServiceExists(entry.serviceNames, activeName);
    if (alreadyInstalled)
    {
        entry.activeServiceName = activeName;
        const DWORD updateCode = RunProcessExitCode(
            files.executable,
            L"-accepteula -c " + QuoteArgument(files.config),
            files.folder,
            120000);

        if (updateCode != 0)
            return "Erro: Sysmon config";

        return "Sysmon atualizado";
    }

    const DWORD installCode = RunProcessExitCode(
        files.executable,
        L"-accepteula -i " + QuoteArgument(files.config),
        files.folder,
        120000);

    if (installCode != 0)
        return "Erro: Sysmon install";

    if (ServiceExists(entry.serviceNames, activeName))
        entry.activeServiceName = activeName;

    return "Sysmon instalado";
}

static HRESULT CreateFirewallPolicy(INetFwPolicy2** policy)
{
    *policy = nullptr;
    return CoCreateInstance(
        __uuidof(NetFwPolicy2),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(policy));
}

static std::string QueryWindowsFirewallStatus()
{
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninitialize = SUCCEEDED(coHr);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE)
        return "Erro firewall COM";

    INetFwPolicy2* policy = nullptr;
    const HRESULT createHr = CreateFirewallPolicy(&policy);
    if (FAILED(createHr) || !policy)
    {
        if (mustUninitialize)
            CoUninitialize();
        return "Erro ao verificar";
    }

    const std::array<NET_FW_PROFILE_TYPE2, 3> profiles = {
        NET_FW_PROFILE2_DOMAIN,
        NET_FW_PROFILE2_PRIVATE,
        NET_FW_PROFILE2_PUBLIC
    };

    bool allEnabled = true;
    for (NET_FW_PROFILE_TYPE2 profile : profiles)
    {
        VARIANT_BOOL enabled = VARIANT_FALSE;
        if (FAILED(policy->get_FirewallEnabled(profile, &enabled)) || enabled == VARIANT_FALSE)
        {
            allEnabled = false;
            break;
        }
    }

    policy->Release();
    if (mustUninitialize)
        CoUninitialize();

    return allEnabled ? "Ativado" : "Desativado";
}

static std::string EnableWindowsFirewall(ServiceEntry& entry)
{
    ServiceEntry firewallService = MakeServiceEntry("mpssvc", {"mpssvc"});
    const std::string serviceStatus = EnableAndStartWindowsService(firewallService);
    entry.activeServiceName = firewallService.activeServiceName.empty() ? "mpssvc" : firewallService.activeServiceName;
    if (serviceStatus.find("Erro") == 0)
        return serviceStatus;

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninitialize = SUCCEEDED(coHr);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE)
        return "Erro firewall COM";

    INetFwPolicy2* policy = nullptr;
    const HRESULT createHr = CreateFirewallPolicy(&policy);
    if (FAILED(createHr) || !policy)
    {
        if (mustUninitialize)
            CoUninitialize();
        return "Erro firewall";
    }

    const std::array<NET_FW_PROFILE_TYPE2, 3> profiles = {
        NET_FW_PROFILE2_DOMAIN,
        NET_FW_PROFILE2_PRIVATE,
        NET_FW_PROFILE2_PUBLIC
    };

    bool ok = true;
    for (NET_FW_PROFILE_TYPE2 profile : profiles)
    {
        ok = SUCCEEDED(policy->put_FirewallEnabled(profile, VARIANT_TRUE)) && ok;
        ok = SUCCEEDED(policy->put_DefaultInboundAction(profile, NET_FW_ACTION_BLOCK)) && ok;
        ok = SUCCEEDED(policy->put_DefaultOutboundAction(profile, NET_FW_ACTION_ALLOW)) && ok;
        ok = SUCCEEDED(policy->put_NotificationsDisabled(profile, VARIANT_FALSE)) && ok;
    }

    policy->Release();
    if (mustUninitialize)
        CoUninitialize();

    if (!ok)
        return "Protecao parcial";

    return QueryWindowsFirewallStatus();
}

static std::string QueryWindowsSecurityStatus()
{
    const DWORD code = RunPowerShellExitCode(
        L"$ErrorActionPreference='SilentlyContinue'; "
        L"$s=Get-MpComputerStatus; "
        L"if($s.AntivirusEnabled -and $s.RealTimeProtectionEnabled) { exit 0 } "
        L"elseif($s.AntivirusEnabled) { exit 2 } "
        L"elseif($null -eq $s) { exit 4 } "
        L"else { exit 3 }");

    if (code == 0)
        return "Ativado";
    if (code == 2)
        return "Protecao parcial";
    if (code == 3)
        return "Desativado";
    if (code == 4)
        return "Nao encontrado";
    return "Erro ao verificar";
}

static std::string EnableWindowsSecurity(ServiceEntry& entry)
{
    std::vector<ServiceEntry> securityServices = {
        MakeServiceEntry("SecurityHealthService", {"SecurityHealthService"}),
        MakeServiceEntry("wscsvc", {"wscsvc"}),
        MakeServiceEntry("WinDefend", {"WinDefend"}),
        MakeServiceEntry("WdNisSvc", {"WdNisSvc"})
    };

    for (ServiceEntry& service : securityServices)
        EnableAndStartWindowsService(service);

    entry.activeServiceName = "WinDefend";

    RunPowerShellExitCode(
        L"$ErrorActionPreference='SilentlyContinue'; "
        L"Set-MpPreference "
        L"-DisableRealtimeMonitoring $false "
        L"-DisableBehaviorMonitoring $false "
        L"-DisableBlockAtFirstSeen $false "
        L"-DisableIOAVProtection $false "
        L"-DisableScriptScanning $false "
        L"-PUAProtection Enabled; "
        L"exit 0");

    return QueryWindowsSecurityStatus();
}

static std::string RunDefenderQuickScan(ServiceEntry& entry)
{
    entry.activeServiceName = "Start-MpScan";
    const std::string securityStatus = EnableWindowsSecurity(entry);
    entry.activeServiceName = "Start-MpScan";
    if (securityStatus.find("Erro") == 0 || securityStatus == "Nao encontrado")
        return securityStatus;

    const DWORD code = RunPowerShellExitCode(
        L"$ErrorActionPreference='Stop'; "
        L"Start-MpScan -ScanType QuickScan; "
        L"exit 0",
        1800000);

    if (code == 0)
        return "Scan concluido";
    if (code == 124)
        return "Erro: scan timeout";
    return "Erro: scan rapido";
}

static void ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(14.0f, 9.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 8.0f);
    style.ScrollbarSize = 12.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 16.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.63f, 0.72f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.06f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.08f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.09f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.16f, 0.29f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.08f, 0.17f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.06f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.02f, 0.06f, 0.10f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.50f, 0.83f, 0.02f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.62f, 0.95f, 0.05f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.39f, 0.70f, 0.01f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.06f, 0.15f, 0.23f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.14f, 0.30f, 0.41f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.29f, 0.39f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.04f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.03f, 0.08f, 0.13f, 0.62f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.05f, 0.11f, 0.16f, 0.72f);
}

static void DrawStatusPill(const ServiceEntry& service)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(service.status.c_str());
    const ImVec2 size(textSize.x + 22.0f, 28.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(service.statusColor.x, service.statusColor.y, service.statusColor.z, 0.16f));
    ImU32 fg = ImGui::ColorConvertFloat4ToU32(service.statusColor);

    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 14.0f);
    drawList->AddCircleFilled(ImVec2(pos.x + 12.0f, pos.y + 14.0f), 4.0f, fg);
    drawList->AddText(ImVec2(pos.x + 22.0f, pos.y + 6.0f), fg, service.status.c_str());
    ImGui::Dummy(size);
}

static void DrawComponentRow(const ServiceEntry& service, float width, float rowHeight, int index)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 rowMax(pos.x + width, pos.y + rowHeight);
    const ImU32 bg = (index % 2 == 0) ? IM_COL32(5, 23, 35, 238) : IM_COL32(7, 29, 43, 238);
    const ImU32 border = IM_COL32(38, 77, 100, 160);

    drawList->AddRectFilled(pos, rowMax, bg, 9.0f);
    drawList->AddRect(pos, rowMax, border, 9.0f, 0, 1.0f);

    std::string serviceLabel = service.displayName;
    if (!service.activeServiceName.empty() && _stricmp(service.displayName.c_str(), service.activeServiceName.c_str()) != 0)
        serviceLabel += "  [" + service.activeServiceName + "]";

    const ImVec2 labelSize = ImGui::CalcTextSize(serviceLabel.c_str());
    drawList->AddText(
        ImVec2(pos.x + 18.0f, pos.y + (rowHeight - labelSize.y) * 0.5f),
        IM_COL32(222, 233, 240, 255),
        serviceLabel.c_str());

    const bool active = IsActiveStatus(service.status);
    const bool showHelpButton = service.marker == ServiceMarker::FirmwareAction;
    float rightEdge = pos.x + width - 16.0f;

    if (showHelpButton)
    {
        const ImVec2 buttonSize(58.0f, 26.0f);
        const ImVec2 buttonPos(rightEdge - buttonSize.x, pos.y + (rowHeight - buttonSize.y) * 0.5f);
        const ImVec2 buttonMax(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y);
        const bool hovered = ImGui::IsMouseHoveringRect(buttonPos, buttonMax);
        const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const ImU32 buttonBg = hovered ? IM_COL32(96, 151, 20, 255) : IM_COL32(48, 78, 22, 240);
        const ImU32 buttonBorder = hovered ? IM_COL32(160, 220, 44, 255) : IM_COL32(100, 150, 42, 210);

        drawList->AddRectFilled(buttonPos, buttonMax, buttonBg, 7.0f);
        drawList->AddRect(buttonPos, buttonMax, buttonBorder, 7.0f);
        const char* buttonText = "BIOS";
        const ImVec2 buttonTextSize = ImGui::CalcTextSize(buttonText);
        drawList->AddText(
            ImVec2(buttonPos.x + (buttonSize.x - buttonTextSize.x) * 0.5f, buttonPos.y + 5.0f),
            IM_COL32(232, 245, 213, 255),
            buttonText);

        if (hovered)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Pesquisar guia de Secure Boot para sua placa-mae.");
            ImGui::EndTooltip();
        }

        if (clicked)
            OpenSecureBootSearch();

        rightEdge = buttonPos.x - 8.0f;
    }

    const ImVec2 textSize = ImGui::CalcTextSize(service.status.c_str());
    const bool showMarker = service.marker == ServiceMarker::InitiallyActive ||
        service.marker == ServiceMarker::ActivatedNow ||
        service.marker == ServiceMarker::StillDisabled;
    const ImVec2 pillSize(textSize.x + (showMarker ? 42.0f : 28.0f), 26.0f);
    const ImVec2 pillPos(rightEdge - pillSize.x, pos.y + (rowHeight - pillSize.y) * 0.5f);
    ImU32 pillBg = ImGui::ColorConvertFloat4ToU32(ImVec4(service.statusColor.x, service.statusColor.y, service.statusColor.z, active ? 0.10f : 0.15f));
    ImU32 fg = ImGui::ColorConvertFloat4ToU32(service.statusColor);

    drawList->AddRectFilled(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), pillBg, 14.0f);
    drawList->AddRect(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), ImGui::ColorConvertFloat4ToU32(ImVec4(service.statusColor.x, service.statusColor.y, service.statusColor.z, 0.28f)), 14.0f);

    if (showMarker)
    {
        const ImVec2 iconCenter(pillPos.x + 15.0f, pillPos.y + 13.0f);
        drawList->AddCircleFilled(iconCenter, 8.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(service.statusColor.x, service.statusColor.y, service.statusColor.z, 0.20f)));

        if (service.marker == ServiceMarker::InitiallyActive)
        {
            drawList->AddLine(ImVec2(iconCenter.x - 3.5f, iconCenter.y), ImVec2(iconCenter.x - 0.8f, iconCenter.y + 3.0f), fg, 1.8f);
            drawList->AddLine(ImVec2(iconCenter.x - 0.8f, iconCenter.y + 3.0f), ImVec2(iconCenter.x + 4.2f, iconCenter.y - 3.5f), fg, 1.8f);
        }
        else if (service.marker == ServiceMarker::ActivatedNow)
        {
            drawList->AddText(ImVec2(iconCenter.x - 3.2f, iconCenter.y - 7.5f), fg, "?");
        }
        else
        {
            drawList->AddText(ImVec2(iconCenter.x - 1.8f, iconCenter.y - 7.5f), fg, "i");
        }

        drawList->AddText(ImVec2(pillPos.x + 30.0f, pillPos.y + 5.0f), fg, service.status.c_str());

        const ImVec2 markerMin(pillPos.x + 5.0f, pillPos.y + 4.0f);
        const ImVec2 markerMax(pillPos.x + 25.0f, pillPos.y + 23.0f);
        if (!service.markerTooltip.empty() && ImGui::IsMouseHoveringRect(markerMin, markerMax))
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(service.markerTooltip.c_str());
            ImGui::EndTooltip();
        }
    }
    else
    {
        drawList->AddCircleFilled(ImVec2(pillPos.x + 13.0f, pillPos.y + 13.0f), 4.0f, fg);
        drawList->AddText(ImVec2(pillPos.x + 24.0f, pillPos.y + 5.0f), fg, service.status.c_str());
    }

    ImGui::Dummy(ImVec2(width, rowHeight + 5.0f));
}

static void DrawBackdrop(ImDrawList* drawList, ImVec2 pos, ImVec2 size)
{
    const ImU32 topLeft = IM_COL32(3, 14, 25, 255);
    const ImU32 topRight = IM_COL32(13, 31, 48, 255);
    const ImU32 bottomRight = IM_COL32(4, 18, 29, 255);
    const ImU32 bottomLeft = IM_COL32(2, 9, 17, 255);
    drawList->AddRectFilledMultiColor(pos, ImVec2(pos.x + size.x, pos.y + size.y), topLeft, topRight, bottomRight, bottomLeft);

    const ImU32 lineColor = IM_COL32(64, 104, 132, 24);
    for (float x = pos.x - size.y; x < pos.x + size.x; x += 88.0f)
        drawList->AddLine(ImVec2(x, pos.y), ImVec2(x + size.y * 0.55f, pos.y + size.y), lineColor, 1.0f);

    for (float y = pos.y + 44.0f; y < pos.y + size.y; y += 88.0f)
        drawList->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y - size.x * 0.18f), IM_COL32(64, 104, 132, 14), 1.0f);

    drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + size.x, pos.y + 190.0f), IM_COL32(0, 0, 0, 42));
}

static void DrawMLogo(ImDrawList* drawList, ImVec2 center, float scale)
{
    const ImU32 shadow = IM_COL32(0, 0, 0, 95);
    const ImU32 green = IM_COL32(135, 214, 0, 255);
    const ImU32 greenDark = IM_COL32(92, 169, 0, 255);

    auto p = [&](float x, float y) {
        return ImVec2(center.x + x * scale, center.y + y * scale);
    };

    ImVec2 left[] = {p(-74, 28), p(-58, -34), p(-22, -38), p(-28, 24)};
    ImVec2 mid[] = {p(-22, 24), p(-14, -66), p(22, -72), p(32, 20)};
    ImVec2 right[] = {p(30, 22), p(38, -28), p(74, -32), p(66, 26)};
    ImVec2 notch[] = {p(-31, 24), p(-19, -8), p(-4, 22)};

    auto offsetPoly = [&](ImVec2* poly, int count) {
        std::vector<ImVec2> shifted;
        shifted.reserve(count);
        for (int i = 0; i < count; ++i)
            shifted.push_back(ImVec2(poly[i].x + 6.0f, poly[i].y + 7.0f));
        drawList->AddConvexPolyFilled(shifted.data(), count, shadow);
    };

    offsetPoly(left, 4);
    offsetPoly(mid, 4);
    offsetPoly(right, 4);

    drawList->AddConvexPolyFilled(left, 4, green);
    drawList->AddConvexPolyFilled(mid, 4, green);
    drawList->AddConvexPolyFilled(right, 4, green);
    drawList->AddConvexPolyFilled(notch, 3, greenDark);
}

static bool AccentButton(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.08f, 0.11f, 1.00f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return pressed;
}

static bool DarkButton(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.07f, 0.18f, 0.27f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.25f, 0.36f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.14f, 0.22f, 1.00f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

static bool TitleButton(const char* label, ImVec2 pos, ImVec2 size, bool danger = false)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered =
        mouse.x >= pos.x && mouse.x <= pos.x + size.x &&
        mouse.y >= pos.y && mouse.y <= pos.y + size.y;
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    const ImU32 bg = danger
        ? (hovered ? IM_COL32(208, 56, 72, 255) : IM_COL32(80, 28, 38, 210))
        : (hovered ? IM_COL32(18, 70, 103, 255) : IM_COL32(8, 36, 56, 210));
    const ImU32 border = danger ? IM_COL32(230, 86, 100, 210) : IM_COL32(42, 91, 122, 210);
    const ImU32 text = IM_COL32(226, 236, 244, 255);

    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 5.0f);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border, 5.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f - 1.0f),
        text,
        label);

    return clicked;
}

static void DrawLoadingSpinner(ImDrawList* drawList, ImVec2 center, float radius, float thickness, ImU32 color)
{
    const float time = static_cast<float>(ImGui::GetTime());
    const float pi = 3.14159265f;

    drawList->AddCircle(center, radius, IM_COL32(42, 82, 106, 92), 64, thickness * 0.70f);

    auto strokeArc = [&](float start, float sweep, float r, ImU32 arcColor, float arcThickness) {
        drawList->PathClear();
        const int segments = 42;
        for (int i = 0; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = start + sweep * t;
            drawList->PathLineTo(ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r));
        }
        drawList->PathStroke(arcColor, 0, arcThickness);
    };

    const float start = time * 4.2f;
    strokeArc(start, pi * 1.35f, radius, color, thickness);
    strokeArc(-start * 0.75f + pi * 0.35f, pi * 0.68f, radius - 8.0f, IM_COL32(68, 126, 158, 170), thickness * 0.75f);

    const float lead = start + pi * 1.35f;
    const ImVec2 leadPos(center.x + cosf(lead) * radius, center.y + sinf(lead) * radius);
    drawList->AddCircleFilled(leadPos, thickness * 1.25f, IM_COL32(180, 245, 32, 245));
}

static void DrawBootCard(ImDrawList* drawList, ImVec2 windowPos, ImVec2 windowSize, float progress, float scale, float alpha)
{
    const ImVec2 baseSize(windowSize.x - 2.0f, windowSize.y - 2.0f);
    const ImVec2 cardSize(baseSize.x * scale, baseSize.y * scale);
    const ImVec2 cardPos(
        windowPos.x + (windowSize.x - cardSize.x) * 0.5f + 1.0f,
        windowPos.y + (windowSize.y - cardSize.y) * 0.5f + 1.0f);

    const int a = static_cast<int>(255.0f * alpha);
    drawList->AddRectFilledMultiColor(
        cardPos,
        ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
        IM_COL32(4, 17, 26, a),
        IM_COL32(6, 24, 36, a),
        IM_COL32(3, 14, 22, a),
        IM_COL32(2, 10, 17, a));
    drawList->AddRect(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y), IM_COL32(39, 80, 104, a), 16.0f, 0, 1.0f);

    const float compactWidth = (std::min)(cardSize.x, 520.0f * scale);
    const float compactX = cardPos.x + (cardSize.x - compactWidth) * 0.5f;

    const ImVec2 spinnerCenter(cardPos.x + cardSize.x * 0.5f, cardPos.y + 76.0f * scale);
    DrawLoadingSpinner(drawList, spinnerCenter, 22.0f * scale, 3.0f * scale, IM_COL32(135, 214, 0, a));

    const char* title = "MMR Activator";
    const char* subtitle = "Aplicando configuracoes automaticamente";
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
    drawList->AddText(ImVec2(cardPos.x + (cardSize.x - titleSize.x) * 0.5f, cardPos.y + 122.0f * scale), IM_COL32(238, 246, 252, a), title);
    drawList->AddText(ImVec2(cardPos.x + (cardSize.x - subtitleSize.x) * 0.5f, cardPos.y + 152.0f * scale), IM_COL32(140, 160, 176, a), subtitle);

    const ImVec2 barPos(compactX + 62.0f * scale, cardPos.y + 210.0f * scale);
    const float barWidth = compactWidth - 124.0f * scale;
    drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth, barPos.y + 8.0f * scale), IM_COL32(5, 32, 49, a), 5.0f);
    drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth * progress, barPos.y + 8.0f * scale), IM_COL32(135, 214, 0, a), 5.0f);
}

static void RefreshService(ServiceEntry& service)
{
    service.status = QueryServiceStatusText(service);
    service.statusColor = ServiceStatusColor(service.status);
}

static void ApplyServiceOutcome(ServiceEntry& service, const std::string& initialStatus, const std::string& finalStatus)
{
    service.status = finalStatus;

    if (IsActiveStatus(finalStatus) && !EqualsIgnoreCase(service.displayName, "Defender Quick Scan"))
        service.status = "Ativado";

    service.marker = ServiceMarker::Unknown;
    service.markerTooltip.clear();

    if (EqualsIgnoreCase(service.displayName, "Secure Boot") && !IsActiveStatus(finalStatus))
    {
        service.marker = ServiceMarker::FirmwareAction;
        service.markerTooltip = "Requer ajuste na BIOS/UEFI. Clique em BIOS para pesquisar o procedimento pelo modelo da placa-mae.";
    }
    else if (IsActiveStatus(finalStatus) && IsActiveStatus(initialStatus))
    {
        service.marker = ServiceMarker::InitiallyActive;
    }
    else if (IsActiveStatus(finalStatus))
    {
        service.marker = ServiceMarker::ActivatedNow;
        service.markerTooltip = "Ativado nesta execucao. Reinicie o PC para garantir inicializacao e telemetria desde o boot.";
    }
    else if (IsDisabledOrFailedStatus(finalStatus))
    {
        service.marker = ServiceMarker::StillDisabled;
        service.markerTooltip = "Nao foi possivel aplicar automaticamente. Verifique politicas, otimizadores ou integridade do Windows; restauracao do sistema pode ser necessaria.";
    }

    service.statusColor = ServiceStatusColor(service.status);
}

static std::string ApplyVisualTestPreset(std::vector<ServiceEntry>& services)
{
    for (int i = 0; i < static_cast<int>(services.size()); ++i)
    {
        ServiceEntry& service = services[i];

        if (EqualsIgnoreCase(service.displayName, "Secure Boot"))
        {
            ApplyServiceOutcome(service, "Desativado", "Desativado");
            continue;
        }

        switch (i % 5)
        {
        case 0:
            ApplyServiceOutcome(service, "Ativado", "Ativado");
            break;
        case 1:
            ApplyServiceOutcome(service, "Desativado", "Ativado");
            break;
        case 2:
            ApplyServiceOutcome(service, "Desativado", "Desativado");
            break;
        case 3:
            ApplyServiceOutcome(service, "Desativado", "Erro: automatico");
            break;
        default:
            ApplyServiceOutcome(service, "Pendente", EqualsIgnoreCase(service.displayName, "Defender Quick Scan") ? "Scan concluido" : "Ativado");
            break;
        }
    }

    return "MODO TESTE: estados visuais simulados; nenhuma configuracao do Windows foi alterada";
}

static std::string ApplyPreset(std::vector<ServiceEntry>& services)
{
    if (IsVisualTestMode())
        return ApplyVisualTestPreset(services);

    std::vector<std::string> messages;
    messages.push_back(EnsureSysmonFilesExtracted());

    for (ServiceEntry& service : services)
    {
        const std::string initialStatus = QueryServiceStatusText(service);

        if (EqualsIgnoreCase(service.displayName, "NTFS Journal"))
        {
            const std::string journalMessage = EnsureNtfsJournalOnFixedDrives();
            const std::string finalStatus = QueryServiceStatusText(service);
            ApplyServiceOutcome(service, initialStatus, finalStatus);

            if (!service.markerTooltip.empty())
                service.markerTooltip = journalMessage + ". " + service.markerTooltip;
            else
                service.markerTooltip = journalMessage;

            messages.push_back(journalMessage);
            continue;
        }

        const std::string finalStatus = EnableAndStartWindowsService(service);
        ApplyServiceOutcome(service, initialStatus, finalStatus);
    }

    return CombineMessages(messages);
}

static void RenderApp()
{
    static std::vector<ServiceEntry> services;
    static std::string lastMessage = "READY";
    static std::mutex stateMutex;
    static bool initialized = false;
    static bool workerStarted = false;
    static std::atomic_bool automationComplete = false;
    static bool transitionComplete = false;
    static std::atomic_bool showRestartWarning = false;
    static double startedAt = 0.0;
    static double completedAt = 0.0;

    if (!initialized)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            services = RequiredServices();
            lastMessage = "PREPARANDO ATIVADOR";
        }
        startedAt = ImGui::GetTime();
        initialized = true;
    }

    if (!workerStarted)
    {
        workerStarted = true;
        std::thread([]() {
            std::vector<ServiceEntry> localServices;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                localServices = services;
                lastMessage = "APLICANDO PRESET";
            }

            const std::string result = ApplyPreset(localServices);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                services = localServices;
                lastMessage = result;
            }

            showRestartWarning.store(true, std::memory_order_release);
            automationComplete.store(true, std::memory_order_release);
        }).detach();
    }

    std::vector<ServiceEntry> servicesSnapshot;
    std::string lastMessageSnapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        servicesSnapshot = services;
        lastMessageSnapshot = lastMessage;
    }

    const bool isAutomationComplete = automationComplete.load(std::memory_order_acquire);
    if (isAutomationComplete && completedAt == 0.0 && ImGui::GetTime() - startedAt > 1.15)
    {
        completedAt = ImGui::GetTime();
    }

    if (isAutomationComplete && !transitionComplete)
    {
        const float raw = completedAt > 0.0 ? (std::min)(1.0f, static_cast<float>((ImGui::GetTime() - completedAt) / 0.92)) : 0.0f;
        const float eased = raw * raw * (3.0f - 2.0f * raw);
        const int width = static_cast<int>(520.0f + (1100.0f - 520.0f) * eased);
        const int height = static_cast<int>(320.0f + (820.0f - 320.0f) * eased);
        CenterWindow(g_hwnd, width, height);

        if (raw >= 1.0f)
        {
            transitionComplete = true;
            g_mainWindowSized = true;
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("MMR Service", nullptr, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();

    if (!transitionComplete)
    {
        drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y), IM_COL32(1, 8, 14, 255));
        const float loadProgress = isAutomationComplete
            ? 1.0f
            : (std::min)(1.0f, static_cast<float>((ImGui::GetTime() - startedAt) / 1.65));
        const float transitionProgress = isAutomationComplete
            ? (std::min)(1.0f, static_cast<float>((ImGui::GetTime() - completedAt) / 0.82))
            : 0.0f;
        const float eased = transitionProgress * transitionProgress * (3.0f - 2.0f * transitionProgress);
        const float scale = 1.0f;
        const float alpha = 1.0f - (std::max)(0.0f, eased - 0.72f) / 0.28f;
        DrawBootCard(drawList, windowPos, windowSize, loadProgress, scale, alpha);
        ImGui::End();
        return;
    }

    DrawBackdrop(drawList, windowPos, windowSize);

    const ImVec2 minimizePos(windowPos.x + windowSize.x - 82.0f, windowPos.y + 14.0f);
    const ImVec2 closePos(windowPos.x + windowSize.x - 42.0f, windowPos.y + 14.0f);
    if (TitleButton("-", minimizePos, ImVec2(30.0f, 28.0f)))
        ShowWindow(g_hwnd, SW_MINIMIZE);
    if (TitleButton("X", closePos, ImVec2(30.0f, 28.0f), true))
        PostQuitMessage(0);

    int running = 0;
    for (const ServiceEntry& service : servicesSnapshot)
        running += IsActiveStatus(service.status) ? 1 : 0;

    const float contentWidth = (std::max)(760.0f, (std::min)(windowSize.x - 64.0f, 940.0f));
    const float contentX = (windowSize.x - contentWidth) * 0.5f;

    const ImVec2 logoCenter(windowPos.x + windowSize.x * 0.5f, windowPos.y + 42.0f);
    DrawMLogo(drawList, logoCenter, 0.50f);

    ImGui::SetCursorPos(ImVec2(0.0f, 78.0f));
    ImGui::SetWindowFontScale(1.38f);
    const char* title = "MATCHMAKING";
    ImGui::SetCursorPosX((windowSize.x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(0.93f, 0.95f, 0.98f, 1.00f), "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    const char* subtitle = "RANKING";
    ImGui::SetCursorPosX((windowSize.x - ImGui::CalcTextSize(subtitle).x) * 0.5f);
    ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.00f), "%s", subtitle);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetCursorPos(ImVec2(contentX, 132.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 16.0f));
    ImGui::BeginChild("CommandPanel", ImVec2(contentWidth, 76.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(ImVec4(0.54f, 0.86f, 0.04f, 1.00f), "AUTO ACTIVATOR");
    ImGui::SameLine();
    ImGui::TextDisabled("  preset aplicado automaticamente  |  %d verificacoes  |  %d ativas", static_cast<int>(servicesSnapshot.size()), running);

    ImGui::SetCursorPosY(42.0f);
    ImGui::TextDisabled("RESULTADO");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.54f, 0.86f, 0.04f, 1.00f), "%s", lastMessageSnapshot.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const float listY = 222.0f;
    const float warningHeight = 78.0f;
    const float warningY = windowSize.y - warningHeight - 18.0f;
    const float listHeight = warningY - listY - 12.0f;
    ImGui::SetCursorPos(ImVec2(contentX, listY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("ListPanel", ImVec2(contentWidth, listHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(ImVec4(0.54f, 0.86f, 0.04f, 1.00f), "PRESET STATUS");
    ImGui::SameLine();
    ImGui::TextDisabled("  execucao automatica");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float rowHeight = 28.0f;
    for (int i = 0; i < static_cast<int>(servicesSnapshot.size()); ++i)
        DrawComponentRow(servicesSnapshot[i], rowWidth, rowHeight, i);

    if (servicesSnapshot.empty())
    {
        ImGui::SetCursorPos(ImVec2(0.0f, 86.0f));
        ImGui::SetWindowFontScale(1.25f);
        const char* empty = "NO SERVICES ADDED";
        ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize(empty).x) * 0.5f);
        ImGui::TextDisabled("%s", empty);
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SetCursorPos(ImVec2(contentX, warningY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 12.0f));
    ImGui::BeginChild("WarningPanel", ImVec2(contentWidth, warningHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(ImVec4(0.98f, 0.70f, 0.22f, 1.00f), "AVISO");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", showRestartWarning.load(std::memory_order_acquire) ? "reinicializacao recomendada" : "aguardando aplicacao automatica");
    ImGui::TextWrapped(
        "Reinicie o seu PC apos usar este ativador para evitar W.O. "
        "Os servicos foram iniciados com o Windows ja ligado; reiniciar garante que eles registrem tudo desde o inicio do sistema. "
        "Atenciosamente, MMR.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"MMRServiceWindow",
        nullptr
    };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"MMR Service Control", WS_POPUP | WS_MINIMIZEBOX, 100, 80, 520, 320, nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;
    CenterWindow(hwnd, 520, 320);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);

    ApplyTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderApp();

        ImGui::Render();
        const float clearColor[4] = {0.07f, 0.08f, 0.10f, 1.00f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL featureLevel;
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (result != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        SetWindowRgn(hWnd, CreateRoundRectRgn(0, 0, LOWORD(lParam) + 1, HIWORD(lParam) + 1, 24, 24), TRUE);
        if (g_pd3dDevice != nullptr)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_NCHITTEST:
    {
        const LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit != HTCLIENT)
            return hit;

        const POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rect{};
        GetWindowRect(hWnd, &rect);

        const int localX = cursor.x - rect.left;
        const int localY = cursor.y - rect.top;
        const int width = rect.right - rect.left;
        if (localY >= 0 && localY < 230 && localX < width - 110)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
