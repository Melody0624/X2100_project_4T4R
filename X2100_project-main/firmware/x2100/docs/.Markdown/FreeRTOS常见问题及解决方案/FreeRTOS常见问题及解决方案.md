# FreeRTOS常见问题及解决方案



## 1.执行make操作，编译器未找到

make: mips-linux-gnu-gcc: Command not found

> 没有设置编译器到系统运行环境变量，切换到最顶层工程目录下设置环境变量指定自己交叉编译器的路径
>
> 解决办法： #cd freertos && source build/envsetup.sh



## 2.执行make操作，libstdc++.so.6共享库未找到

mips-linux-gnu-gcc:error while loading shared libraries: libstdc++.so.6: cannot open shared object file: No such file or director

> 编译工具是32位，系统缺少32位兼容包，下载32位兼容包
>
> 解决方法：sudo apt-get install lib32stdc++6



## 3.运行./IConfigTool编译裁剪工具时闪退

Cannot mix incompatible Qt library (version 0x40807) with this library (version 0x40806) Aborted (core dumped)

> IConfigTool自带的libQt与系统的libQt冲突，故删除IConfigTool自带的libQt，使用自己系统的libQt
>
> 解决方法： 删除 tools/iconfigtool/IConfigToolApp/lib下的 libQtCore.so.4 和 libQtGui.so.4两个文件



## 4.在使用烧录软件cloner时发现下载不进去

> 烧录器没有权限访问usb设备
>
> 解决方法：使用sudo命令 sudo ./cloner