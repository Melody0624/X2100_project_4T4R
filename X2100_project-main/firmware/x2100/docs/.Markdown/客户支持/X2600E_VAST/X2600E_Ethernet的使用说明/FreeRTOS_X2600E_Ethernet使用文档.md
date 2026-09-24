# FreeRTOS_X2600E_Ethernet 使用文档

## 一 功能简介

MAC x1

- 支持 10/100Mbps
- 支持 RMII PHY 接口

## 二 软件配置

### 2.1 有线网卡驱动配置

打开iconfigtool配置页面

*以PD_X2600E_VAST_V2.0开发板为例 , 使用x2600e_vast_nand_defconfig配置 , 实际根据硬件需要配置*

![1](FreeRTOS_X2600E_Ethernet使用文档.assets/1.png)





MAC驱动配置

![2](FreeRTOS_X2600E_Ethernet使用文档.assets/2.png)

> **mdio时钟影响phy的初始化时间, 如果初始化不过, 可以尝试修改**

IPv4配置

![3](FreeRTOS_X2600E_Ethernet使用文档.assets/3.png)



### 2.2 lwip协议栈配置

![4](FreeRTOS_X2600E_Ethernet使用文档.assets/4.png)

⽹络协议栈配置：

lwip⽹络协议栈只做了简单的配置, 区分 ipv6和apps

apps包括

altcp_tls http lwiperf mdns mqtt netbiosns smtp snmp sntp tftp

## 三 编译和烧录

```c
 bhu@bhu-PC:~/rtos$ cd freertos                      
 bhu@bhu-PC:~/rtos/freertos$  source build/envsetup.sh        //第一次编译需要初始化编译环境
 bhu@bhu-PC:~/rtos/freertos$ make x2600e_vast_nand_defconfig
 bhu@bhu-PC:~/rtos/freertos$ make 
```

```c
bhu@bhu-PC:~/rtos/freertos$ ls rtos-with-spl.bin 
rtos-with-spl.bin                                           //编译出来的文件
```

**请使用最新版烧录工具**

```c
wget ftp://ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-ubuntu.tar.gz 
wget ftp://ftp.ingenic.com.cn/DevSupport/Tools/USBBurner/cloner-latest-windows.zip
```

**烧录配置**

![5](FreeRTOS_X2600E_Ethernet使用文档.assets/5.png)

![6](FreeRTOS_X2600E_Ethernet使用文档.assets/6.png)

![7](FreeRTOS_X2600E_Ethernet使用文档.assets/7.png)

## 四 测试验证

网络验证

```c
$ mac_info                                                        //查看网卡信息            
[14760.452082] network interface device: et0 (Default)
[14760.452273]   MTU: 1500
[14760.452366]   MAC: 52 9d 3d 5e 49 c5 
[14760.452506]   FLAGS: UP LINK_UP ETHARP BROADCAST IGMP
[14760.452701]   ip address: 10.4.3.32
[14760.452833]   gw address: 10.4.3.1
[14760.452963]   net mask  : 255.255.255.0
  
$ ping 10.4.3.9                                                 //测试网卡链接                    
[14827.646159] 60 bytes from 10.4.3.9 icmp_seq=0 ttl=64 time=1 ms
[14828.646903] 60 bytes from 10.4.3.9 icmp_seq=1 ttl=64 time=0 ms
[14829.647617] 60 bytes from 10.4.3.9 icmp_seq=2 ttl=64 time=0 ms
[14830.648327] 60 bytes from 10.4.3.9 icmp_seq=3 ttl=64 time=1 ms

```

iperf测试

```c
$ iperf_server                                //启动iperf的tcp服务器
[6.136974] iperf tcp server start

bhu@bhu-PC:~/work$ iperf -c 10.4.3.32                       //客户端发送信息
------------------------------------------------------------
Client connecting to 10.4.3.32, TCP port 5001
TCP window size: 45.0 KByte (default)
------------------------------------------------------------
[  3] local 10.4.3.9 port 39238 connected with 10.4.3.32 port 5001
[ ID] Interval       Transfer     Bandwidth
[  3]  0.0-10.0 sec   110 MBytes  92.5 Mbits/sec


$ [121.684463] IPERF report: type=0, remote: 10.4.3.9:39238, total bytes: 115604504, duration in ms: 10010, kbits/s: 92384
//服务端接收信息
```

