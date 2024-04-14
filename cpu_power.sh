#!/bin/bash
set -eu

if [ $# -ne 1 ]; then
  >&2 echo Usage: $0 '(performance|powersave|show)'
  exit 2
fi

action="$1"
case "$action" in
performance)
  # Cステートを無効化
  for cpu in /sys/devices/system/cpu/cpu*/cpuidle/state*; do
    echo 1 | sudo tee $cpu/disable > /dev/null
  done

  # Pステートをパフォーマンスモードに設定
  for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu > /dev/null
  done
  ;;
powersave)
  # Cステートを有効化
  for cpu in /sys/devices/system/cpu/cpu*/cpuidle/state*; do
    echo 0 | sudo tee $cpu/disable > /dev/null
  done

  # Pステートを省電力に設定
  for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo powersave | sudo tee $cpu > /dev/null
  done
  ;;
show)
  echo --- cpu idle state ---
  cat /sys/devices/system/cpu/cpu*/cpuidle/state*/disable
  echo --- cpu freq ---
  cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
  ;;
esac
