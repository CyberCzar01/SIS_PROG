## Описание



### Ядро ОС

**Основные возможности:**
- Загрузка через Multiboot2 протокол
- Инициализация и работа с serial портом (COM1)
- Парсинг Multiboot2 информационной структуры
- Работа с фреймбуфером (VESA/VBE)
- Парсинг ACPI таблиц (RSDP, RSDT, MADT/APIC)
- Вывод диагностической информации

**Ключевые компоненты:**
- `boot.S` - Multiboot2 header и точка входа в 32-bit защищенном режиме
- `kernel.c` - основная логика: парсинг MB2, ACPI, инициализация фреймбуфера
- `fb.c` - функции для работы с графическим буфером
- `serial.c` - функции вывода на serial порт
- `acpi.c` - парсинг ACPI таблиц и MADT (Multiple APIC Description Table)

### UEFI Загрузчик

**Основные возможности:**
- Загрузка и валидация ELF32 файла ядра
- Создание Multiboot2 информационной структуры
- Получение информации о фреймбуфере
- Получение ACPI таблиц из UEFI
- Переход из 64-bit режима в 32-bit совместимый режим
- Передача управления ядру

**Ключевые компоненты:**
- `BootLoader.c` - основная логика загрузки и подготовки окружения
- `Trampoline.S` - ассемблерный код для перехода в 32-bit режим
- ELF32 парсер для загрузки сегментов ядра
- Создание Multiboot2 структур с информацией о фреймбуфере и ACPI

## Сборка проекта



### Сборка ядра

```bash
cd sisproga_lab34/lab3-kernel
make clean
make
```

Результат сборки:
- `kernel.elf` - ELF файл с отладочной информацией
- `kernel.bin` - бинарный файл для загрузки

### Сборка UEFI загрузчика

```bash
cd sisproga_lab34/lab4-uefi-bootloader
```

## Запуск и тестирование

### Подготовка ESP

Скомпилированные файлы находятся в каталоге `esp/`:
- `esp/kernel.bin` - ядро ОС
- `esp/EFI/BOOT/BOOTX64.EFI` - UEFI загрузчик

### Запуск в QEMU для ЛР3

`vm-pci.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

QEMU=~/src/qemu-custom/build/qemu-system-x86_64-unsigned
DISK="/Users/fedorsvetlichniy/qemu/build/disk.qcow2"
BAR="/Users/fedorsvetlichniy/qemu/build/bar2.bin"
ISO="/Users/fedorsvetlichniy/qemu/build/debian-13.1.0-amd64-netinst.iso"
HOSTDIR="/Users/fedorsvetlichniy/Desktop/PCI_Driver"

UEFI_IMG="$HOSTDIR/uefi.img"
OVMF_FW="$HOSTDIR/OVMF.fd"

CPU="-smp 2,sockets=1,cores=2,threads=1"
RAM="-m 2G"
NET="-net nic -net user,hostfwd=tcp::2222-:22"
BAROPTS="-object memory-backend-file,size=64K,share=on,mem-path=$BAR,id=membar2 -device pci-testdev,membar=64K,memdev=membar2"
FS9P="-fsdev local,id=fsdev0,path=$HOSTDIR,security_model=none,readonly=off -device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare"

case "${1:-run}" in
  install)
    exec "$QEMU" $CPU $RAM -display cocoa -serial stdio \
      -hda "$DISK" $NET $BAROPTS $FS9P -cdrom "$ISO" -boot d
    ;;
  run)
    exec "$QEMU" $CPU $RAM -display cocoa -serial stdio \
      -hda "$DISK" $NET $BAROPTS $FS9P -boot c
    ;;
  headless)
    exec "$QEMU" $CPU $RAM -display none -monitor none \
      -chardev stdio,id=char0,signal=off -serial chardev:char0 \
      -hda "$DISK" $NET $BAROPTS $FS9P -boot c
    ;;
  stop)
    pkill -f qemu-system-x86_64-unsigned || true
    ;;
  *)
    echo "usage: $0 {install|run|headless|lab4|stop}"
    exit 1
    ;;
esac
```

### Параметры запуска QEMU

Скрипт `vm-pci.sh` настраивает:
- **CPU**: 2 ядра (1 сокет, 2 ядра, 1 поток)
- **RAM**: 2 ГБ
- **Сеть**: User networking с пробросом порта 2222
- **PCI**: Тестовое PCI устройство с memory bar
- **9P**: Файловая система для обмена файлами с хостом
- **UEFI**: Используется OVMF firmware

## Что ожидается при запуске

