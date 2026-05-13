#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <shellapi.h>
#include <Shlobj.h>
#include <algorithm>
#include <cctype>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"
#include "scanner.hpp"

static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

MemoryScanner scanner;
std::vector<ProcessInfo> processes;
std::vector<ScanResult> results;
std::mutex resultsMutex;
int selectedResultIdx = -1;
std::atomic<unsigned long long> resultsVersion(0);

std::atomic<bool> isScanning(false);
std::atomic<bool> stopScanning(false);
std::atomic<float> scanProgress(0.0f);
char searchFilter[128] = "";
char processFilter[64] = "";
int selectedProc = -1;
bool refineRequested = false;
bool processFocusMode = false;
DWORD focusedProcessPid = 0;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void OpenStringLocation(std::string pathVal) {
    if (pathVal.empty()) return;

    while (!pathVal.empty() && (pathVal.front() == ' ' || pathVal.front() == '\"')) pathVal.erase(pathVal.begin());
    while (!pathVal.empty() && (pathVal.back() == ' ' || pathVal.back() == '\n' || pathVal.back() == '\r' || pathVal.back() == '\"')) pathVal.pop_back();
    std::replace(pathVal.begin(), pathVal.end(), '/', '\\');

    std::string extracted = pathVal;
    size_t drivePos = std::string::npos;
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::string upperDrive = std::string(1, c) + ":\\";
        std::string lowerDrive = std::string(1, (char)tolower(c)) + ":\\";
        size_t pos = extracted.find(upperDrive);
        if (pos == std::string::npos) pos = extracted.find(lowerDrive);
        if (pos != std::string::npos) { drivePos = pos; break; }
    }
    if (drivePos != std::string::npos) {
        extracted = extracted.substr(drivePos);
    } else {
        size_t netPos = extracted.find("\\\\");
        if (netPos != std::string::npos) extracted = extracted.substr(netPos);
    }

    auto Exists = [](const std::string& p) {
        DWORD attrs = GetFileAttributesA(p.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES;
    };

    auto TryResolve = [&](const std::string& candidate, std::string& outResolved) -> bool {
        if (candidate.empty()) return false;
        if (Exists(candidate)) { outResolved = candidate; return true; }
        char fullPath[MAX_PATH] = { 0 };
        if (GetFullPathNameA(candidate.c_str(), MAX_PATH, fullPath, NULL) > 0 && Exists(fullPath)) {
            outResolved = fullPath; return true;
        }
        char desktopPath[MAX_PATH] = { 0 };
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktopPath))) {
            std::string desktopCandidate = std::string(desktopPath) + "\\" + candidate;
            if (Exists(desktopCandidate)) { outResolved = desktopCandidate; return true; }
        }
        return false;
    };

    std::vector<std::string> candidates;
    candidates.push_back(extracted);
    for (size_t i = 0; i < pathVal.size(); ++i) {
        if ((i + 2 < pathVal.size() && std::isalpha((unsigned char)pathVal[i]) && pathVal[i + 1] == ':' && (pathVal[i + 2] == '\\' || pathVal[i + 2] == '/')) ||
            (i + 1 < pathVal.size() && pathVal[i] == '\\' && pathVal[i + 1] == '\\')) {
            size_t j = i;
            while (j < pathVal.size()) {
                char ch = pathVal[j];
                if (ch == '\"' || ch == '\n' || ch == '\r' || ch == '\t') break;
                j++;
            }
            if (j > i) {
                std::string frag = pathVal.substr(i, j - i);
                std::replace(frag.begin(), frag.end(), '/', '\\');
                candidates.push_back(frag);
            }
        }
    }

    std::string resolvedPath;
    for (const auto& c : candidates) {
        if (TryResolve(c, resolvedPath)) break;
    }
    if (resolvedPath.empty()) {
        MessageBoxA(NULL, "This value does not contain a valid file path.", "Invalid Path", MB_ICONWARNING);
        return;
    }

    std::string cmd = "/select,\"" + resolvedPath + "\"";
    ShellExecuteA(NULL, "open", "explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
}

