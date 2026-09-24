# 构建与烧录

## 构建环境

- Linux或WSL
- 仓库自带XBurst2交叉工具链
- 当前支持配置：`x2100_Cheetah_RadarEye_nor_defconfig`

## 根目录构建

```bash
make radar
```

清理后重建：

```bash
make radar-rebuild JOBS=4
```

也可进入SDK直接构建：

```bash
cd firmware/x2100/freertos
make x2100_Cheetah_RadarEye_nor_defconfig
make -j2
```

## 输出

- `zero.elf`：调试符号固件。
- `zero.bin`：RTOS主体二进制。
- `rtos-with-spl.bin`：包含SPL的烧录镜像。

## 烧录

USBCloner位于 `firmware/x2100/tools/USBCloner/`。优先使用x2100 Cheetah RadarEye NOR OTA配置。两个平台安装包保留在本地，但由根 `.gitignore` 排除，不上传GitHub。

烧录前应确认板卡供电、USB启动模式、NOR分区布局和实际使用的镜像文件。

## 配置路径注意事项

厂商USBCloner `.cfg` 中带有原开发电脑的绝对路径。首次烧录时应在USBCloner界面重新选择本仓库中的SPL和固件，不能直接依赖配置里的 `/home/chenph/...` 或 `/mnt/work/...` 路径。

当前SPL位于 `firmware/x2100/tools/USBCloner/spl/`，固件位于 `firmware/x2100/freertos/`。