При успешном запуске в консоли вы увидите:

```
=== LAB3 kernel start ===
[RAW] mb_magic=0x36D76289 mb_info=0x00110220
[MB2] info @ 0x00110220 total_size=1136
[MB2] tag type=21 size=12 @ 0x00110228
[MB2] tag type=1 size=9 @ 0x00110238
[MB2] tag type=2 size=20 @ 0x00110248
[MB2] tag type=10 size=28 @ 0x00110260
[MB2] tag type=6 size=184 @ 0x00110280
[MB2] tag type=9 size=740 @ 0x00110338
[MB2] tag type=4 size=16 @ 0x00110620
[MB2] tag type=5 size=20 @ 0x00110630
[MB2] tag type=8 size=32 @ 0x00110648
[MB2] tag type=14 size=28 @ 0x00110668
[MB2] ACPI tag=14 rsdp_copy@0x00110670 rev=0 sig=RSD PTR 
[MB2] tag type=0 size=8 @ 0x00110688
[MB2] END tag
[ACPI] RSDP copy @ 0x00110670
[ACPI] signature: RSD PTR 
[ACPI] revision: 0
[ACPI] rsdt_address: 0x7FFE239C
[ACPI] xsdt_address: 0x0000000800000000
[ACPI] RSDT @ 0x7FFE239C sig=RSDT len=52
[ACPI] RSDT entries: 4
[ACPI]  [0] 0x7FFE2248 sig=FACP len=116
[ACPI]  [1] 0x7FFE22BC sig=APIC len=128
[ACPI]  -> Found MADT/APIC at 0x7FFE22BC
[MADT] @ 0x7FFE22BC sig=APIC len=128 rev=3
[MADT] local_apic_addr=0xFEE00000 flags=0x00000001
[MADT] #0 off=44 type=0 len=8 : Local APIC: ACPI_ID=0 APIC_ID=0 flags=0x00000001
[MADT] #1 off=52 type=0 len=8 : Local APIC: ACPI_ID=1 APIC_ID=1 flags=0x00000001
[MADT] #2 off=60 type=1 len=12 : I/O APIC: id=0 addr=0xFEC00000 gsi_base=0
[MADT] #3 off=72 type=2 len=10 : ISO: bus=0 src_irq=0 gsi=2 flags=0
[MADT] #4 off=82 type=2 len=10 : ISO: bus=0 src_irq=5 gsi=5 flags=13
[MADT] #5 off=92 type=2 len=10 : ISO: bus=0 src_irq=9 gsi=9 flags=13
[MADT] #6 off=102 type=2 len=10 : ISO: bus=0 src_irq=10 gsi=10 flags=13
[MADT] #7 off=112 type=2 len=10 : ISO: bus=0 src_irq=11 gsi=11 flags=13
[MADT] #8 off=122 type=4 len=6 : Local APIC NMI: acpi_id=255 flags=0 lint=1
[MADT] done
=== LAB3 done, halting ===
```
### Для запуска ЛР4:

`OVMF_VARS.fd` рекомендуется пересоздавать из `OVMF_VARS_TEMPLATE.fd` перед запуском, чтобы UEFI не запоминал старые Boot записи.

Для отладки используется вывод в `-serial stdio`.

После успешного `ExitBootServices` нельзя вызывать `Boot Services`; управление сразу передаётся трамплину.

Сборка UEFI загрузчика

Сначала нужно перекинуть файлы ```{BootLoader.c, BootLoader.inf, Trampoline.S, Elf32.h, Mb2.h}``` в каталог ``` ~/edk2/MdeModulePkg/Application/BootLoader```

Далее нужно отредактировать файл ```MdeModulePkg.dsc```:
 -  Открыть файл ```MdeModulePkg.dsc``` в текстовом редакторе.
 - Найти строку "Components" и добавить туда строку ```MdeModulePkg/Application/BootLoader/BootLoader.inf```
 Чтобы выглядело вот так:

 ```
 [Components]
    MdeModulePkg/Application/BootLoader/BootLoader.inf
    MdeModulePkg/Applictaioon/HelloWorld.inf
    MdeModulePkg/Applictaioon/DumpDynPcd/DumpDynPcd.inf
    MdeModulePkg/Applictaioon/MemoryProfileInfo/MemoryProfileInfo.inf
```

После чего:

