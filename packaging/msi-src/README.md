# KradDeviceInfo

**krad.device.info** — Windows uchun to'liq apparat va dasturiy ta'minot
inventarizatsiya vositasi. C++17 + Qt (Widgets) da yozilgan, tashqi
kutubxonalarsiz (faqat WinAPI / WMI / SMBIOS / PDH).

![Overview](docs/screenshot_overview.png)

## Imkoniyatlar

| Modul | Tafsilotlar |
|-------|-------------|
| **Overview** | Real-time gauge'lar (CPU/RAM/GPU/Disk), tarix grafiklari, tezkor ma'lumot |
| **CPU** | Brend, yadro soni, soket, keshlar (L1/L2/L3), ko'rsatmalar (AVX-512...), hipervizor, kod nomi, harorat |
| **Memory** | Modul darajasida SPD (ishlab chiqaruvchi, part-number, MT/s), slotlar, ECC, virtual xotira |
| **GPU** | DXGI + SetupAPI: VRAM, vendor/device id, drayver versiya/sana, video rejim |
| **Storage** | Har bir disk: NVMe/SATA/USB, SSD/HDD, sog'liq (health), firmware, TRIM, partitsiyalar, volume'lar |
| **Network** | Adapterlar: MAC, IPv4/IPv6, gateway, DNS, DHCP, tezlik, trafik hisoblagichlari |
| **Devices** | Monitorlar (EDID parse: ishlab chiqaruvchi, seriya, native rezolyutsiya), USB qurilmalar, batareya (kishsayish %), audio |
| **BIOS** | SMBIOS raw parser (GetSystemFirmwareTable): UUID, board seriya, boot rejimi (UEFI/Legacy), Secure Boot |
| **Software** | O'rnatilgan dasturlar (HKLM/HKCU + Wow64), autostart, xizmatlar (services) |
| **Benchmark** | CPU (1-thread / N-thread), xotira (bandwidth + latency), disk (seq + 4K random IOPS) |
| **Report** | JSON / HTML / TXT / CSV eksport — professional HTML hisobot |

## Tizim talablari

- **Windows 7 x64 yoki yangirogi** (Win 8/8.1/10/11, Server 2008R2+)
- Qt 5.15 runtime (statik yoki dinamik)
- CPU: SSE4.2 (2011+ protsessorlar)

> Eski Windows (XP/Vista) uchun core qatlam moslashgan — faqat Qt 5.6
> bilan qayta qurish kerak (qarang `CMakePresets.json`).

## Qurish (Windows)

### MSVC (Visual Studio 2022)
```bat
cmake --preset win-msvc
cmake --build build-msvc --config Release
```

### MinGW-w64
```bat
cmake --preset win-mingw
cmake --build build-mingw
```

Talablar: CMake 3.16+, Qt 5.15 (Widgets + Concurrent), MSVC 2019+ yoki
MinGW-w64 (GCC 9+, **posix** threads varianti tavsiya etiladi).

### Linux (demo rejim — ishlab chiqish/CI uchun)
```bash
cmake --preset linux-demo
cmake --build build-linux
```
Linux build real /proc ma'lumotlarini o'qiydi va GUI'ni to'liq sinov
qilish imkonini beradi.

## CLI (buyruq qatori)

```bat
kraddeviceinfo --export json -o C:\reports\pc.json
kraddeviceinfo --export html
kraddeviceinfo --bench cpu,memory,disk --duration 10
kraddeviceinfo --monitor --interval 500
kraddeviceinfo --help
```

GUI-subsystem exe bo'lsa ham konsolga chiqish ishlaydi
(`AttachConsole`).

## Portable / Installer / Download sayt

- **Portable zip**: `dist/KradDeviceInfo-1.0.0-portable-win64.zip` (~10.5 MB)
- **Installer**: `makensis packaging/installer.nsi` → `dist/KradDeviceInfo-Setup-1.0.0.exe`
- **Smoke test**: `build/krad_smoke.exe` — barcha kollektorlar konsolga

### Download sayti (Cloudflare Pages)

`site/` papkasi to'liq tayyor landing + yuklab olish sahifasi:

```bash
# 1-variant: wrangler CLI
./deploy.sh                      # yoki: npx wrangler pages deploy site --project-name kraddeviceinfo

# 2-variant: dashboard
# dash.cloudflare.com -> Workers & Pages -> Create -> Pages -> Upload assets -> site/ papkasini tanlang

# 3-variant: Git push (CI/CD)
# .github/workflows/build.yml — Windows exe build + Pages deploy avtomatik
```

URL: `https://<project-name>.pages.dev`

### Cross-compile (Linux'dan Windows exe)

```bash
pip install aqtinstall
python3 -m aqt install-qt windows desktop 5.15.2 win64_mingw81 --outputdir /opt/qt-win
cmake -B build-win-gui --toolchain packaging/mingw-win-qt.cmake \
  -DKRAD_HOST_MOC=/usr/lib/qt5/bin/moc -DKRAD_HOST_RCC=/usr/lib/qt5/bin/rcc \
  -DKRAD_HOST_LRELEASE=/usr/lib/qt5/bin/lrelease
cmake --build build-win-gui
```

## Arxitektura

```
include/krad/model.h        Qt-free ma'lumot modeli
src/core/                   util, report, benchmark (portable)
src/platform/win32/         Win32/WMI/PDH/SMBIOS kollektorlar
src/platform/linux_backend  /proc asosidagi demo backend
src/app/                    main, CLI, eksport (JSON/HTML/TXT/CSV)
src/gui/                    Qt GUI: tema, gauge/chart, 10 sahifa
tests/                      win_smoke, screenshot tool
packaging/                  NSIS installer
```

Core qatlam **Qt-free** — mingw bilan alohida tekshiriladi, boshqa
loyihalarga ham osongina ulanadi.

## Litsenziya

MIT — qarang [LICENSE](LICENSE).
