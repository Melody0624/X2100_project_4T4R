# 			PD_X2600_VAST_V2.0开发板RTOS快速上手

## 一 硬件介绍

本开发板默认采⽤X2600E + sfc nand

供电：Type-C插⼊可以烧录&供电，不需额外供电

串⼝输出：uart2, PD12〜13, 波特率：3000000

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/1.png" alt="1" style="zoom:150%;" />

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/2.png" alt="2" style="zoom:150%;" />

## 二 获取源码方法

### 2.1 提供public key

在获取源码之前，**都需要把你开发电脑上的相应的 public key发送给我司进⾏添加**，从⽽得到获取源码的下载权限。本地电脑获取public key的⽅法如下：

如果本机还没有 ssh public key,可以通过以下⽅法⽣成, 打开⼀个终端输⼊下列命令:

```
jiangwen@uws:~/work/x1000/zk_external_git_test$ ssh-keygen
```

//输⼊ ssh-keygen 后,使⽤ 默认配置,⼀路回⻋直到完成即可

```
jiangwen@uws:~/work/x1000/zk_external_git_test$ cat ~/.ssh/id_rsa.pub
ssh-rsa 
AAAAB3NzaC1yc2EAAAADAQABAAABAQCpl0B4jXob25BV9lK+/DdSz8Grxb4OUvbflo2c/POp5/ZZd
bIvnyebs8Dy2nA8KcpRMBP5rx7KcGJ1We/Khv6xvdSypYDdymZgfMAjoSqTQOtGrpkYAWNMkB4qh
cfKUeVTRh0Y9qfbxdO6iAZ7yOeuwr4D5oK71ZJ2FImVy/X35ENQL4ct2bABBSKC8NR0nRbuD6m37f
Ns2jFXkSWHI+jJcr3pRvhdSpFLWQqDmeVd9StfndnD2j42eM8bHdCJSqGNYEZLcEjSn0udS7H7mrAaI
kqGUkoZKhdKv7Q19kJPJW+qGXt/cd9W2dZLoLtm/NmpDIk4CZe54nD5mM/zKTdh jiangwen@uws 
```

请把 id_rsa.pub 内容或文件给到我们。 

同一份代码后期同步更新请直接使用  ./repo sync  命令 。

### 2.2 Repo ⼯具下载

```
$ mkdir project_workspace
$ cd project_workspace
$ wget http://git.ingenic.com.cn:8082/bj/repo
$ chmod +x repo
```

### 2.3 同步代码

同步代码的地址如下：

```
./repo init -u ssh://sz_halley2@119.136.25.25:29418/mirror/freertos/manifest.git 
./repo sync 
```

如若报错可使⽤http同步⽅式：

```
./repo init -u http://sz_halley2@119.136.25.25:8089/mirror/freertos/manifest.git
./repo sync
```

注意：

**1, 对于外部客⼾来说，这⾥的帐号不⽤修改，直接使⽤sz_halley2就可以。**

**2, 客⼾下载代码的⽬录路径要⽤全英⽂的，不要带有特殊符号的那种，不然编译可能会有问题**

### 2.4 同步代码可能遇到的问题

主要是因为系统环境,⽹络环境不⼀致导致在同步⼯程过程中出现的⼀些报错信息的解决⽅法。

这⾥的获取代码是基于Ubuntu14.04 64bit的环境进⾏的。

**问题1：** 
**Unable to negotiate with xxx.xxx.xxx.xxx port 29418: no matching key exchange method found.** 

**Their offer: diffie-hellman-group1-sha1** 

解决方法：

请修改~/.ssh/config，加入 

Host * 
        

KexAlgorithms +diffie-hellman-group1-sha1 

若没有 config 文件请建立一个 



**问题2：-bash: ./repo: No such file or directory**

解决⽅法：

系统缺少repo⼯具或没有添加环境变量

wget http://git.ingenic.com.cn:8082/bj/repo (注:此链接的repo⼯具为君正修改过后放在服务器上
的，也可使⽤⾕歌提供的repo⼯具)

chmod +x repo



**问题3：Permission denied (publickey).**

fatal: Could not read from remote repository.

解决⽅法：

请确认是否有权限同步代码，同步代码需提交key于我司开通权限



**问题4：Their offer: diffie-hellman-group1-sha1**

解决⽅法：

修改~/.ssh/config，加⼊

Host *

KexAlgorithms +diffie-hellman-group1-sha1



**问题5：aes128-ctr，aes192-ctr，aes256-ctr**

解决⽅法：

修改 /etc/ssh/ssh_config ⽂件

删除Ciphers aes128-ctr，aes192-ctr，aes256-ctr….⾏前注释符号



**问题6：git config --global user.name "yourname"**

git config --global user.email your@email.com

解决⽅法：

若不需要提交代码⾄服务器可直接跳过执⾏后续步骤

若需要提交代码请根据提⽰命令注册姓名及邮箱地址

git config --global user.name "your name"

git config --global user.email "you email"



**问题7：Traceback (most recent call last)**

File "/home/jdai/work/test1/.repo/repo/main.py", line 385, in
_Main(sys.argv[1:])

