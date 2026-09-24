# X2100 Cheetah 无人机避障雷达

## 项目目录

```text
.
├── firmware/x2100/                       # X2100 厂商 SDK 与固件工程
│   ├── freertos/                         # FreeRTOS 主工程
│   │   ├── configs/                      # 各芯片和开发板的编译配置
│   │   │   ├── x2100_Cheetah_RadarEye_nor_defconfig  # 当前雷达工程配置
│   │   │   └── x2100_Cheetah_nor_defconfig           # X2100 Cheetah 配置
│   │   ├── os/freertos/                  # FreeRTOS 内核
│   │   ├── xburst2/soc-x2000/            # X2000/X2100 底层 SoC 支持
│   │   ├── devices/camera/x2000/cheetah/ # Cheetah MIPI 数据接收
│   │   │   └── sensor_cheetah.c          # Cheetah 传感器驱动入口
│   │   ├── drivers/                      # USB、OTA 等通用驱动
│   │   ├── example/                      # GPIO、USB、系统等示例代码
│   │   ├── vendor/                       # 当前雷达业务代码
│   │   │   ├── cheetah/                  # Cheetah 雷达芯片控制
│   │   │   ├── motor_cycle_demo/         # 两轮车雷达处理工程
│   │   │   │   ├── src/detection/        # CFAR、测角和检测后处理
│   │   │   │   └── src/set_params.c      # 雷达参数与阵列配置
│   │   │   ├── mmw_msg_pkt/              # 雷达消息封装
│   │   │   └── uart_cli/、uart_trans/     # 串口命令与数据传输
│   │   ├── filesystem/、net/、shell/      # 文件系统、网络和命令行
│   │   ├── third_party/                  # 第三方组件与算法库
│   │   ├── Makefile、Config.in           # 工程构建与功能配置入口
│   │   └── zero.elf、zero.bin、rtos-with-spl.bin  # 编译和烧录文件
│   ├── bootloader/
│   │   └── README                        # 启动程序说明
│   ├── tools/
│   │   ├── toolchains/                   # XBurst2 交叉编译工具链
│   │   ├── USBCloner/                    # 固件烧录工具及配置
│   │   ├── iconfigtool/                  # 固件配置工具
│   │   └── TuningTool/                   # 雷达调试辅助脚本
│   └── docs/
│       ├── 开发使用说明/                 # FreeRTOS 编译、烧录和 API 文档
│       ├── 芯片手册/                     # X2000/X2100 数据手册
│       └── FAE文档/                      # 厂商补充技术资料
├── project/170radar/                     # 雷达硬件和项目资料
│   ├── hardware/
│   │   ├── mt-2t4r-rev-a/
│   │   │   ├── *.DSN                    # OrCAD 原理图工程
│   │   │   ├── *.brd                    # PCB 设计文件
│   │   │   └── *.pdf                    # 原理图 PDF
│   │   ├── mechanical/radar-module-structure.pdf  # 结构图
│   │   └── archive/*.rar                # 原始硬件资料归档
│   ├── datasheets/
│   │   ├── Cheetah4401M_Datasheet_V1.4.pdf
│   │   ├── X2100_Datasheet_v1.2.pdf
│   │   └── RV1126B_Datasheet_V1.5.pdf
│   ├── protocol/Haikaibao_Radar_Protocol.docx    # 雷达通信协议
│   └── reference/
│       ├── SenardMicro_Near_Range_Radar_Demo.pdf # 参考方案
│       └── radar-board-photo.png                  # 雷达板照片
├── host-tools/
│   └── MotorCycleTools-2.4.1/
│       ├── MotorCycle_Tools.exe          # Windows 雷达上位机
│       ├── 两轮车雷达使用手册 V1.4.pdf
│       ├── MotorCycle Tools 使用说明[2.2.6].docx
│       └── *.dll、platforms/             # 上位机运行依赖
├── docs/
│   ├── ARCHITECTURE.md                   # 系统架构说明
│   ├── BUILD_AND_FLASH.md                # 编译与烧录说明
│   ├── REPOSITORY_LAYOUT.md              # 仓库目录说明
│   └── 2T4R_TO_4T4R.md                   # 4T4R 改造分析
├── Makefile                              # 根目录构建入口
└── README.md                             # 项目目录说明
```
