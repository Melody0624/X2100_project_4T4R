# FreeRTOS工程烧录简介



## 1.烧录说明

本章简单说明烧录工具的使用以及各分区对应的烧录文件。

获取最新的烧录工具方法如下：

###  ubuntu版本

方式一：

[ubuntu版本烧录工具请下载](ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-ubuntu.tar.gz)

方式二：

```shell
wget ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-ubuntu.tar.gz
```

### windows版本

方式一：

[windows版本烧录工具请下载](ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-windows.zip)

方式二：

```shell
wget ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-windows.zip
```

下载完成后，解压烧录工具，进入烧录工具的目录，启动烧录工具: `sudo ./cloner`

`(注意：1.在 linux 下需用 root 权限打开烧录工具，方可烧录。2.烧录工具版本和配置文件名可能会更新，请以实际拿到的配置为准确)`

`打开烧录软件 以下为第一个界面----->选择 config 进入配置界面`

![1](img/1.png)



## 1.1.配置对应的板级信息

![2](img/2.png)

1.这里选择自己对应的板级配置，以x1000为例`（我的板级使用nor flash作为存储器， 实际以自己的硬件配置来选择如果是nand flash 则选nand）。`



## 1.2.查看并设置分区信息

![3](img/3.png)

设置烧录分区的偏移地址(offset)和文件大小(size)以及分区名称(partition name) ，`(其中rootfs名称不能改动)`



## 1.3.选择要烧录的镜像文件

![4](img/4.png)

1.选择将要烧录的镜像文件,label 选择freertos 和rootfs。

2.`在ops选择存储器类型`，板级是nor flash 就选SFC_NOR，nand falsh 就选SFC_NAND选择与自己存储器相对应的类型。

3.最后在setting选项栏选择将要烧录的镜像文件`(生成的启动引导文件为freertos/rtos-with-spl.bin, 文件系统镜像文件为freertos/fat-test.img)`

4.选择保存

这里烧录的是文件，所以type选择文件，offset 为烧录的偏移地址。



## 1.4.烧录前进行全部擦除

![5](img/5.png)

在SFC的基本信息中建议将全部擦出勾选中，以免上一次的烧写对现在产生影响。



## 1.5.进行烧录

![6](img/6.png)



![7](img/7.png)

烧录步骤：

1.点击软件开始

2.按住开发板BOOT键不放

3.开发板重新上电或者按reset 键复位开发板

4.如上界面表示烧录完成 （可能在擦除的时候会比较久，耐性等待）