void CopyToClipboard(const std::string& text) {
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hg) {
            void* ptr = GlobalLock(hg);
            if (ptr) { memcpy(ptr, text.c_str(), text.size() + 1); GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); }
        }
        CloseClipboard();
    }
}

void ApplyEnterpriseTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.Colors[ImGuiCol_WindowBg]       = ImVec4(0.05f, 0.07f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_ChildBg]        = ImVec4(0.09f, 0.12f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_PopupBg]        = ImVec4(0.10f, 0.13f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_Border]         = ImVec4(0.18f, 0.24f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_Separator]      = ImVec4(0.16f, 0.22f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.21f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive]  = ImVec4(0.19f, 0.30f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_Button]         = ImVec4(0.12f, 0.17f, 0.23f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered]  = ImVec4(0.19f, 0.32f, 0.45f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]   = ImVec4(0.13f, 0.45f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_Text]           = ImVec4(0.89f, 0.94f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]   = ImVec4(0.54f, 0.63f, 0.72f, 1.00f);
    style.Colors[ImGuiCol_Header]         = ImVec4(0.14f, 0.20f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered]  = ImVec4(0.18f, 0.30f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.14f, 0.45f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_CheckMark]      = ImVec4(0.39f, 0.85f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]     = ImVec4(0.27f, 0.74f, 0.91f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.39f, 0.88f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.06f, 0.09f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab]  = ImVec4(0.16f, 0.25f, 0.34f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.21f, 0.35f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.25f, 0.47f, 0.63f, 1.00f);

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
}

