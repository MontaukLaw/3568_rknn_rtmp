# CPU定频
echo "CPU0-3 可用频率:"
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
echo userspace > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
echo 1992000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed
echo "CPU0-3 当前频率:"
cat /sys/devices/system/cpu/cpufreq/policy0/cpuinfo_cur_freq

# NPU定频
echo "NPU 可用频率:"
cat /sys/class/devfreq/fde40000.npu/available_frequencies    
echo userspace > /sys/class/devfreq/fde40000.npu/governor
echo 900000000 > /sys/class/devfreq/fde40000.npu/userspace/set_freq
echo "NPU 当前频率:"
cat /sys/class/devfreq/fde40000.npu/cur_freq