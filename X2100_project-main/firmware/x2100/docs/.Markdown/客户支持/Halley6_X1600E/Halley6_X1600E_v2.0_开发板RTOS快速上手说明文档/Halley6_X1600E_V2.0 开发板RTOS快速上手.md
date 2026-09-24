# **Halley6_X1600E_V2.0 开发板RTOS快速上手**



## 一  硬件介绍

本开发板默认采⽤X1600E + sfc nand

Type-C插⼊供电，Micro_USB插入烧录

串⼝输出：uart2, PB00〜01, 波特率：3000000

![2022-09-26_16-51](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/1.png)







![2022-09-26_16-51_1](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/2.png)



## 二 获取源码方法

### 2.1 提供public key

在获取源码之前，都需要把你开发电脑上的相应的 public key发送给我司进行添加，从而得到获取源码的权限。本地电脑获取 public key的方法如下： 

如果本机还没有 ssh public key,可以通过以下方法生成, 打开一个终端输入下列命令: 

```
jiangwen@uws:~/work/x1000/zk_external_git_test$ ssh-keygen
```

//输入 ssh-keygen 后,使用默认配置,一路回车直到完成即可 

```
jiangwen@uws:~/work/x1000/zk_external_git_test$ cat ~/.ssh/id_rsa.pub
```

请把 id_rsa.pub 内容或文件给到我们. 

同一份代码后期同步更新请直接使用 ./repo sync 命令.

### 2.2 Repo ⼯具下载

```c
$ mkdir project_workspace
$ cd project_workspace
$ wget http://git.ingenic.com.cn:8082/bj/repo
$ chmod +x repo
```

### 2.3 同步代码

同步代码的地址如下： 

```c
./repo init -u ssh://sz_halley2@119.136.25.25:29418/mirror/freertos/manifest.git 
./repo sync 
```

如若报错可使用http同步方式：

```c
./repo init -u http://sz_halley2@119.136.25.25:8089/mirror/freertos/manifest.git
./repo sync
```

注意：

**1. 对于外部客⼾来说，这⾥的帐号不⽤修改，直接使⽤sz_halley2就可以。**

**2. 客⼾下载代码的⽬录路径要⽤全英⽂的，不要带有特殊符号的那种，不然编译可能会有问题**

### 2.4 同步代码可能遇到的问题

主要是因为系统环境,⽹络环境不⼀致导致在同步⼯程过程中出现的⼀些报错信息的解决⽅法

这⾥的获取代码是基于Ubuntu14.04 64bit的环境进⾏的

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
bhu@bhu-PC:~/rtos$ cd freertos/
bhu@bhu-PC:~/rtos/freertos$ source build/envsetup.sh //设置编译器到环境变量
bhu@bhu-PC:~/rtos/freertos$ make x1600e_nand_defconfig //选择编译配置文件
bhu@bhu-PC:~/rtos/freertos$ make    //编译工程代码
```

生成烧录文件：

```c
bhu@bhu-PC:~/rtos/freertos$ ls -l rtos-with-spl.bin 
-rw-r--r-- 1 bhu bhu 436388 12月 21 10:11 rtos-with-spl.bin
```

更多编译相关请参考：doc/开发使用说明/1_FreeRTOS工程编译说明.pdf

## 四 最新烧录工具的获取

**在ubuntu下执⾏如下命令可以免密下载：**

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

![2022-09-27_11-38](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/3.png)



![2022-09-27_14-53](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/4.png)







![2022-09-27_14-59_1](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/5.png)



![2022-09-27_15-00](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/6.png)



烧录步骤：

1.点击“开始”

2.按住开发板BOOT键不放

3.开发板重新上电或者按reset 键复位开发板

更多请参考：doc/开发使用说明/2_FreeRTOS工程烧录简介.pdf：

## 六 IconfigTool修改系统配置

修改配置文件使用可视化配置工具tools/iconfigtool/IConfigToolApp/IConfigTool。 通过此工具可以直观的修改加载软件的配置文件。

```c
./IConfigTool           // 运行配置工具
```

**注意：**如果IConfigTool出现闪退时，删除工具lib/目录下libQtCore.so.4与libQtGui.so.4文件

![7](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/7.png)

Config.in 是生成配置界面文件

deconfig是需要修改的配置文件

点击open进⼊IConﬁgTool配置⼯具主界面

![8](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/8.png)

在此就可以根据自己的软件需求进行可视化修改配置文件freertos/configs/x1600e_nand_defconfig

修改完成后保存配置，并编译系统：

```
点击File选项
选择save进行保存
make x1600e_nand_defconfig
make
```

更多IConfigTool相关请参考：doc/开发使用说明/1_FreeRTOS工程编译说明.pdf

## 七 shell辅助开发命令

君正的freertos系统提供了一些shell调试命令作为辅助开发工具. 具体配置如下：

![9](Halley6_X1600E_v2.0_开发板快速上手说明文档.assets/9.png)

> **shell命令的源码位于freertos/shell/cmds目录下**

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
