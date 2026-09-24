/mnt/e/Project/Project_RadarParser_Cheetah_MotorCycle_Demo/DETINFO数据结构说明.md
# DETINFO 数据结构说明

本文档描述当 `dataFormat == DATA_TYPE_DAT_DETINFO` 时，`.dat` 文件中 DETINFO（检测点信息）的数据格式。

---

## 1. 整体文件结构

文件按**帧**组织，采用 TLV（Tag-Length-Value）格式。每帧数据的整体结构如下：

┌──────────────────────────────────────────────────────────┐ │ 消息头 (msg_header) │ │ 9 × uint32 = 36 字节 │ ├──────────────────────────────────────────────────────────┤ │ [0] magic word 低位 = 0x03040102 │ │ [1] magic word 高位 = 0x07080506 │ │ [3] totalPacketLen │ │ [5] frameNumber (帧号) │ │ [6] numTLVs (本帧 TLV 数量) │ └──────────────────────────────────────────────────────────┘ │ TLV 1 (Tag + Length + Payload) │ │ TLV 2 ... │ │ TLV N ... │


**消息头定义**（`header/parse_dat_detInfo.h:26-30`）：

| 字段 | 位置 | 类型 | 值 / 说明 |
|------|------|------|-----------|
| magic word 低位 | msg_header[0] | uint32 | `0x03040102` |
| magic word 高位 | msg_header[1] | uint32 | `0x07080506` |
| totalPacketLen | msg_header[3] | uint32 | 整个数据包长度 |
| frameNumber | msg_header[5] | uint32 | 帧号 |
| numTLVs | msg_header[6] | uint32 | 本帧 TLV 数量 |

Magic Word 的小端字节序列为：`02 01 04 03 06 05 08 07`

---

## 2. TLV Tag 标识

（`header/parse_dat_detInfo.h:32-36`）

| Tag 值 | 常量名 | 含义 | 解析行为 |
|--------|--------|------|----------|
| **21** | `TLV_TAG_DETINFO` | **检测点信息** | **解析**并填充到 `DetInfo` 结构体 |
| 22 | `TLV_TAG_TRACKINFO` | 航迹信息 | 跳过 payload |
| 23 | `TLV_TAG_EGO_VLC` | 自车速度 | 跳过 payload |
| 24 | `TLV_TAG_WARNINFO` | 告警信息 | 跳过 payload |

> 当前程序仅解析 **Tag=21** 的 DETINFO，其他 TLV 仅移动文件指针跳过。

---

## 3. DETINFO Payload 格式（Tag = 21）

核心检测点数据结构定义在 `src/parse_dat_detInfo.c:51-73` 的注释中。

### 3.1 Payload 头部

| 字段 | 类型 | 字节数 | 说明 |
|------|------|--------|------|
| numDets | uint32 | 4 | 检测点总数量 |
| numStaticDets | uint32 | 4 | 静态检测点数量 |

### 3.2 单检测点数据（每点 15 个字段 × 4 字节 = 60 字节）

对每个检测点，依次读取以下字段（均为 4 字节）：

| 序号 | 字段名 | 二进制类型 | 结构体字段类型 | 说明 |
|------|--------|------------|----------------|------|
| 1 | relRDIdx | uint32 | `int` | 关联的 RD 检测点索引 |
| 2 | vlc | float | `float` | 径向速度 (m/s) |
| 3 | x_output | float | `float` | 输出坐标系 X (m) |
| 4 | y_output | float | `float` | 输出坐标系 Y (m) |
| 5 | motion_state | uint32 | `int` | 运动状态 (0=静止, 1=停止, 2=运动) |
| 6 | pwr | float | `float` | 功率 |
| 7 | snr | float | `float` | 信噪比 |
| 8 | isPeak | uint32 | `bool` | 是否为峰值点 |
| 9 | rng | float | `float` | 距离 (m) |
| 10 | vlc_amb | float | `float` | 模糊速度 (m/s) |
| 11 | vlc_disAmb_conf | float | `int` | 速度解模糊置信度（float → int，四舍五入） |
| 12 | vlc_disAmb_fac | float | `int` | 速度解模糊因子（float → int，四舍五入） |
| 13 | azm_deg | float | `float` | 方位角 (deg) |
| 14 | x_rcs | float | `float` | RCS 坐标系 X (m) |
| 15 | y_rcs | float | `float` | RCS 坐标系 Y (m) |

### 3.3 字节布局示意

