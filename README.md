# Memory Checker

## English

Memory Checker is a high-performance Windows memory scanning tool written in C++ utilizing an ImGui and DirectX 11 based user interface. It allows users to attach to live running processes and scan for string values (both ASCII and UTF-16) within their memory pages in real-time. It features a modern, clean, and highly responsive user interface inspired by the Visual Studio Enterprise theme.

### Features
* Live process listing and quick filtering.
* Fast memory scanning for ASCII and UTF-16 strings.
* Search and refine functionality to narrow down scan results.
* Capability to extract and automatically open file paths found within memory strings.
* Copy addresses and string values directly to the clipboard.
* Hardware-accelerated, efficient DirectX 11 user interface.

### Requirements
* Windows 10 or Windows 11
* Visual Studio (with C++ Desktop Development tools installed)
* DirectX 11

### How to Build
1. Open `MemoryChecker.slnx` or `MemoryChecker.vcxproj` using Visual Studio.
2. Select your desired build configuration (e.g., Release x64).
3. Build the solution (Ctrl + Shift + B).
4. The executable will be generated in the output directory (e.g., `x64/Release`).

---

## Türkçe

Memory Checker, C++ ile geliştirilmiş, ImGui ve DirectX 11 tabanlı kullanıcı arayüzüne sahip, yüksek performanslı bir Windows bellek tarama aracıdır. Kullanıcıların aktif olarak çalışan işlemlere (process) bağlanmasına ve bellek sayfalarındaki metin değerlerini (ASCII ve UTF-16) gerçek zamanlı olarak taramasına olanak tanır. Visual Studio Enterprise temasından ilham alan modern, temiz ve son derece tepkisel bir kullanıcı arayüzü sunar.

### Özellikler
* Anlık işlem (process) listeleme ve hızlı filtreleme.
* ASCII ve UTF-16 metinler için hızlı bellek taraması.
* Tarama sonuçlarını daraltmak için arama ve filtreleme (refine) işlevi.
* Bellek dizgeleri içinde bulunan dosya yollarını tespit etme, ayıklama ve otomatik olarak açma yeteneği.
* Adresleri ve metin değerlerini doğrudan panoya kopyalama.
* Donanım hızlandırmalı, verimli DirectX 11 kullanıcı arayüzü.

### Gereksinimler
* Windows 10 veya Windows 11
* Visual Studio (C++ Masaüstü Geliştirme araçları yüklü olmalıdır)
* DirectX 11

### Nasıl Derlenir
1. Visual Studio ile `MemoryChecker.slnx` veya `MemoryChecker.vcxproj` dosyasını açın.
2. İstediğiniz derleme yapılandırmasını (örn. Release x64) seçin.
3. Çözümü (Solution) derleyin (Ctrl + Shift + B).
4. Çalıştırılabilir dosya (.exe) çıktı dizininde (örn. `x64/Release`) oluşturulacaktır.
