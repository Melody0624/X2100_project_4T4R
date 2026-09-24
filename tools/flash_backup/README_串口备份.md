# X2100 Flash 串口只读备份

本工具通过固件 Shell 的 `nor_read` 命令读取 XT25F16，不执行擦除或写入。

## 使用前提

- 串口中执行 `nor_read 0x1DB000 0x20`能够返回`read_buf:`和32字节数据。
- 关闭MobaXterm、上位机及其他占用该COM口的软件。
- 默认串口参数为460800、8N1、无流控。

## 备份配置区

在Windows PowerShell中执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
& "D:\downloads\X2100_project-main\tools\flash_backup\dump_x2100_flash.ps1" -Port COM3
```

将`COM3`替换为实际调试串口。默认读取：

- 起始地址：`0x1DB000`
- 长度：`0x25000`
- 输出长度：151552字节

脚本会完整读取两次，只有两次SHA-256一致且开头魔数为`56 44 41 52`时，才生成`*_verified.bin`。
开始正式读取前，脚本会先读取32字节并检查配置魔数。默认每次读取256字节，以降低串口输出丢失的概率。

## 备份整片Flash

配置区备份成功后再执行：

```powershell
& "D:\downloads\X2100_project-main\tools\flash_backup\dump_x2100_flash.ps1" -Port COM3 -Region Full
```

整片输出长度应为2097152字节。串口文本传输量较大，需要等待。

## 输出文件

文件保存在脚本目录下的`output`文件夹：

- `*_read1.bin`：第一次读取；
- `*_read2.bin`：第二次读取；
- `*_verified.bin`：两次一致后生成的备份；
- `*_manifest.txt`：地址、长度和SHA-256记录。

如果两次读取不一致，脚本保留两个文件并报错。此时不要擦除或烧录Flash。
