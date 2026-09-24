from pathlib import Path
from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_ALIGN_VERTICAL, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Inches, Pt, RGBColor


ROOT = Path(r"D:\downloads\X2100_project-main")
OUT = ROOT / "artifacts" / "X2100_Cheetah_雷达移植当前状态交接_20260916.docx"
OUT.parent.mkdir(parents=True, exist_ok=True)

doc = Document()
section = doc.sections[0]
section.top_margin = Cm(2.1)
section.bottom_margin = Cm(2.0)
section.left_margin = Cm(2.2)
section.right_margin = Cm(2.2)


def set_run_font(run, east_asia="Microsoft YaHei", latin="Aptos", size=10.5,
                 bold=False, color="000000"):
    run.font.name = latin
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = RGBColor.from_string(color)
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), east_asia)
    run._element.get_or_add_rPr().rFonts.set(qn("w:ascii"), latin)
    run._element.get_or_add_rPr().rFonts.set(qn("w:hAnsi"), latin)


def set_cell_shading(cell, fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_margins(cell, top=100, start=120, bottom=100, end=120):
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for tag, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{tag}"))
        if node is None:
            node = OxmlElement(f"w:{tag}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def set_repeat_table_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def set_keep_with_next(paragraph, value=True):
    p_pr = paragraph._p.get_or_add_pPr()
    node = p_pr.find(qn("w:keepNext"))
    if node is None:
        node = OxmlElement("w:keepNext")
        p_pr.append(node)
    node.set(qn("w:val"), "1" if value else "0")


def add_page_number(paragraph):
    paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = paragraph.add_run()
    fld_char1 = OxmlElement("w:fldChar")
    fld_char1.set(qn("w:fldCharType"), "begin")
    instr_text = OxmlElement("w:instrText")
    instr_text.set(qn("xml:space"), "preserve")
    instr_text.text = " PAGE "
    fld_char2 = OxmlElement("w:fldChar")
    fld_char2.set(qn("w:fldCharType"), "end")
    run._r.extend([fld_char1, instr_text, fld_char2])
    set_run_font(run, size=9, color="666666")


def add_heading(text, level=1):
    p = doc.add_paragraph(style=f"Heading {level}")
    p.paragraph_format.space_before = Pt(12 if level == 1 else 8)
    p.paragraph_format.space_after = Pt(5)
    set_keep_with_next(p)
    r = p.add_run(text)
    set_run_font(r, size=15 if level == 1 else 12, bold=True)
    return p


def add_para(text="", bold_lead=None, keep=False, mono=False):
    p = doc.add_paragraph()
    p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
    p.paragraph_format.line_spacing = 1.18
    p.paragraph_format.space_after = Pt(5)
    if keep:
        set_keep_with_next(p)
    if bold_lead and text.startswith(bold_lead):
        r1 = p.add_run(bold_lead)
        set_run_font(r1, bold=True)
        r2 = p.add_run(text[len(bold_lead):])
        set_run_font(r2, latin="Consolas" if mono else "Aptos", east_asia="Microsoft YaHei")
    else:
        r = p.add_run(text)
        set_run_font(r, latin="Consolas" if mono else "Aptos", east_asia="Microsoft YaHei",
                     size=9.5 if mono else 10.5)
    return p


def add_bullet(text, level=0):
    p = doc.add_paragraph(style="List Bullet" if level == 0 else "List Bullet 2")
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.line_spacing = 1.12
    r = p.add_run(text)
    set_run_font(r)
    return p


def add_number(text, number):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.75)
    p.paragraph_format.first_line_indent = Cm(-0.55)
    p.paragraph_format.space_after = Pt(4)
    p.paragraph_format.line_spacing = 1.12
    r = p.add_run(f"{number}.  {text}")
    set_run_font(r)
    return p


def add_table(headers, rows, widths=None, font_size=9.2):
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    table.style = "Table Grid"
    hdr = table.rows[0]
    set_repeat_table_header(hdr)
    for i, head in enumerate(headers):
        cell = hdr.cells[i]
        cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER
        set_cell_shading(cell, "1F4E78")
        set_cell_margins(cell)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_after = Pt(0)
        r = p.add_run(head)
        set_run_font(r, size=9.3, bold=True, color="FFFFFF")
        if widths:
            cell.width = Cm(widths[i])
    for ridx, row in enumerate(rows):
        cells = table.add_row().cells
        for i, value in enumerate(row):
            cell = cells[i]
            cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER
            set_cell_margins(cell)
            if ridx % 2 == 1:
                set_cell_shading(cell, "F3F7FA")
            p = cell.paragraphs[0]
            p.paragraph_format.space_after = Pt(0)
            p.paragraph_format.line_spacing = 1.05
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if len(str(value)) < 28 else WD_ALIGN_PARAGRAPH.LEFT
            r = p.add_run(str(value))
            set_run_font(r, size=font_size)
            if widths:
                cell.width = Cm(widths[i])
    doc.add_paragraph().paragraph_format.space_after = Pt(1)
    return table


# Styles
styles = doc.styles
normal = styles["Normal"]
normal.font.name = "Aptos"
normal.font.size = Pt(10.5)
normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
for name in ("Title", "Heading 1", "Heading 2", "Heading 3"):
    style = styles[name]
    style.font.color.rgb = RGBColor(0, 0, 0)
    style.font.name = "Aptos Display"
    style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")

# Some Word templates add a colored bottom border to the built-in Title style.
# Remove it so the title is separated by whitespace only.
title_style_ppr = styles["Title"]._element.get_or_add_pPr()
title_style_border = title_style_ppr.find(qn("w:pBdr"))
if title_style_border is not None:
    title_style_ppr.remove(title_style_border)

footer = section.footer
add_page_number(footer.paragraphs[0])

# Title page
title = doc.add_paragraph(style="Title")
title.alignment = WD_ALIGN_PARAGRAPH.CENTER
title.paragraph_format.space_before = Pt(35)
title.paragraph_format.space_after = Pt(16)
title_ppr = title._p.get_or_add_pPr()
title_border = title_ppr.find(qn("w:pBdr"))
if title_border is not None:
    title_ppr.remove(title_border)
r = title.add_run("X2100 Cheetah 雷达移植当前状态交接")
set_run_font(r, size=24, bold=True)

subtitle = doc.add_paragraph()
subtitle.alignment = WD_ALIGN_PARAGRAPH.CENTER
subtitle.paragraph_format.space_after = Pt(24)
r = subtitle.add_run("用于新对话继续开发  2026年9月16日")
set_run_font(r, size=11, color="555555")

add_para(
    "本文档记录当前可继续执行的最终技术状态，包括软件架构、参数、代码与固件位置、验证结果、已知限制和下一步联调顺序。"
    "中间方案迭代和基础知识问答已省略。核心结论是：4TX4RX 公共 BPM 与 DDMA 软件候选、厂家寄存器表安全接入、USB 输出和 CAN 协议编解码层已经形成；"
    "但真实 4TX 射频波形、阵列映射、幅相标定和 CAN 硬件传输仍未完成，因此不能把当前结果描述为 4TX 实测通过。"
)

add_table(
    ["事项", "当前状态", "结论"],
    [
        ["4TX4RX 信号处理软件", "已实现并通过主机合成测试", "可继续联调，尚非射频验收版本"],
        ["X2100 板端运行", "旧版自测/回放已运行", "最新 CAN 产物清单仍标记 board_tested=false"],
        ["Cheetah 厂家寄存器表", "已原样提取并加入安全校验", "硬件含义与 4TX 对齐仍待厂家确认"],
        ["USB 上位机输出", "已接 MotorCycle Tools 协议", "自测/回放可显示点云，真实 4TX 未验证"],
        ["CAN", "协议编解码和批发送状态机已实现", "控制器/收发器未绑定，没有实际发帧"],
        ["跟踪 航迹 预警 融合", "未实现", "当前算法终点为点云"],
    ],
    widths=[4.4, 5.0, 6.4],
)

doc.add_page_break()

add_heading("1 项目环境与最终工作目录")
add_para("项目根目录：D:\\downloads\\X2100_project-main", mono=True)
add_para("当前主工作目录：D:\\downloads\\X2100_project-main\\migration\\adc_live_4tx4rx_bpm_ddma", mono=True)
add_para("FreeRTOS SDK 位于 WSL，构建脚本会使用独立 SDK 副本，不覆盖原始 SDK。构建产物位于项目根目录的 artifacts 文件夹。")

add_table(
    ["对象", "用途", "当前判断"],
    [
        ["X2100 大开发板", "FreeRTOS、算法、USB、串口和性能自测", "板上没有雷达前端，不能验证真实 ADC/RF"],
        ["小型产品板", "原 2TX4RX 雷达基线和真实前端测试", "可保留原固件做对照；没有 OTA/CAN 不影响算法与 USB 点云基线"],
        ["Cheetah 前端", "FMCW 发射、接收、ADC 数据产生", "4TX 新硬件与寄存器含义尚未完成实测确认"],
        ["MotorCycle Tools 2.4.1", "USB CDC 点云显示与录制", "使用 USB_DOWNLOAD 对应 COM，界面选 115200"],
    ],
    widths=[3.7, 6.1, 6.0],
)

add_heading("2 当前确定的软件方案")
add_para("当前候选方案为 4TX4RX、公共 BPM 加 4TX DDMA。软件源于原 Cheetah C 工程与 X2100 FreeRTOS 移植代码，不是 MATLAB 自动生成代码，也不是 TI SDK。TI 文档只用于核对 DDMA 的通用原理。")

add_heading("2.1 信号处理链", level=2)
add_para("真实或合成 ADC 帧 -> ADC 解包 -> 每路去直流和 Blackman 窗 -> 512 点 Range RFFT -> 公共 BPM 去扰码 -> Hanning 窗 -> 128 点 Doppler FFT -> 8 子带 DDMA 解析 -> Doppler CASO 与 Range CAGO CFAR -> AoA -> 点云 -> USB MotorCycle Tools。")
add_para("当前没有聚类、跨帧关联、跟踪滤波、航迹管理、风险判断、预警和多雷达融合。CAN 也尚未接入点云发送任务。")

add_heading("2.2 采样类型", level=2)
add_para("当前是实采样，不是 IQ 复采样。每个 RX 的每个采样点在负载中只占一个 16 位字；Range 阶段使用 RFFT，FFT 后才形成复数频谱。DDMA 和 BPM 不改变 ADC 的实采样属性。")
add_para("ADC 码流不是可以直接解释的有符号 int16。现有驱动约定为小端、12 位 offset binary、左移 4 位，必须按当前解包逻辑转换。")

add_heading("3 当前统一参数")
add_table(
    ["参数", "当前值", "说明"],
    [
        ["发射 接收", "4TX 4RX", "软件候选，形成 16 个虚拟通道；真实阵列顺序未确认"],
        ["中心频率", "76.5 GHz", "当前统一配置值"],
        ["ADC 采样率", "26.665 MS/s", "实采样"],
        ["每 Chirp 每 RX 有效样点", "506", "固定支持尺寸"],
        ["每帧 Chirp 数", "128", "慢时间 FFT 长度为 128"],
        ["Chirp 起始间隔", "26 us", "不是已确认的完整 Ramp 时间"],
        ["帧周期", "50.328 ms", "约 19.87 Hz"],
        ["调频斜率", "19.531 MHz/us", "当前统一配置值"],
        ["Range FFT", "512 点 RFFT", "506 点补零到 512"],
        ["Doppler FFT", "128 点", "在公共 BPM 处理后执行"],
        ["DDMA 子带", "8 个 每带 16 格", "4 个 TX 候选偏移为 0 1 2 3"],
        ["TX 相位增量候选", "0 45 90 135 度每 Chirp", "未获厂家最终确认"],
        ["公共 BPM", "四路 TX 叠加同一 0/180 度序列", "沿用原 512 项码表前 128 项；每帧起点待确认"],
        ["有效采样时长", "约 18.976 us", "506除以26.665 MS/s"],
        ["采样覆盖带宽", "约 370.62 MHz", "斜率乘采样时长；不是已确认完整 Chirp 带宽"],
        ["ADC 帧负载", "522240 字节", "128乘以每 Chirp 的32字节头和506乘4乘2字节数据"],
    ],
    widths=[4.2, 4.3, 7.4],
    font_size=8.8,
)
add_para("注意：26 us 目前只能称为 Chirp 起始间隔。完整 Ramp time、Idle time、ADC start delay 和完整扫频带宽尚不能从现有资料可靠解码。")

add_heading("4 已完成的软件工作")
add_bullet("建立统一参数入口 radar_config.h，算法、合成 ADC、前端帧尺寸和打包清单共用派生尺寸。")
add_bullet("完成常驻工作缓冲区和 Range/Doppler 数据复用，去除稳态重复 Range FFT 与大量逐点打印。")
add_bullet("优化 Doppler FFT 后，板端旧版自测日志显示平均总处理时间约 43.7 ms，小于 50.328 ms 帧周期，overruns=0，内存占用稳定。该结论来自旧版自测/回放，不代表最新 CAN 产物或真实 RF 已实测。")
add_bullet("DDMA 解析器枚举 8 个子带起点，检查候选接近、空带污染、弱 TX 和非法数值；不确定结果不进入速度、测角和点云。")
add_bullet("加入输入长度、ADC 低位、饱和、帧号、时间戳、缺帧、重帧、乱序和恢复诊断；支持三级诊断输出。")
add_bullet("保留 USB MotorCycle Tools 输出。旧版合成/回放点云可被工具解析；tracks=0 和无预警是当前设计状态。")
add_bullet("从厂家 vendor_bsis.c 的 cheetah_128_512_config 条件分支提取 93 条寄存器/延时操作，保留顺序和条件，增加整表校验、失败行号与停止机制。")
add_bullet("完成 CAN 11 位标准帧协议编解码、点云批开始/点/结束、自检、心跳、控制字解析和升级结果编码；硬件传输仍未绑定。")

add_heading("5 厂家配置接入状态")
add_para("厂家源文件：D:\\xwechat\\xwechat_files\\wxid_3f8betvfbj0322_c74a\\msg\\file\\2026-09\\vendor_bsis.c", mono=True)
add_para("当前提取分支：#elif (CHEETAH_CONFIG == cheetah_128_512_config)。这里是 C 预处理条件分支，不是 Git 分支。")
add_table(
    ["项目", "当前值", "状态"],
    [
        ["寄存器操作数量", "93", "已提取并校验结构"],
        ["IS_MIRROR", "0", "暂存选择，需厂家确认"],
        ["USE_BPM", "1", "暂存选择，需与算法码相位对齐"],
        ["ADC_REPLAY", "0", "暂存选择"],
        ["SAVE_RAW_DATA", "0", "暂存选择"],
        ["USE_USB_OUTPUT", "1", "保留 USB 上位机"],
        ["真实写寄存器", "关闭", "SUPPLIER_RF_ALGORITHM_CONFIRMED=0"],
        ["硬件验证", "未完成", "ACK 不等于读回一致或波形正确"],
    ],
    widths=[4.8, 3.6, 7.5],
)
add_para("live_guarded 会在 SPI 写入前停止。不得通过直接把 READY 或 CONFIRMED 宏改为 1 来绕过保护。厂家表可以安全接入和做软件测试，但只有确认发射编码、ADC 布局和阵列映射后才能正式启用。")

add_heading("6 CAN 当前状态")
add_para("CAN_PROTOCOL.md 对照两份通信协议完成了严格子集实现。启动日志中的 CAN codec selfcheck PASS 只代表内存中的打包自检；transport UNBOUND 表示没有初始化控制器、没有连接收发器、没有发送任何 CAN 帧。")
add_table(
    ["范围", "已完成", "尚未完成"],
    [
        ["协议层", "0x101 0x102 0x103到0x113 0x121 编码；0x001到0x005和0x011解析", "0x021 0x022 OTA 写 Flash"],
        ["传输层", "同步批发送状态机和失败停止", "X2100或MCP2515驱动绑定、队列、超时、总线恢复"],
        ["算法映射", "接口保留协议整数单位", "标定 RCS、合成速度、运动方向和坐标约定"],
        ["硬件", "无", "确认控制器 收发器 引脚 总线编号 波特率并抓包验收"],
    ],
    widths=[3.4, 6.0, 6.5],
)
add_para("协议仍有字段矛盾：运动方向 signed 与 0到180 范围冲突；距离和编号文档上限超出位宽；速度正负物理含义、角度方向和心跳量化不清。当前代码拒绝歧义值，不填假值。算法 power 也不能直接当成标定 RCS，径向速度不能直接当作合成速度或运动方向。")

add_heading("7 验证结果与证据边界")
add_table(
    ["验证项", "结果", "能证明什么", "不能证明什么"],
    [
        ["完整 C 流水线", "222 个合成用例通过", "解包 FFT DDMA CFAR AoA 软件自洽", "真实射频性能和 MIPS 数值时序"],
        ["连续场景", "150 帧 200 次目标检查 30 空帧", "多帧恢复和旧点云清除", "跨帧跟踪和实采稳定性"],
        ["DDMA 解析", "8 锚点及歧义 污染 弱TX测试通过", "软件拒绝不可靠候选", "任意重叠目标都可唯一解出"],
        ["厂家表", "8 种选项组合和逐行失败注入通过", "表结构与错误处理", "寄存器物理含义和真实波形"],
        ["CAN", "全部位宽边界和2000点批测试通过", "协议字节布局", "CAN总线实际收发"],
        ["X2100 旧版自测", "平均约43.7 ms 无超时", "旧版合成/回放性能基线", "最新产物和真实4TX前端"],
        ["USB 工具", "可解析自测/回放点云", "现有 USB 协议兼容", "真实目标探测精度"],
    ],
    widths=[3.0, 4.2, 4.5, 4.8],
    font_size=8.4,
)
add_para("主机合成测试中的最坏速度误差约 0.290 m/s、距离误差约 0.00261 m、理想阵列角度误差约 0.00552 度。这些数字只能描述理想合成输入，不能用于对外宣称硬件测距、测速或测角精度。")

add_heading("8 最新源码和固件产物")
add_para("当前主源码目录：D:\\downloads\\X2100_project-main\\migration\\adc_live_4tx4rx_bpm_ddma", mono=True)
add_table(
    ["文件或目录", "用途"],
    [
        ["README_BPM_DDMA.md", "最终软件方案、限制、构建与固件说明"],
        ["OPTIMIZATION_V3.md", "统一配置、诊断、连续多帧自测说明"],
        ["SUPPLIER_INTEGRATION.md", "厂家寄存器表接入边界"],
        ["CAN_PROTOCOL.md", "CAN 字段、歧义、API 与硬件待办"],
        ["radar_config.h", "唯一编译期参数入口"],
        ["vendor.c", "当前信号处理主链"],
        ["ddma_resolver.c h", "8 子带 DDMA 解析与拒绝策略"],
        ["radar_diagnostics.c h", "帧流和 RX DDMA 诊断"],
        ["radar_rf_profile.c h", "厂家表校验与安全下发接口"],
        ["supplier_registers.inc", "从厂家源文件生成的寄存器操作表"],
        ["radar_can_protocol.c h", "CAN 协议编解码和批发送状态机"],
        ["tests", "主机单元与完整流水线测试"],
    ],
    widths=[6.1, 10.0],
)

add_para("最新打包目录：D:\\downloads\\X2100_project-main\\artifacts\\bpm_ddma_4tx4rx_can_20260910", mono=True)
add_table(
    ["变体", "用途", "rtos with spl SHA256", "关键状态"],
    [
        ["selftest", "无前端板合成 ADC 自测", "3159B06F5BA8F491FF954E6DA543CCD33812E25BBD36FD964A341DFCA2032A83", "可烧录自测；清单标记未板测"],
        ["live_guarded", "真实 ADC 接入预留", "3189862B2369BAB4277BCABFB5098C0816DACA35EF35E7977ABE6C73F67E18B", "保护关闭 RF 写入；不能作为正式实采固件"],
    ],
    widths=[2.8, 4.3, 6.1, 3.0],
    font_size=7.8,
)
add_para("烧录时使用对应目录内 rtos-with-spl.bin 和匹配的 Cloner cfg。当前硬件为 SFC NAND 与 LPDDR2 配置，不要混用 Linux、NOR 或其他板型 cfg。烧录的是二进制镜像，不是 defconfig。")

add_heading("9 真实 4TX 联调前必须得到的信息")
add_number("请厂家确认当前 cheetah_128_512_config 是否确实启用 4 个 TX，以及每个 TX 对应的物理天线和 DDMA 子带。", 1)
add_number("请厂家确认 TX0到TX3 的逐 Chirp 相位增量，并确认四路是否叠加同一公共 BPM 序列、BPM 每帧起始码序号以及是否每帧复位。", 2)
add_number("请厂家给出 ChirpProfile 字段说明或直接给出当前 start frequency、slope、ramp end time、idle time、ADC start delay、sample rate、sample count 和 chirp repetition interval。", 3)
add_number("请厂家说明 32 字节 Chirp 头部字段、ADC 排列顺序、码制、通道顺序和帧号/码相位字段。", 4)
add_number("请提供 4TX4RX 天线坐标、16 路虚拟阵列顺序、通道极性以及幅相标定方法或标定数据。", 5)
add_number("如需 CAN，请提供板级原理图或接口定义，明确控制器或 MCP2515、收发器、SPI/GPIO、CANH/CANL、终端电阻、总线编号和波特率。", 6)
add_number("请协议对端书面确认运动方向、速度正负、角度方向、RCS 定义和心跳量化等歧义。", 7)

add_heading("10 推荐的后续执行顺序")
add_number("冻结当前 selftest 产物作为软件回归基线，保留 SHA256，不覆盖旧版。", 1)
add_number("拿到厂家确认后更新统一配置和 RF/array 保护条件；先做寄存器表结构、SPI 返回值和必要读回检查。", 2)
add_number("只开启前端与原始 ADC 采集，保存若干帧；先验证四路 RX 幅度、饱和、低位、帧长、帧号和 Chirp 头，不立即宣称点云正确。", 3)
add_number("用静止角反射器检查 Range 频谱、BPM 码相位和 8 个 DDMA 子带；确认 TX 与子带映射后再开放速度和 AoA 输出。", 4)
add_number("完成阵列通道排序与幅相标定，依次做静止单目标、运动单目标、角度扫描、强弱双目标和长时间连续运行。", 5)
add_number("真实点云稳定后再加入跟踪、航迹和预警。完成 RCS/速度/方向定义后，才把点云接入 CAN 通信任务并进行抓包验收。", 6)

add_heading("11 继续开发时必须保持的边界")
add_bullet("不要把 selftest 或嵌入式回放数据称为真实雷达测量。")
add_bullet("不要把 26 us 称为完整扫频时间；当前只确认其为候选 Chirp 起始间隔。")
add_bullet("不要把 370.62 MHz 称为完整 Chirp 带宽；它只是按有效采样窗口计算的覆盖带宽。")
add_bullet("不要在厂家和阵列信息缺失时把 RF ARRAY CONFIRMED 或 READY 宏直接设为 1。")
add_bullet("不要把算法功率直接填入 CAN 的标定 RCS，也不要用径向速度伪造合成速度和运动方向。")
add_bullet("不要把 CAN 打包自检 PASS 表述成已经通过 CAN 口发送。")
add_bullet("当前 4TX 相位和公共 BPM 关系是软件候选，只有实测子带和厂家确认后才可冻结。")

add_heading("12 新对话启动说明")
add_para("在新对话中上传本文件，并使用下面这段话开始：")
p = doc.add_paragraph()
p.paragraph_format.left_indent = Cm(0.8)
p.paragraph_format.right_indent = Cm(0.8)
p.paragraph_format.space_before = Pt(4)
p.paragraph_format.space_after = Pt(8)
r = p.add_run(
    "请先完整阅读这份交接文档，并以其中的最终状态、路径、验证边界和待办为准继续 X2100 Cheetah 雷达项目。"
    "不要重复中间方案讨论，也不要把合成测试当作真实 4TX 实测。开始工作前先核对 migration/adc_live_4tx4rx_bpm_ddma 下的 README_BPM_DDMA.md、"
    "SUPPLIER_INTEGRATION.md 和 CAN_PROTOCOL.md。我的下一目标是按交接文档第10节继续真实前端联调。"
)
set_run_font(r, size=10.2)

add_para("如后续源码或固件发生变更，应同步更新本文档的参数、验证状态、产物路径和 SHA256，避免新对话继续使用过期结论。")

doc.core_properties.title = "X2100 Cheetah 雷达移植当前状态交接"
doc.core_properties.subject = "用于新对话继续当前 X2100 Cheetah 雷达软件移植与联调"
doc.core_properties.author = "项目交接整理"
doc.core_properties.keywords = "X2100 Cheetah FreeRTOS 4TX4RX BPM DDMA USB CAN"

doc.save(OUT)
print(OUT)
