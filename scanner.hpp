#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <fstream>

struct ProcessInfo {
    DWORD pid;
    std::string name;
};

struct ScanResult {
    uintptr_t address;
    std::string value;
    bool isUnicode;
};

class MemoryScanner {
public:
    std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    std::vector<ProcessInfo> GetProcessList() {
        std::vector<ProcessInfo> processes;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return processes;

        PROCESSENTRY32 pe; 
        pe.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hSnap, &pe)) {
            do {
                std::string pName;
#ifdef UNICODE
                pName = WStringToString(pe.szExeFile);
#else
                pName = std::string(pe.szExeFile);
#endif
                processes.push_back({ pe.th32ProcessID, pName });
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return processes;
    }

    bool EnableDebugPrivilege() {
        HANDLE hToken;
        LUID luid;
        TOKEN_PRIVILEGES tp;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            return false;

        if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            CloseHandle(hToken);
            return false;
        }

        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
            CloseHandle(hToken);
            return false;
        }

        CloseHandle(hToken);
        return true;
    }

    void ScanStrings(DWORD pid, std::vector<ScanResult>& results, std::atomic<bool>& stopScanning, std::atomic<float>& progress) {
        results.clear();
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) return;

        SYSTEM_INFO si;
        GetSystemInfo(&si);

        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;

        while (addr < maxAddr && !stopScanning) {
            if (VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
                    std::vector<char> buffer(mbi.RegionSize);
                    SIZE_T bytesRead;
                    if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                        ExtractStringsFromBuffer(buffer.data(), bytesRead, (uintptr_t)mbi.BaseAddress, results);
                    }
                }
                addr += mbi.RegionSize;
                progress = (float)addr / (float)maxAddr;
            } else {
                addr += 4096;
            }
        }

        CloseHandle(hProcess);
    }

private:
    void ExtractStringsFromBuffer(const char* buffer, SIZE_T size, uintptr_t baseAddr, std::vector<ScanResult>& results) {
        std::string currentStr;
        for (SIZE_T i = 0; i < size; ++i) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                currentStr += buffer[i];
            } else {
                if (currentStr.length() >= 5) {
                    results.push_back({ baseAddr + i - currentStr.length(), currentStr, false });
                }
                currentStr.clear();
            }
        }

        std::string currentWide;
        for (SIZE_T i = 0; i < size - 1; i += 2) {
            char c = buffer[i];
            char nullByte = buffer[i + 1];
            if (c >= 32 && c <= 126 && nullByte == 0) {
                currentWide += c;
            } else {
                if (currentWide.length() >= 5) {
                    results.push_back({ baseAddr + i - (currentWide.length() * 2), currentWide, true });
                }
                currentWide.clear();
            }
        }
    }
};
