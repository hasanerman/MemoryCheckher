#include "scanner.hpp"
#include <iostream>
#include <iomanip>
#include <string>   
#include <fstream>  
#include <atomic>

int main() {
    MemoryScanner scanner;
    
    std::cout << "[*] SeDebugPrivilege yetkisi aliniyor..." << std::endl;
    if (scanner.EnableDebugPrivilege()) {
        std::cout << "[+] Yetki alindi!" << std::endl;
    } else {
        std::cout << "[-] Yetki alinamadi. Yonetici olarak calistirmayi deneyin." << std::endl;
    }

    std::cout << "[*] Surecler listeleniyor..." << std::endl;
    std::vector<ProcessInfo> processes = scanner.GetProcessList();
    
    DWORD targetPid = 0;
    std::string targetName = "explorer.exe";

    for (const auto& p : processes) {
        if (p.name == targetName) {
            targetPid = p.pid;
            break;
        }
    }

    if (targetPid == 0) {
        std::cout << "[-] " << targetName << " bulunamadi!" << std::endl;
        std::cout << "[*] Mevcut bazi surecler:" << std::endl;
        for (size_t i = 0; i < (processes.size() < 5 ? processes.size() : 5); ++i) {
            std::cout << " - " << processes[i].name << " (PID: " << processes[i].pid << ")" << std::endl;
        }
        return 1;
    }

    std::cout << "[+] Hedef bulundu: " << targetName << " (PID: " << targetPid << ")" << std::endl;
    std::cout << "[*] Bellek taraniyor, bu biraz zaman alabilir..." << std::endl;

    std::vector<ScanResult> results;
    std::atomic<bool> stopScanning(false);
    std::atomic<float> progress(0.0f);

    scanner.ScanStrings(targetPid, results, stopScanning, progress);

    std::cout << "\n[+] Tarama tamamlandi! Toplam " << results.size() << " string bulundu." << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl;
    std::cout << std::left << std::setw(18) << "Adres" << std::setw(10) << "Tip" << "Deger" << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl;

    int count = 0;
    for (const auto& res : results) {
        std::cout << "0x" << std::setw(16) << std::hex << res.address 
                  << std::setw(10) << std::dec << (res.isUnicode ? "[U] " : "[A] ") 
                  << res.value << std::endl;
        if (++count >= 50) break;
    }

    if (results.size() > 50) {
        std::cout << "... ve " << (results.size() - 50) << " sonuc daha var." << std::endl;
    }

    std::cout << "\n[*] Tum sonuclari 'scan_results.txt' dosyasina kaydediyorum..." << std::endl;
    
    std::ofstream file("scan_results.txt");
    if (file.is_open()) {
        for (const auto& res : results) {
            file << "0x" << std::hex << res.address << std::dec << " | " << (res.isUnicode ? "[U] " : "[A] ") << res.value << "\n";
        }
        file.close();
        std::cout << "[+] Kaydedildi: scan_results.txt" << std::endl;
    } else {
        std::cout << "[-] Dosya olusturulamadi!" << std::endl;
    }
    
    std::cout << "[+] Devam etmek icin Enter'a basin..." << std::endl;
    std::cin.get();

    return 0;
}