File "/home/jdai/work/test1/.repo/repo/main.py", line 365, in _Main
result = repo._Run(argv) or 0

File "/home/jdai/work/test1/.repo/repo/main.py", line 137, in _Run

解决⽅法：

rm –rf *.*repo (repo前⾯有*.*)

./repo init –u ssh://……….

使⽤repo init 同步没有执⾏完会产⽣缓存需删除缓存后再次执⾏。



**问题8：Testing colorized output (for 'repo diff', 'repo status')**

black red green yellow blue magenta cyan white
bold dim ul reverse

Enable color display in this user account (y/N)?

解决⽅法：

直接按回⻋键

注意：若在同步过程中⻓时间卡住不动，有可能是因为库太⼤，或进程卡死，请ctrl+c 键退出再执
⾏，⽀持断点续传。



**问题9：Bad owner or permissions on .ssh/config**

解决⽅法：

sudo chmod 600 .ssh/config

## 三 编译方法

编译所需依赖库（若编译报错需看具体报错信息） 

sudo apt-get install autopoint 

sudo apt-get install liblzo2-dev 

sudo apt-get install build-essential 

sudo apt-get install automake autoconf    m4    libtool gettext 

sudo apt-get install zlib1g-dev 

sudo  apt-get  install  libncurses5-dev  libslang2-dev  libselinux1-dev  debhelper  lsb-release 
pkg-config po-debconf 

```c
sxyzhang@T430:~/my/work$ cd freertos/
sxyzhang@T430:~/my/work/freertos$ source build/envsetup.sh//设置编译器到环境变量
sxyzhang@T430:~/my/work/freertos$ make x2600e_vast_nand_defconfig //选择编译配置文件
sxyzhang@T430:~/my/work/freertos$ make          //编译工程代码
```

生成烧录文件：

```c
sxyzhang@T430:~/my/work/freertos$ ll rtos-with-spl.bin
-rw-rw-r-- 1 sxyzhang sxyzhang 540088 11月  7 09:55 rtos-with-spl.bin
```

更多编译相关请参考：doc/开发使用说明/1_FreeRTOS工程编译说明.pdf

## 四 最新烧录工具获取

在ubuntu下执⾏如下命令可以免密下载：

**ubuntu版本：**

```
wget ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-ubuntu.tar.gz
```

**windows版本：**

```
wget ftp://szingenic:hq7Wy0gws@ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-windows.zip
```

## 五 烧录方法

烧录工具解压后，启动烧录⼯具:  sudo ./cloner

**注意：**在 linux 下需⽤ root 权限打开烧录⼯具，⽅可烧录。

打开烧录软件 以下为第⼀个界⾯----->选择 config 进⼊配置界⾯

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/3.png" alt="3" style="zoom:150%;" />

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/4.png" alt="4" style="zoom:150%;" />

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/5.png" alt="5" style="zoom:150%;" />

上图中，设置烧录分区的偏移地址(offset)和⽂件⼤⼩(size)以及分区名称(partition name) 。 

label必须选箭头下来框中可选的，必须与sfc-》分区信息  中的partition name一致。

offset必须与sfc-》分区信息  中对应分区的其实地址保持一致。

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/6.png" alt="6" style="zoom:150%;" />

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/7.png" alt="7" style="zoom:150%;" />

在SFC的基本信息中建议将全部擦出勾选中，以免上⼀次的烧写对现在产⽣影响。

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/8.png" alt="8" style="zoom:150%;" />

其他选项卡默认就好。

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/9.png" alt="9" style="zoom:150%;" />

烧录步骤：

1.点击“开始”

2.按住开发板BOOT键不放

3.开发板重新上电或者按reset 键复位开发板

更多请参考：doc/开发使用说明/2_FreeRTOS工程烧录简介.pdf

## 六 IconfigTool修改系统配置

修改配置文件使用可视化配置工具tools/iconfigtool/IConfigToolApp/IConfigTool。 通过此工具可以直观的修改加载软件的配置文件。

```c
./IConfigTool           // 运行配置工具
```

**注意：**如果IConfigTool出现闪退时，删除工具lib/目录下libQtCore.so.4与libQtGui.so.4文件

 <img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/10.png" alt="10" style="zoom: 200%;" />

Config.in 是生成配置界面文件

config是需要修改的配置文件

点击open进⼊IConﬁgTool配置⼯具主界⾯。

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/11.png" alt="11" style="zoom: 200%;" />

在此就可以根据自己的软件需求进行可视化修改配置文件freertos/configs/x2600e_vast_nand_defconfig。

修改完成后保存配置，并编译系统：

```
点击File选项
选择save进行保存
make x2600e_vast_nand_defconfig
make
```

更多IConfigTool相关请参考：doc/开发使用说明/1_FreeRTOS工程编译说明.pdf

## 七 shell辅助开发命令

君正的freertos系统提供了一些shell调试命令作为辅助开发工具。具体配置如下：

<img src="PD_X2600_VAST_V2.0开发板RTOS快速上手.assets/12.png" alt="12" style="zoom:150%;" />

shell命令的源码位置：