┌──────────────────────────────────────────────────────┐ │ numDets uint32 (4 字节) │ │ numStaticDets uint32 (4 字节) │ ├──────────────────────────────────────────────────────┤ │ 检测点 0 (共 60 字节): │ │ ├─ relRDIdx [4 字节, uint32] │ │ ├─ vlc [4 字节, float] │ │ ├─ x_output [4 字节, float] │ │ ├─ y_output [4 字节, float] │ │ ├─ motion_state [4 字节, uint32] │ │ ├─ pwr [4 字节, float] │ │ ├─ snr [4 字节, float] │ │ ├─ isPeak [4 字节, uint32] │ │ ├─ rng [4 字节, float] │ │ ├─ vlc_amb [4 字节, float] │ │ ├─ vlc_disAmb_conf [4 字节, float→int] │ │ ├─ vlc_disAmb_fac [4 字节, float→int] │ │ ├─ azm_deg [4 字节, float] │ │ ├─ x_rcs [4 字节, float] │ │ └─ y_rcs [4 字节, float] │ ├──────────────────────────────────────────────────────┤ │ 检测点 1 (同检测点 0 格式) ... │ │ ... │ │ 检测点 (numDets-1) ... │ └──────────────────────────────────────────────────────┘


### 3.4 Payload 总字节数

Payload 总长度 = 8 + numDets × 60 字节


| 部分 | 字节数 |
|------|--------|
| 消息头（整帧） | 36 字节 |
| TLV Tag + Length | 8 字节 |
| DETINFO 头部（numDets + numStaticDets） | 8 字节 |
| 每检测点 | 60 字节 |

---

## 4. 对应 C 结构体

### 4.1 DetObj — 单个检测点

（`header/radar_types.h:201-225`）

```c
typedef struct {
    int relRDIdx;               // 关联的 RD 检测索引
    float pwr;                  // 功率
    float snr;                  // 信噪比
    bool isPeak;                // 是否峰值
    float rng;                  // 距离 (m)
    float vlc_amb;              // 模糊速度 (m/s)
    float vlc;                  // 真实速度 (m/s)
    int vlc_disAmb_conf;        // 速度解模糊置信度
    int vlc_disAmb_fac;         // 速度解模糊因子
    float azm_deg;              // 方位角 (deg)
    float x_rcs;                // RCS 坐标系 X (m)
    float y_rcs;                // RCS 坐标系 Y (m)
    float z_rcs;                // RCS 坐标系 Z (m)   ⚠️ 不在二进制中
    float x_output;             // 输出坐标系 X (m)
    float y_output;             // 输出坐标系 Y (m)
    int motion_state;           // 运动状态
    bool isInStaticZone;        // 是否在静止区域        ⚠️ 不在二进制中
    int assocStatus;            // 关联状态              ⚠️ 不在二进制中
    int assocTrkID[2];          // 关联航迹ID[纵,横]     ⚠️ 不在二进制中
    bool isForVlcUpdate[2];     // 是否用于速度更新       ⚠️ 不在二进制中
    float assocTrkVlc[2];       // 关联航迹速度           ⚠️ 不在二进制中
    float assocTrkVlcDiff[2];   // 关联速度差             ⚠️ 不在二进制中
    float secondAng;            // 第二角度               ⚠️ 不在二进制中
} DetObj;
```

### 4.2 DetInfo — 单帧检测点集合

（`header/radar_types.h:228-232`）

```c
typedef struct {
    int numDets;                        // 检测点数量
    int numStaticDets;                  // 静态检测点数量
    DetObj detObj[MAX_DETECTIONS];      // 检测点数组 (MAX_DETECTIONS = 256)
} DetInfo;
```

### 4.3 运动状态枚举

（`header/radar_types.h:65-67`）

| 值 | 常量 | 含义 |
|----|------|------|
| 0 | `MOTION_STATUS_STATIC` | 静止 |
| 1 | `MOTION_STATUS_STOP` | 停止（运动→静止） |
| 2 | `MOTION_STATUS_MOVE` | 运动 |

---

## 5. 解析流程

（`src/parse_dat_detInfo.c:190-289`）

1. **搜索 Magic Word**：逐字节搜索 8 字节的魔数序列 `{02 01 04 03 06 05 08 07}`，搜索上限 500 字节
2. **读取消息头**：读取剩余 7 个 uint32，拼接为完整 9 个 uint32 的消息头
3. **获取帧信息**：从消息头提取 `frameNumber` 和 `numTLVs`
4. **遍历 TLV**：对每个 TLV：
   - 读取 `tag` 和 `payload_length`
   - 若 `tag == 21`（DETINFO），调用 `_parse_detInfo_payload()` 解析
   - 其他 tag → 调用 `fseek()` 跳过 payload

---

## 6. 最大检测点数限制

- **`MAX_DETECTIONS = 256`**（`header/radar_types.h:40`）
- 若文件中 `numDets > 256`，只解析前 256 个检测点，剩余数据通过 `fseek` 跳过

---

## 7. 相关源文件

| 文件 | 说明 |
|------|------|
| `header/radar_types.h` | 数据结构体定义（DetObj, DetInfo 等） |
| `header/parse_dat_detInfo.h` | TLV 格式常量、Magic Word、TLV Tag 定义 |
| `src/parse_dat_detInfo.c` | DETINFO 解析实现（`_parse_detInfo_payload()`） |
| `RadarParser_Cheetah_MotorCycle_Demo.cpp` | 主程序，`dataFormat` 选择入口 |
