# 		CIM&SD_电源域切换说明文档

## 一 背景技术

### 	1.1 应用场景

x2000、x2100、x2600可以支持CIM 和 SD的外接供电电压1.8v和3.3v切换。也就是说，这些主控芯片的外接sensor设备和SD设备的供电压可以是1.8v或3.3v。但是必须保证主控寄存器的CIM和SD电压设置与外设供电电压保持一致，不然会有损坏主控芯片的风险。

​	本文主要讲述x2000、x2100、x2600搭载rtos 和linux系统时，根据外设供电电压设置主控寄存器的CIM和SD电压的方法。有两种方法：

1， 利用ADC硬件单元来检测输入电压值，程序**自动设置**主控寄存器的CIM和SD电压。

2，客户根据硬件设计来判断当前CIM和SD的工作电压，从而**手动设置**主控寄存器的CIM和SD电压。

### 1.2 芯片寄存器设置

x2000、x2100、x2600的CIM 和 SD电压设置寄存器如下：

![1](CIM&SD_电源域切换说明文档.assets/1.png)

章节1.1中涉及的两种设置方法最终设置的都是此寄存器的BIT30~31，此寄存器的物理地址为：0x100000e0。

**注意：此处的电压设置，须与硬件设计的CIM和SD供电电压一致，不然会有损坏主控芯片的风险。**

## 二 rtos下切换电压

### 2.1 用ADC自动检测电压

以PD_X2600E_VAST_V2.0开发板为例，展示利用ADC12~13来检测输入电压值，程序**自动设置**主控寄存器的CIM、SD电压。硬件设计如下：

![2](CIM&SD_电源域切换说明文档.assets/2.png)

上图可知，当前硬件设置VDDIO_CIM接的是VCC_1V8. 

![10](CIM&SD_电源域切换说明文档.assets/3.png)

上图可知，当前硬件设置VDDIO_SD接的是VCC_3V3.

![3](CIM&SD_电源域切换说明文档.assets/4.png)

上图为ADC12~13检测电压的硬件设计。

根据硬件设计，R162和R163作为等值的分压电阻，当前的ADC12如果检测到900mV左右就是正确的。软件上将ADC12获取到900mV左右的值作为设置EXCLK_DS对应BIT30 = 1的依据。

R164和R165作为等值的分压电阻，当前的ADC13如果检测到1.65V左右就是正确的。软件上将ADC13获取到1.65V左右的值作为设置EXCLK_DS对应BIT31 = 0的依据。

针对当前硬件设计，软件配置如下：

![4](CIM&SD_电源域切换说明文档.assets/5.png)

上图选择自动检测电压的配置。

![5](CIM&SD_电源域切换说明文档.assets/6.png)

![12](CIM&SD_电源域切换说明文档.assets/7.png)

上图展示根据硬件设计进一步设置ADC通道及对应的分压电阻R0和R1的阻值。

![6](CIM&SD_电源域切换说明文档.assets/8.png)

上图选中adc驱动。

保存上述配置并重新编译烧录之后，系统启动log信息如下：

![6-6](CIM&SD_电源域切换说明文档.assets/9.png)

### 2.2 硬件写死电压值

以PD_X2600E_VAST_V2.0开发板为例，展示**手动设置**主控寄存器的CIM、SD电压。硬件参考设计同2.1章节，不再赘述。软件配置如下：

![8](CIM&SD_电源域切换说明文档.assets/10.png)

![8-1](CIM&SD_电源域切换说明文档.assets/11.png)

保存上述配置并重新编译烧录之后，系统启动log信息如下：

![6-6](CIM&SD_电源域切换说明文档.assets/9.png)

### 2.3 确认当前电压设置

![15](CIM&SD_电源域切换说明文档.assets/12.png)

此处打印的0x100000e0寄存器的当前值0x40000006的BIT31~30与1.2章节的芯片寄存器设置相吻合，同时进一步佐证了，当前硬件设计CIM电压为1.8v， 而SD电压为3.3v。

## 三 linux 下切换电压

### 3.1 用ADC自动检测电压

以PD_X2600E_VAST_V2.0开发板为例，展示**ADC自动设置**主控寄存器的CIM、SD电压。硬件参考设计同2.1章节，不再赘述。软件配置如下：

![16](CIM&SD_电源域切换说明文档.assets/13.png)

上图选中adc模块驱动。

![17](CIM&SD_电源域切换说明文档.assets/14.png)

上图选中adc shell命令有效。

![18](CIM&SD_电源域切换说明文档.assets/15.png)

上图选中adc读取cim和sd电压的命令。

![19](CIM&SD_电源域切换说明文档.assets/16.png)

![19-1](CIM&SD_电源域切换说明文档.assets/17.png)

上图选中buildroot中开机自动运行adc读取cim、sd电压的脚本。

保存上述配置并重新编译烧录之后，系统启动log信息如下：

![20](CIM&SD_电源域切换说明文档.assets/18.png)

### 3.2 硬件写死电压值

以PD_X2600E_VAST_V2.0开发板为例，展示**手动设置**主控寄存器的CIM、SD电压。硬件参考设计同2.1章节，不再赘述。软件配置如下：![19](CIM&SD_电源域切换说明文档.assets/19.png)

取消勾选开机根据adc读取电压值的脚本，会使用kernel dts文件中默认的电压设置。此时先保存上述配置后，重新编译整个系统。下面将详述如何修改kernel dts中默认cim、sd电压，以整体配置文件x2600e_vast_v20_nand_5.10_defconfig为例。

```c
 //获取当前kernel路径及当前配置文件
zxy@vb:~/job/linux/x2600_sz/build$ cat configs/x2600e_vast_v20_nand_5.10_defconfig | grep kernel
APP_kernel_dir=../kernel/kernel
APP_kernel_config=x2600_vast_module_base_linux_sfc_nand_defconfig
```

```c
//定位当当前kernel加载的dts文件
zxy@vb:~/job/linux/x2600_sz/build$ cd ../kernel/kernel
zxy@vb:~/job/linux/x2600_sz/kernel/kernel$ cat arch/mips/configs/x2600_vast_module_base_linux_sfc_nand_defconfig | grep dts
CONFIG_DT_X2600_MODULE_BASE_DTS_FILE="x2600_vast_module_base.dts"
zxy@vb:~/job/linux/x2600_sz/kernel/kernel$ find ./ -name x2600_vast_module_base.dts
./module_drivers/dts/x2600_vast_module_base.dts
```

![20](CIM&SD_电源域切换说明文档.assets/20.png)

打开对应的kernel/kernel/module_drivers/dts/x2600_vast_module_base.dts文件，其中ingenic,gpa_voltage对应cim的默认电压设置，ingenic,gpa_voltage对应sd的默认电压设置。

**注意：**此处只有在取消勾选开机根据adc读取电压值的脚本时才会生效。如果有配置开机根据adc读取电压值的脚本，那么系统会优先使用ADC读到的硬件电压值来设置cpu 0x100000e0寄存器的CIM、SD电压。

修改dts文件中的默认电压之后，需要重新编译kernel：

```c
zxy@vb:~/job/linux/x2600_sz/build$ make kernel
```

### 3.3 确认当前电压设置

![15](CIM&SD_电源域切换说明文档.assets/12.png)

此处打印的0x100000e0寄存器的当前值0x40000006的BIT31~30与1.2章节的芯片寄存器设置相吻合，同时进一步佐证了，当前硬件设计CIM电压为1.8v， 而SD电压为3.3v。