```c
sxyzhang@T430:~/my/work$ ll freertos/shell/cmds/
总用量 144
drwxrwxr-x 4 sxyzhang sxyzhang 4096 9月  18 17:13 ./
drwxrwxr-x 3 sxyzhang sxyzhang 4096 11月  7 09:54 ../
-rw-rw-r-- 1 sxyzhang sxyzhang 1136 7月  11  2022 cmds_cat.c  // 常用系统工具的shell命令源码
-rw-rw-r-- 1 sxyzhang sxyzhang  682 7月  11  2022 cmds_cd.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1212 7月  11  2022 cmds_cp.c
-rw-rw-r-- 1 sxyzhang sxyzhang 3906 9月  18 17:13 cmds_devmem.c
-rw-rw-r-- 1 sxyzhang sxyzhang  801 7月  11  2022 cmds_df.c
-rw-rw-r-- 1 sxyzhang sxyzhang 3073 7月  11  2022 cmds_echo.c
-rw-rw-r-- 1 sxyzhang sxyzhang  692 7月  11  2022 cmds_help.c
-rw-rw-r-- 1 sxyzhang sxyzhang 6467 7月  11  2022 cmds_hexdump.c
-rw-rw-r-- 1 sxyzhang sxyzhang  658 7月  11  2022 cmds_history.c
-rw-rw-r-- 1 sxyzhang sxyzhang 2437 7月  11  2022 cmds_insmod.c
-rw-rw-r-- 1 sxyzhang sxyzhang  955 11月 18  2022 cmds_iperf_server.c
-rw-rw-r-- 1 sxyzhang sxyzhang  897 7月  11  2022 cmds_ls.c
-rw-rw-r-- 1 sxyzhang sxyzhang  675 7月  11  2022 cmds_lsmod.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1281 11月 18  2022 cmds_mac_info.c
-rw-rw-r-- 1 sxyzhang sxyzhang 6282 7月  11  2022 cmds_mem.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1927 4月  25  2023 cmds_memtester.c
-rw-rw-r-- 1 sxyzhang sxyzhang 4013 11月 18  2022 cmds_mkdir.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1485 7月  11  2022 cmds_mv.c
-rw-rw-r-- 1 sxyzhang sxyzhang 6425 11月 18  2022 cmds_ping.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1421 7月  11  2022 cmds_pm.c
-rw-rw-r-- 1 sxyzhang sxyzhang  743 7月  11  2022 cmds_pwd.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1044 7月  11  2022 cmds_reset.c
-rw-rw-r-- 1 sxyzhang sxyzhang 6755 7月  11  2022 cmds_rm.c
-rw-rw-r-- 1 sxyzhang sxyzhang 1297 7月  11  2022 cmds_rmmod.c
-rw-rw-r-- 1 sxyzhang sxyzhang 2040 7月  11  2022 cmds_thread_dump.c
-rw-rw-r-- 1 sxyzhang sxyzhang  766 7月  11  2022 cmds_thread_list.c
-rw-rw-r-- 1 sxyzhang sxyzhang 7144 7月  11  2022 cmds_top.c
drwxrwxr-x 4 sxyzhang sxyzhang 4096 2月   8  2023 drivers/   // 芯片驱动相关的shell命令源码
drwxrwxr-x 2 sxyzhang sxyzhang 4096 7月  11  2022 os/        // rtos系统相关的shell命令源码
```

以gpio为例演示辅助开发工具如下：

```
gpio_get_func_des
功能：查看io⼝功能说明
参数：⽆
example：
          gpio_get_func_des
```

```
gpio_get_func  <GPIO>
功能：获取指定IO功能状态
参数：gpio                       //io⼝的名字
example：
          gpio_get_func PA 
```

```
gpio_set_func <GPIO> <FUNCTION>
功能：设定指定IO功能
参数：gpio                      //io⼝的名字
     function                  //io⼝功能
example：
          gpio_set_func PA  OUTPUT 
```

```
gpio_set_value <GPIO> <value>
功能：设置io电平
参数：gpio                    //io⼝的名字
     level                   //io⼝功能
 example：
         gpio_set_value PA  0
"注：io⼝必须为output状态"
```

```
gpio_get_value <GPIO>
功能：获取io电平
参数：gpio                    //io⼝的名字
 example：
         gpio_set_value PA 
```

更多shell命令详解请参考：doc/开发使用说明/FreeRTOS_Shell辅助开发.pdf

## 八 快速开发及调试

客户的使用过程中有如下高频需求：

1 添加客户⾃⼰的代码

2 添加源⽂件,加⼊通⽤编译流程

3 在外部编译静态库⽂件

4 添加⽬标⽂件,加⼊通⽤编译流程

5 添加⾃动检索头⽂件⽬录

6  添加宏控可在IconfigTool中修改

7 ⽤宏来控制⽂件是否加⼊编译流程

针对上述高频需求的详细说明，请参考：doc/开发使用说明/3_FreeRTOS快速开发文档.pdf

## 九 FAT文件系统的应用

客户有fat文件系统的需求，请参考：doc/开发使用说明/1_FreeRTOS工程编译说明.pdf