```bash
cd sisproga_lab34/lab4-uefi-bootloader
# Или ваш путь, где лежит
```
Нужно будет сделать сборку, и как только сделан build, который я делел командой:
```bash
source edksetup.sh

build -a X64 -t GCC5 -b DEBUG \
  -p MdeModulePkg/MdeModulePkg.dsc \
  -m MdeModulePkg/Application/Bootloader/BootLoader.inf
```
Вам нужно будет перекинуть два файла: 
```bash
# Я делал так:
mkdir -p ~/sisproga_lab34/esp/EFI/BOOT

cp -v /home/<Имя в системе>/edk2/Build/MdeModulePkg/DEBUG_GCC5/X64/BootLoader.efi \
  ~/sisproga_lab34/esp/EFI/BOOT/BOOTX64.EFI
# Тут не нужно перекидывать ядро, как я понял, вы его не собираете, поэтому используйте то, что я разместил на git, но обязательно нужно, чтобы оно было в папке esp (esp/kernel.bin)

# Потом потом нужно скопировать себе на хост папку esp, после чего только добавить туда моё ядро kernel.bin
```
Дальше вводите эту команду со своими данными:
```bash
qemu-system-x86_64 \
  -m 4G \
  -serial stdio \
  -drive if=pflash,format=raw,readonly=on,file="$HOME/qemu/ovmf/OVMF_CODE.fd" \
  -drive if=pflash,format=raw,file="$HOME/qemu/ovmf/OVMF_VARS.fd" \
  -drive if=virtio,format=raw,file=fat:rw:"/Users/fedorsvetlichniy/Desktop/PCI_Driver/esp/"
```
### При запуске ЛР4 увидите:
```
BdsDxe: loading Boot0001 "UEFI Misc Device" from PciRoot(0x0)/Pci(0x4,0x0)
BdsDxe: starting Boot0001 "UEFI Misc Device" from PciRoot(0x0)/Pci(0x4,0x0)
BootLoader: kernel path: \kernel.bin
[BL] GOP: 1280x800 fb=C0000000
[BL] Entry=001021EA MbInfo=BDE57000
[BL] TrampolineStart=BDE52708 End=BDE52788 size=128
[BL] paddrTramp=BDE55000
[BL] tramp bytes: FA 89 CE 48 83 EC 10 48 8D 05 5A 00 00 00 66 C7

=== LAB3 kernel start ===
[RAW] mb_magic=0x36D76289 mb_info=0xBDE57000
[MB2] info @ 0xBDE57000 total_size=128
[MB2] tag type=2 size=23 @ 0xBDE57008
[MB2] tag type=8 size=38 @ 0xBDE57020
[MB2] framebuffer addr=0x00000000C0000000 1280x800 pitch=5120 bpp=32 type=1
[MB2] tag type=15 size=44 @ 0xBDE57048
[MB2] ACPI tag=15 rsdp_copy@0xBDE57050 rev=2 sig=RSD PTR 
[MB2] tag type=0 size=8 @ 0xBDE57078
[MB2] END tag
[ACPI] RSDP copy @ 0xBDE57050
[ACPI] signature: RSD PTR 
[ACPI] revision: 2
[ACPI] rsdt_address: 0xBF77D074
[ACPI] xsdt_address: 0x00000000BF77D0E8
[ACPI] RSDT @ 0xBF77D074 sig=RSDT len=56
[ACPI] RSDT entries: 5
[ACPI]  [0] 0xBF779000 sig=FACP len=116
[ACPI]  [1] 0xBF778000 sig=APIC len=120
[ACPI]  -> Found MADT/APIC at 0xBF778000
[MADT] @ 0xBF778000 sig=APIC len=120 rev=3
[MADT] local_apic_addr=0xFEE00000 flags=0x00000001
[MADT] #0 off=44 type=0 len=8 : Local APIC: ACPI_ID=0 APIC_ID=0 flags=0x00000001
[MADT] #1 off=52 type=1 len=12 : I/O APIC: id=0 addr=0xFEC00000 gsi_base=0
[MADT] #2 off=64 type=2 len=10 : ISO: bus=0 src_irq=0 gsi=2 flags=0
[MADT] #3 off=74 type=2 len=10 : ISO: bus=0 src_irq=5 gsi=5 flags=13
[MADT] #4 off=84 type=2 len=10 : ISO: bus=0 src_irq=9 gsi=9 flags=13
[MADT] #5 off=94 type=2 len=10 : ISO: bus=0 src_irq=10 gsi=10 flags=13
[MADT] #6 off=104 type=2 len=10 : ISO: bus=0 src_irq=11 gsi=11 flags=13
[MADT] #7 off=114 type=4 len=6 : Local APIC NMI: acpi_id=255 flags=0 lint=1
[MADT] done
=== LAB3 done, halting ===
```


