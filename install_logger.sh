#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
# One-time installer for the scooter ride logger.
# Run on the Pi (over SSH):   sudo bash /boot/firmware/install_logger.sh
set -e
BOOT=/boot/firmware
[ -d "$BOOT" ] || BOOT=/boot
echo "boot partition: $BOOT"
mkdir -p "$BOOT/rides"
# Stop any serial login shell that would compete for the UART.
for u in serial-getty@ttyAMA0 serial-getty@ttyS0 serial-getty@serial0; do
  systemctl disable --now "$u" 2>/dev/null || true
done
# Install the service, patching the boot path in case it is /boot not /boot/firmware.
sed "s#/boot/firmware#$BOOT#g" "$BOOT/pi-scooter-logger.service"   > /etc/systemd/system/pi-scooter-logger.service
systemctl daemon-reload
systemctl enable --now pi-scooter-logger.service
sleep 1
echo "----- status -----"
systemctl --no-pager --full status pi-scooter-logger.service | head -n 15
echo
echo "Done. Logs will appear in $BOOT/rides/  (read them by putting the SD in a PC)."
echo "Reboot to confirm auto-start:  sudo reboot"