void RenderUI(HWND hwnd) {
    ApplyEnterpriseTheme();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("MasterContainer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    auto PushPrimaryButton = []() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.55f, 0.74f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.66f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.06f, 0.42f, 0.60f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.99f, 1.00f, 1.00f));
    };
    auto PushSecondaryButton = []() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.19f, 0.27f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.27f, 0.38f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.23f, 0.32f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.92f, 0.98f, 1.00f));
    };
    auto PopButtonVariant = []() { ImGui::PopStyleColor(4); };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.10f, 0.14f, 1.00f));
    ImGui::BeginChild("Toolbar", ImVec2(0, 46), false, ImGuiWindowFlags_NoScrollbar);
    
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(0)) {
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    
    ImGui::SetCursorPos(ImVec2(14, 14));
    ImGui::TextUnformatted("Memory Checker");
    ImGui::SameLine();
    ImGui::TextDisabled("Live Process String Scanner");
    
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 108, 9));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0)); 
    if (ImGui::Button("_", ImVec2(32, 28))) ShowWindow(hwnd, SW_MINIMIZE);
    ImGui::SameLine(0, 0);
    if (ImGui::Button("[ ]", ImVec2(32, 28))) {
        if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        else ShowWindow(hwnd, SW_MAXIMIZE);
    }
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.1f, 0.2f, 1.0f)); 
    if (ImGui::Button("X", ImVec2(32, 28))) PostQuitMessage(0);
    ImGui::PopStyleColor(2);
    
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.11f, 0.15f, 1.00f)); 
    ImGui::BeginChild("Sidebar", ImVec2(320, 0), true, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::SetCursorPos(ImVec2(12, 12));
    ImGui::TextDisabled("PROCESSES");
    
    ImGui::SetCursorPos(ImVec2(12, 35));
    ImGui::SetNextItemWidth(295);
    ImGui::InputTextWithHint("##ProcSearch", "Filter processes...", processFilter, sizeof(processFilter));
    if (processFocusMode && strlen(processFilter) == 0) {
        processFocusMode = false;
        focusedProcessPid = 0;
    }
    
    ImGui::SetCursorPos(ImVec2(0, 68));
    ImGui::Separator();
    
    if (ImGui::BeginChild("ProcList", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar)) {
        for (int i = 0; i < (int)processes.size(); i++) {
            if (processFocusMode && processes[i].pid != focusedProcessPid) continue;
            if (strlen(processFilter) > 0) {
                std::string pName = processes[i].name;
                std::string pFilter = processFilter;
                std::transform(pName.begin(), pName.end(), pName.begin(), ::tolower);
                std::transform(pFilter.begin(), pFilter.end(), pFilter.begin(), ::tolower);
                if (pName.find(pFilter) == std::string::npos) continue;
            }
            
            bool isSelected = (selectedProc == i);
            
            ImGui::PushID(i);
            const float rowHeight = 24.0f;
            ImVec2 rowSize(ImGui::GetContentRegionAvail().x, rowHeight);
            if (ImGui::InvisibleButton("proc_row", rowSize)) {
                selectedProc = i;
                if (processFocusMode && focusedProcessPid == processes[i].pid) {
                    processFocusMode = false;
                    focusedProcessPid = 0;
                    processFilter[0] = '\0';
                } else {
                    processFocusMode = true;
                    focusedProcessPid = processes[i].pid;
                    strncpy_s(processFilter, sizeof(processFilter), processes[i].name.c_str(), _TRUNCATE);
                }
            }

            const bool hovered = ImGui::IsItemHovered();
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (isSelected) dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(41, 79, 116, 255), 4.0f);
            else if (hovered) dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(28, 52, 74, 255), 4.0f);

            const bool isExplorer = (_stricmp(processes[i].name.c_str(), "explorer.exe") == 0);
            char rowText[512];
            sprintf_s(rowText, sizeof(rowText), "%s %-6u  %s", isExplorer ? "[F]" : (isSelected ? "[*]" : "[ ]"), processes[i].pid, processes[i].name.c_str());
            const ImU32 textCol = isSelected ? IM_COL32(238, 246, 255, 255) : IM_COL32(188, 206, 222, 255);
            dl->AddText(ImVec2(rect.Min.x + 8.0f, rect.Min.y + 4.0f), textCol, rowText);

            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild(); 
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 0);
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.25f, 0.27f, 1.00f));
    ImGui::BeginChild("VertBorder", ImVec2(1, 0), false); ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 0);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.08f, 0.12f, 1.00f));
    ImGui::BeginChild("MainWorkspace", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::BeginChild("ScanControls", ImVec2(0, 130), true, ImGuiWindowFlags_NoScrollbar); 
    
    ImGui::SetCursorPos(ImVec2(16, 14));
    ImGui::TextDisabled("Target: ");
    ImGui::SameLine();
    if (selectedProc >= 0) ImGui::TextColored(ImVec4(1,1,1,1), "%s (PID: %u)", processes[selectedProc].name.c_str(), processes[selectedProc].pid);
    else ImGui::TextDisabled("No process attached");
    
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    PushSecondaryButton();
    if (ImGui::Button("Refresh", ImVec2(96, 30))) processes = scanner.GetProcessList();
    PopButtonVariant();
    
    ImGui::SetCursorPos(ImVec2(15, 48));
    ImGui::Separator();
    
    ImGui::SetCursorPos(ImVec2(16, 62));
    
    static char startAddr[32] = "0x000000000000";
    static char endAddr[32] = "0x7FFFFFFFFFFF";
    static int dataTypeFilter = 0; 
    const bool uiHasFilter = (strlen(searchFilter) > 0) || (dataTypeFilter != 0);
    
    ImGui::TextDisabled("Range:"); ImGui::SameLine(65);
    ImGui::SetNextItemWidth(130); ImGui::InputText("##start", startAddr, sizeof(startAddr));
    ImGui::SameLine(); ImGui::TextDisabled("-"); ImGui::SameLine();
    ImGui::SetNextItemWidth(130); ImGui::InputText("##end", endAddr, sizeof(endAddr));
    
    ImGui::SameLine(380);
    ImGui::TextDisabled("Filter:"); ImGui::SameLine(432);
    ImGui::SetNextItemWidth(180); ImGui::InputText("##mainfilter", searchFilter, sizeof(searchFilter));
    
    ImGui::SameLine(655);
    ImGui::TextDisabled("Type:"); ImGui::SameLine(698);
    ImGui::SetNextItemWidth(90);
    const char* types[] = { "All", "ASCII", "UTF-16" };
    ImGui::Combo("##datatype", &dataTypeFilter, types, IM_ARRAYSIZE(types));
    
    ImGui::SetCursorPos(ImVec2(16, 97));
    PushPrimaryButton();
    if (ImGui::Button(isScanning ? "Stop Scan" : "Start Scan", ImVec2(132, 32))) {
        if (isScanning) {
            stopScanning = true;
        } else if (selectedProc >= 0) {
            resultsMutex.lock(); results.clear(); resultsMutex.unlock();
            selectedResultIdx = -1;
            resultsVersion++;
            isScanning = true; stopScanning = false;
            std::thread([&]() {
                std::vector<ScanResult> temp;
                scanner.ScanStrings(processes[selectedProc].pid, temp, stopScanning, scanProgress);
                resultsMutex.lock(); results = std::move(temp); resultsMutex.unlock();
                resultsVersion++;
                isScanning = false;
            }).detach();
        }
    }
    PopButtonVariant();
    
    ImGui::SameLine();
    PushSecondaryButton();
    if (ImGui::Button("Clear Results", ImVec2(132, 32))) {
        resultsMutex.lock(); results.clear(); resultsMutex.unlock();
        selectedResultIdx = -1;
        resultsVersion++;
    }
    PopButtonVariant();
    ImGui::SameLine();
    PushSecondaryButton();
    if (ImGui::Button("Refine Scan", ImVec2(132, 32))) {
        if (!isScanning && uiHasFilter) {
            refineRequested = true;
        } else if (!isScanning && selectedResultIdx >= 0 && selectedResultIdx < (int)results.size()) {
            strncpy_s(searchFilter, sizeof(searchFilter), results[selectedResultIdx].value.c_str(), _TRUNCATE);
            dataTypeFilter = results[selectedResultIdx].isUnicode ? 2 : 1;
            refineRequested = true;
        }
    }
    PopButtonVariant();
    
    if (isScanning) {
        ImGui::SetCursorPos(ImVec2(440, 104));
        ImGui::TextColored(ImVec4(0.0f, 0.48f, 0.80f, 1.0f), "Scanning memory pages: %.1f%%", scanProgress * 100.0f);
    }
    
    ImGui::EndChild(); 
    
    ImGui::Separator();
    
    ImGui::SetNextWindowContentSize(ImVec2(1900.0f, 0.0f));
    ImGui::BeginChild("TableContainer", ImVec2(0, -30), true, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);

    resultsMutex.lock();
    
    static std::vector<int> filteredIndicesCache;
    static std::string filterCache;
    static int typeCache = -1;
    static unsigned long long versionCache = 0;
    bool hasFilter = (strlen(searchFilter) > 0) || (dataTypeFilter != 0);
    
    if (hasFilter) {
        std::string currentFilter = searchFilter;
        std::transform(currentFilter.begin(), currentFilter.end(), currentFilter.begin(), ::tolower);
        const unsigned long long ver = resultsVersion.load();
        const bool needRebuild = (ver != versionCache) || (currentFilter != filterCache) || (dataTypeFilter != typeCache);
        if (needRebuild) {
            filteredIndicesCache.clear();
            for (int i = 0; i < (int)results.size(); ++i) {
                const auto& res = results[i];
                if (dataTypeFilter == 1 && res.isUnicode) continue;
                if (dataTypeFilter == 2 && !res.isUnicode) continue;
                if (!currentFilter.empty()) {
                    std::string rVal = res.value;
                    std::transform(rVal.begin(), rVal.end(), rVal.begin(), ::tolower);
                    if (rVal.find(currentFilter) == std::string::npos) continue;
                }
                filteredIndicesCache.push_back(i);
            }
            filterCache = currentFilter;
            typeCache = dataTypeFilter;
            versionCache = ver;
        }
    }
    if (refineRequested && hasFilter) {
        std::vector<ScanResult> refined;
        refined.reserve(filteredIndicesCache.size());
        for (int idx : filteredIndicesCache) refined.push_back(results[idx]);
        results = std::move(refined);
        searchFilter[0] = '\0';
        dataTypeFilter = 0;
        hasFilter = false;
        filteredIndicesCache.clear();
        selectedResultIdx = -1;
        refineRequested = false;
        resultsVersion++;
        versionCache = 0;
    } else if (refineRequested) {
        refineRequested = false;
    }
    int displayCount = hasFilter ? (int)filteredIndicesCache.size() : (int)results.size();
    
    ImGui::Columns(4, "ResultsTable", true);
    ImGui::SetColumnWidth(0, 180);
    ImGui::SetColumnWidth(1, 90);
    ImGui::SetColumnWidth(2, 1200);
    ImGui::SetColumnWidth(3, 80);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.70f, 0.82f, 1.0f));
    ImGui::Text(" Address"); ImGui::NextColumn();
    ImGui::Text(" Type"); ImGui::NextColumn();
    ImGui::Text(" Value"); ImGui::NextColumn();
    ImGui::Text(" Size"); ImGui::NextColumn();
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (displayCount == 0) {
        const ImVec2 area = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + area.y * 0.30f);
        ImGui::TextColored(ImVec4(0.58f, 0.72f, 0.84f, 1.0f), "No scan results yet");
        ImGui::TextDisabled("1) Select a process  2) Start Scan  3) Refine with Filter/Type");
    } else {
        ImGuiListClipper clipper;
        clipper.Begin(displayCount);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                int actualIdx = hasFilter ? filteredIndicesCache[i] : i;
                const auto& res = results[actualIdx];
                ImGui::PushID(actualIdx);
                const bool isRowSelected = (selectedResultIdx == actualIdx);

                char addrStr[64]; sprintf_s(addrStr, "0x%p", (void*)res.address);
                if (ImGui::Selectable("##result_row", isRowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, 20))) {
                    selectedResultIdx = actualIdx;
                }
                if (ImGui::IsItemClicked(1)) selectedResultIdx = actualIdx;
                ImGui::SameLine(0, 8);
                ImGui::Text(" %s", addrStr); ImGui::NextColumn();
                ImGui::TextDisabled(" %s", res.isUnicode ? "UTF-16" : "ASCII"); ImGui::NextColumn();

                ImGui::TextUnformatted(res.value.c_str());
                if (ImGui::BeginPopupContextItem("RowPopup")) {
                    selectedResultIdx = actualIdx;
                    ImGui::TextDisabled("Actions");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Address")) CopyToClipboard(addrStr);
                    if (ImGui::MenuItem("Copy Value")) CopyToClipboard(res.value);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Open Path from Value")) OpenStringLocation(res.value);
                    ImGui::EndPopup();
                }
                ImGui::NextColumn();

                ImGui::TextDisabled(" %zu", res.value.length()); ImGui::NextColumn();
                ImGui::PopID();
            }
        }
    }
    ImGui::Columns(1);
    resultsMutex.unlock();
    
    ImGui::EndChild(); 
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.16f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.94f, 0.98f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(0, 28), false, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::SetCursorPos(ImVec2(10, 6));
    if (isScanning) ImGui::Text("Scanning %.1f%%", scanProgress.load() * 100.0f);
    else ImGui::Text("Ready");
    
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::Text("Shown: %d", displayCount);
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    ImGui::Text("Total: %zu", results.size());
    
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::EndChild(); 
    ImGui::PopStyleColor();

    ImGui::End(); 
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("MemoryTracerClass"), NULL };
    RegisterClassEx(&wc);
    
    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, _T("Memory Tracer"), WS_POPUP, 100, 100, 1300, 850, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F, 0 };
    ImFont* uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, NULL, ranges);
    if (!uiFont) uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 17.0f, NULL, ranges);
    if (!uiFont) uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 17.0f, NULL, ranges);
    if (uiFont) io.FontDefault = uiFont;

    scanner.EnableDebugPrivilege();
    processes = scanner.GetProcessList();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI(hwnd);

        ImGui::Render();
        const float clear_color[4] = { 0.12f, 0.12f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0); 
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, NULL, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
