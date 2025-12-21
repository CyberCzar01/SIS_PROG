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
