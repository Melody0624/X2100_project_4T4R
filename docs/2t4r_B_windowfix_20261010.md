# B 版 USB 虚拟串口卡住：DSP 启动空指针修复

## 实测证据

用户烧录 `B_updated_source_2T4R_TX01_8CH_2M_OTA_20261010.cfg` 后提供的物理 UART 日志显示：

- 1.270 秒：`g_serial ready`。
- 1.558 秒：`usb gadget serial connect!!!`，CDC 已枚举成功。
- 3.606 秒、第 2 帧：`TLB (load or instruction fetch)`，`epc: 8016d834 badva: 00000000`。
- 调用栈包含 `detection_processing`，随后 `Game over`，日志停止。

所以 USB 已初始化，后续固件异常使主机的虚拟串口操作无法继续获得响应。日志前端乱码、`rootfs NOT FOUND` 不是此次已经定位的崩溃位置。

## 根因和修复

原 B 包 ELF 反汇编在 `0x8016d828` 从 `0x801e3830` 加载指针，符号表确认该位置是 `winRng_sf1`。异常指令 `0x8016d834` 使用该指针读取第一个距离窗系数；寄存器及 `badva=0` 证明它仍为 NULL。

新版 `set_params.c` 只保留距离、多普勒、测角窗的 NULL 指针定义，检测库内部管理窗函数。B 版保留旧检测源码后仍使用这三个全局指针，适配时遗漏了配套初始化。

现已在 B 源码 `set_parameters()` 中补齐：506 点 Blackman 距离窗、128 点 Hanning 多普勒窗、8 点 Hanning 测角窗。使用原工程的窗函数实现和静态共享存储，内存池释放/重建后仍有效。波形、检测/CFAR/测角/后处理源码、CRT 库、USB 协议及用户八通道补偿保持原 B 设置。

## 烧录修正版

独立目录：`artifacts/2t4r_tx01_8ch_compare_20261010/B_updated_source_windowfix/`。

配置：`B_updated_source_windowfix_2T4R_TX01_8CH_2M_OTA_20261010.cfg`。

使用该目录的 SPL 和 `zero_ota.bin`；两个 OTA 槽均写入修正版。`force_erase=1` 会清除旧 Flash 参数和标定，首次启动使用用户八通道编译默认值。原 A/B 包及 USBCloner 原 spl/img 文件保留。

## 验证和限制

先将主机测试从手动构造缓冲/窗函数改为实际 SDK 的 `set_parameters()` 和 `init_memory_pool()`，在修复前复现三个窗指针 NULL 的断言失败。修复后实际初始化、内存池释放再初始化、6 个完整 ADC/RFFT/BPM/DFFT/DDMA/CFAR/AoA/后处理用例通过 ASan/UBSan。

交叉编译和烧录布局/容量/校准系数验证结果见修正版目录中的 `verification.txt`、`build.log`、`BUILD_MANIFEST.json` 和 `pipeline.log`。主机测试采用便携 FFT，不覆盖板上 SIMD、CRT 库及真实 USB 事务。修正版需要重新烧录实测，确认连续处理不再出现 `Game over`，并测试上位机连接。

复现编译：`tools/build_2t4r_tx01_compare.sh B_updated_source_windowfix`。
