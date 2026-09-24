import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import queue


# 命令常量定义
CMD_MULTI_READ_DEV = "44"
CMD_MULTI_WRITE_DEV = "45"
CMD_MULTI_WRITE_REG = "47"

SUBCMD_SET_START_ANGLE = "02R00000"
SUBCMD_SET_END_ANGLE = "02R00002"
SUBCMD_SET_MOTOR_STEP = "02R00004"
SUBCMD_SET_MOTOR_START = "01M00011"
SUBCMD_READ_MOTOR_STATUS = "02M0010"
SUBCMD_START_MOTOR_STEP = "01M00021"
SUBCMD_MOTOR_RESET = "01M00001"


class UARTConfigApp:
    def __init__(self, root):
        self.root = root
        self.root.title("双UART配置工具")
        self.root.geometry("1400x700")
        
        self.serial1 = None
        self.serial2 = None
        self.running1 = False
        self.running2 = False
        
        self.turntable_uart = 1
        self.turntable_running = False
        self.turntable_timer = None
        self.roll_index = 0
        self.total_roll_num = 0
        self.read_adc_done = False
        self.turntable_com_buf = bytearray()
        
        self.setup_ui()
        self.refresh_ports()
    
    def create_context_menu(self, text_area, uart_num):
        menu = tk.Menu(self.root, tearoff=0)
        menu.add_command(label="全选", command=lambda: self.select_all(text_area))
        menu.add_command(label="复制", command=lambda: self.copy_text(text_area))
        menu.add_separator()
        menu.add_command(label="清空数据", command=lambda: self.clear_text(text_area, uart_num))
        
        def show_menu(event):
            menu.post(event.x_root, event.y_root)
        
        text_area.bind("<Button-3>", show_menu)
    
    def select_all(self, text_area):
        text_area.tag_add(tk.SEL, "1.0", tk.END)
        text_area.mark_set(tk.INSERT, "1.0")
        text_area.see(tk.INSERT)
    
    def copy_text(self, text_area):
        try:
            text = text_area.get(tk.SEL_FIRST, tk.SEL_LAST)
            self.root.clipboard_clear()
            self.root.clipboard_append(text)
        except tk.TclError:
            pass
    
    def clear_text(self, text_area, uart_num):
        text_area.config(state='normal')
        text_area.delete("1.0", tk.END)
        text_area.config(state='disabled')
        
        if uart_num == 1:
            if self.serial1 and self.serial1.is_open:
                self.serial1.reset_input_buffer()
        else:
            if self.serial2 and self.serial2.is_open:
                self.serial2.reset_input_buffer()
    
    def setup_ui(self):
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.columnconfigure(2, weight=1)
        main_frame.rowconfigure(0, weight=1)
        
        uart1_frame = ttk.LabelFrame(main_frame, text="UART 1", padding="10")
        uart1_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), padx=5)
        
        uart2_frame = ttk.LabelFrame(main_frame, text="UART 2", padding="10")
        uart2_frame.grid(row=0, column=1, sticky=(tk.W, tk.E, tk.N, tk.S), padx=5)
        
        turntable_frame = ttk.LabelFrame(main_frame, text="转台控制", padding="10")
        turntable_frame.grid(row=0, column=2, sticky=(tk.W, tk.E, tk.N, tk.S), padx=5)
        
        self.setup_uart_controls(uart1_frame, 1)
        self.setup_uart_controls(uart2_frame, 2)
        self.setup_turntable_controls(turntable_frame)
    
    def setup_uart_controls(self, parent, uart_num):
        ttk.Label(parent, text="端口:").grid(row=0, column=0, sticky=tk.W, pady=5)
        
        port_var = tk.StringVar()
        port_combo = ttk.Combobox(parent, textvariable=port_var, state="readonly")
        port_combo.grid(row=0, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(parent, text="波特率:").grid(row=1, column=0, sticky=tk.W, pady=5)
        
        baud_var = tk.StringVar(value="9600")
        baud_combo = ttk.Combobox(parent, textvariable=baud_var, values=["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"])
        baud_combo.grid(row=1, column=1, sticky=(tk.W, tk.E), pady=5)
        
        connect_btn = ttk.Button(parent, text="连接", command=lambda: self.toggle_connection(uart_num))
        connect_btn.grid(row=2, column=0, columnspan=2, pady=10)
        
        ttk.Label(parent, text="发送数据:").grid(row=3, column=0, sticky=tk.W, pady=5)
        
        send_text = scrolledtext.ScrolledText(parent, height=3, width=30)
        send_text.grid(row=3, column=1, sticky=(tk.W, tk.E), pady=5)
        
        send_btn = ttk.Button(parent, text="发送", command=lambda: self.send_uart(uart_num))
        send_btn.grid(row=4, column=0, columnspan=2, pady=5)
        
        ttk.Label(parent, text="接收数据:").grid(row=5, column=0, sticky=tk.W, pady=5)
        
        text_area = scrolledtext.ScrolledText(parent, height=15, width=40, state='disabled')
        text_area.grid(row=6, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)

        self.create_context_menu(text_area, uart_num)
        
        if uart_num == 1:
            self.port1_var = port_var
            self.port1_combo = port_combo
            self.baud1_var = baud_var
            self.baud1_combo = baud_combo
            self.send1_text = send_text
            self.text1 = text_area
            self.connect1_btn = connect_btn
        else:
            self.port2_var = port_var
            self.port2_combo = port_combo
            self.baud2_var = baud_var
            self.baud2_combo = baud_combo
            self.send2_text = send_text
            self.text2 = text_area
            self.connect2_btn = connect_btn
        
        parent.columnconfigure(1, weight=1)
        parent.rowconfigure(6, weight=1)
    
    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        
        self.port1_combo['values'] = port_list
        self.port2_combo['values'] = port_list
        
        if port_list:
            self.port1_var.set(port_list[0])
            if len(port_list) > 1:
                self.port2_var.set(port_list[1])
    
    def toggle_connection(self, uart_num):
        if uart_num == 1:
            if self.serial1 and self.serial1.is_open:
                self.disconnect_uart(uart_num)
            else:
                self.connect_uart(uart_num)
        else:
            if self.serial2 and self.serial2.is_open:
                self.disconnect_uart(uart_num)
            else:
                self.connect_uart(uart_num)
    
    def connect_uart(self, uart_num):
        try:
            if uart_num == 1:
                port = self.port1_var.get()
                baud = int(self.baud1_var.get())
                self.serial1 = serial.Serial(port, baud, timeout=1, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE)
                self.running1 = True
                threading.Thread(target=self.read_uart, args=(1,), daemon=True).start()
                self.connect1_btn.config(text="断开")
                self.update_text_area(self.text1, f"\n[系统] UART {uart_num} 连接成功 ({port}, {baud})\n")
            else:
                port = self.port2_var.get()
                baud = int(self.baud2_var.get())
                self.serial2 = serial.Serial(port, baud, timeout=1, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE)
                self.running2 = True
                threading.Thread(target=self.read_uart, args=(2,), daemon=True).start()
                self.connect2_btn.config(text="断开")
                self.update_text_area(self.text2, f"\n[系统] UART {uart_num} 连接成功 ({port}, {baud})\n")
            
            print(f"UART {uart_num} 连接成功")
        except Exception as e:
            text_area = self.text1 if uart_num == 1 else self.text2
            self.update_text_area(text_area, f"\n[系统] UART {uart_num} 连接失败: {e}\n")
            print(f"UART {uart_num} 连接失败: {e}")
    
    def disconnect_uart(self, uart_num):
        try:
            if uart_num == 1:
                self.running1 = False
                if self.serial1:
                    self.serial1.close()
                    self.serial1 = None
                self.connect1_btn.config(text="连接")
                self.update_text_area(self.text1, f"\n[系统] UART {uart_num} 断开成功\n")
            else:
                self.running2 = False
                if self.serial2:
                    self.serial2.close()
                    self.serial2 = None
                self.connect2_btn.config(text="连接")
                self.update_text_area(self.text2, f"\n[系统] UART {uart_num} 断开成功\n")
            
            print(f"UART {uart_num} 断开成功")
        except Exception as e:
            text_area = self.text1 if uart_num == 1 else self.text2
            self.update_text_area(text_area, f"\n[系统] UART {uart_num} 断开失败: {e}\n")
            print(f"UART {uart_num} 断开失败: {e}")
    
    def send_uart(self, uart_num):
        try:
            if uart_num == 1:
                serial_obj = self.serial1
                send_text = self.send1_text
            else:
                serial_obj = self.serial2
                send_text = self.send2_text
            
            if serial_obj and serial_obj.is_open:
                data = send_text.get("1.0", tk.END)
                if data:
                    serial_obj.write(data.encode('ascii'))
                    print(f"UART {uart_num} 发送成功: {repr(data)}")
                else:
                    print(f"UART {uart_num} 发送失败: 请输入要发送的数据")
            else:
                print(f"UART {uart_num} 发送失败: 串口未连接")
        except Exception as e:
            print(f"UART {uart_num} 发送失败: {e}")
    
    def read_uart(self, uart_num):
        serial_obj = self.serial1 if uart_num == 1 else self.serial2
        text_area = self.text1 if uart_num == 1 else self.text2
        running = self.running1 if uart_num == 1 else self.running2
        
        serial_obj.timeout = 0.01
        
        while running:
            try:
                if serial_obj.in_waiting > 0:
                    data = serial_obj.read(serial_obj.in_waiting)
                    decoded_data = data.decode('utf-8', errors='ignore')
                    
                    self.root.after(0, self.update_text_area, text_area, decoded_data)
                else:
                    time.sleep(0.001)
            except Exception as e:
                print(f"UART {uart_num} 读取错误: {e}")
                break
            
            running = self.running1 if uart_num == 1 else self.running2
    
    def update_text_area(self, text_area, data):
        MAX_LINES = 10000
        text_area.config(state='normal')
        text_area.insert(tk.END, data)
        
        line_count = int(text_area.index('end-1c').split('.')[0])
        if line_count > MAX_LINES:
            lines_to_delete = line_count - MAX_LINES
            text_area.delete('1.0', f'{lines_to_delete}.0')
        
        text_area.see(tk.END)
        text_area.config(state='disabled')
    
    def setup_turntable_controls(self, parent):
        ttk.Label(parent, text="选择UART端口:").grid(row=0, column=0, sticky=tk.W, pady=5)
        
        self.turntable_uart_var = tk.StringVar(value="UART 1")
        uart_combo = ttk.Combobox(parent, textvariable=self.turntable_uart_var, 
                                   values=["UART 1", "UART 2"], state="readonly")
        uart_combo.grid(row=0, column=1, sticky=(tk.W, tk.E), pady=5)
        
        # 雷达联动勾选框
        self.radar_link_var = tk.BooleanVar(value=False)
        radar_link_check = ttk.Checkbutton(parent, text="雷达联动", variable=self.radar_link_var)
        radar_link_check.grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=5)
        
        # 雷达运行帧数输入框
        ttk.Label(parent, text="雷达帧数:").grid(row=2, column=0, sticky=tk.W, pady=5)
        self.radar_frame_cnt_var = tk.IntVar(value=100)
        ttk.Entry(parent, textvariable=self.radar_frame_cnt_var).grid(row=2, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Separator(parent, orient='horizontal').grid(row=3, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(parent, text="起始角度 (度):").grid(row=4, column=0, sticky=tk.W, pady=5)
        self.start_angle_var = tk.DoubleVar(value=-45.0)
        ttk.Entry(parent, textvariable=self.start_angle_var).grid(row=4, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(parent, text="终止角度 (度):").grid(row=5, column=0, sticky=tk.W, pady=5)
        self.end_angle_var = tk.DoubleVar(value=45.0)
        ttk.Entry(parent, textvariable=self.end_angle_var).grid(row=5, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(parent, text="步进角度 (度):").grid(row=6, column=0, sticky=tk.W, pady=5)
        self.step_angle_var = tk.DoubleVar(value=5.0)
        ttk.Entry(parent, textvariable=self.step_angle_var).grid(row=6, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(parent, text="间隔时间 (秒):").grid(row=7, column=0, sticky=tk.W, pady=5)
        self.interval_time_var = tk.DoubleVar(value=1.0)
        ttk.Entry(parent, textvariable=self.interval_time_var).grid(row=7, column=1, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Separator(parent, orient='horizontal').grid(row=8, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Button(parent, text="启动电机", command=self.turntable_start_motor).grid(row=9, column=0, pady=5)
        ttk.Button(parent, text="复位电机", command=self.turntable_reset_motor).grid(row=9, column=1, pady=5)
        
        ttk.Button(parent, text="开始扫描", command=self.turntable_start_scan).grid(row=10, column=0, pady=5)
        ttk.Button(parent, text="停止扫描", command=self.turntable_stop_scan).grid(row=10, column=1, pady=5)
        
        ttk.Separator(parent, orient='horizontal').grid(row=11, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        
        # ttk.Label(parent, text="状态信息:").grid(row=10, column=0, sticky=tk.W, pady=5)
        
        self.turntable_status = scrolledtext.ScrolledText(parent, height=10, width=30, state='disabled')
        self.turntable_status.grid(row=11, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)
        
        # 模拟响应模式勾选框（用于测试）
        self.simulate_response_var = tk.BooleanVar(value=False)
        simulate_check = ttk.Checkbutton(parent, text="模拟响应（测试用）", variable=self.simulate_response_var)
        simulate_check.grid(row=12, column=0, columnspan=2, sticky=tk.W, pady=5)

        # 在"模拟响应（测试用）"旁边添加连续采集按钮的勾选框
        self.continuous_collection_var = tk.BooleanVar()
        continuous_collection_check = tk.Checkbutton(parent, text="连续采集按钮", variable=self.continuous_collection_var)
        continuous_collection_check.grid(row=12, column=1, sticky=tk.W, pady=5)

        parent.columnconfigure(1, weight=1)
        parent.rowconfigure(13, weight=1)
    
    def get_turntable_serial(self):
        uart_num = 1 if self.turntable_uart_var.get() == "UART 1" else 2
        return self.serial1 if uart_num == 1 else self.serial2, uart_num
    
    def turntable_log(self, message):
        self.root.after(0, self.update_text_area, self.turntable_status, f"{message}\n")
        print(f"[转台] {message}")
    
    def pack_config_data(self, para):
        """
        将参数打包为转台配置数据格式
        para: 已经乘以10的整数值（如-45° -> -450）
        返回: 8位十六进制字符串，高低字节交换
        """
        value = int(para)
        # 处理负数：转换为32位有符号整数补码
        if value < 0:
            value = (1 << 32) + value
        # 格式化为8位十六进制字符串（大写）
        hex_str = format(value, '08X')
        # 高低字节交换（如 0xFFFFFE35 -> "FE35FFFF"）
        return hex_str[4:8] + hex_str[0:4]
    
    def build_command_frame(self, cmd, cmd_data):
        data_out = bytearray()
        
        data_out.append(0x02)
        print(f"Step 1 - 添加帧头: {hex(data_out[0])}")
        
        data_out.extend([0x30, 0x31])
        print(f"Step 2 - 添加站号: {hex(data_out[1])} {hex(data_out[2])}")
        
        data_out.extend(cmd.encode('ascii'))
        print(f"Step 3 - 添加命令: {cmd} -> {[hex(b) for b in cmd.encode('ascii')]}")
        
        data_out.extend(cmd_data.encode('ascii'))
        print(f"Step 4 - 添加子命令: {cmd_data} -> {[hex(b) for b in cmd_data.encode('ascii')]}")
        
        # 校验和只取低字节（8位）
        sum_check = sum(data_out) & 0xFF
        print(f"Step 5 - 计算校验和: sum(data_out) = {hex(sum_check)}")
        
        checksum_hex = format(sum_check, '02X')
        data_out.extend(checksum_hex.encode('ascii'))
        print(f"Step 6 - 添加校验和: {checksum_hex} -> {[hex(b) for b in checksum_hex.encode('ascii')]}")
        
        data_out.append(0x03)
        print(f"Step 7 - 添加帧尾: {hex(data_out[-1])}")
        
        hex_str = ' '.join(f'0x{b:02X}' for b in data_out)
        print(f"最终帧: {hex_str}")
        print(f"帧长度: {len(data_out)}字节")
        
        return data_out
    
    def check_response_frame(self, data_in):
        if len(data_in) < 6:
            return -3
        
        if data_in[0] != 0x02:
            return -1
        
        # 计算校验和：从帧头到子命令结束（不包括校验和和帧尾）
        sum_check = 0
        for i in range(len(data_in) - 3):
            sum_check += data_in[i]
        
        # 只取低字节
        sum_check = sum_check & 0xFF
        
        try:
            received_checksum = int(data_in[-3:-1].decode('ascii'), 16)
        except:
            return -2
        
        # 调试输出
        print(f"\n[校验和检查]")
        print(f"接收帧长度: {len(data_in)} 字节")
        print(f"计算范围: 字节 0 到 {len(data_in) - 4}")
        print(f"计算的校验和: 0x{sum_check:02X} ({sum_check})")
        print(f"接收的校验和: 0x{received_checksum:02X} ({received_checksum})")
        
        if received_checksum != sum_check:
            return -2
        
        return 0
    
    def get_cmd_status_code(self, cmd_data):
        if len(cmd_data) > 5:
            return cmd_data[5]
        return -1
    
    def get_cmd_m10_status(self, cmd_data):
        if len(cmd_data) > 6:
            return cmd_data[6]
        return -1
    
    def get_cmd_m11_status(self, cmd_data):
        if len(cmd_data) > 7:
            return cmd_data[7]
        return -1
    
    def send_turntable_command(self, main_cmd, sub_cmd, pause_background=True):
        serial_obj, uart_num = self.get_turntable_serial()
        if not serial_obj or not serial_obj.is_open:
            self.turntable_log(f"错误: UART {uart_num} 未连接")
            return False, None
        
        background_paused = False
        
        try:
            # 暂停对应UART的后台读取，防止响应被抢占
            if pause_background:
                if uart_num == 1:
                    self.running1 = False
                else:
                    self.running2 = False
                background_paused = True
                # 等待后台线程停止
                time.sleep(0.05)
            
            self.turntable_com_buf.clear()
            
            # 清空串口缓冲区（添加延迟确保清空完成）
            serial_obj.reset_input_buffer()
            serial_obj.reset_output_buffer()
            time.sleep(0.05)  # 等待缓冲区清空完成
            
            # 再次检查并清空可能残留的数据
            while serial_obj.in_waiting > 0:
                serial_obj.read(serial_obj.in_waiting)
                time.sleep(0.01)
            
            frame = self.build_command_frame(main_cmd, sub_cmd)
            
            print(f"\n=== 发送命令 ===")
            print(f"主命令: {main_cmd}")
            print(f"子命令: {sub_cmd}")
            
            # 记录发送时间
            send_time = time.time()
            
            # 发送数据
            bytes_written = serial_obj.write(frame)
            serial_obj.flush()
            
            print(f"[发送数据] 写入字节数: {bytes_written}")
            print(f"[发送数据] 发送时间: {send_time:.6f}")
            
            # 短暂等待设备处理
            time.sleep(0.1)
            
            # 模拟响应模式（用于测试，强制返回正确响应）
            if self.simulate_response_var.get():
                # 模拟转台返回成功响应
                # 响应帧: 0x02 0x30 0x31 0x34 0x34 0x30 0x31 0x31 0x35 0x44 0x03
                # STX + "01" + "44" + "0" + "1" + "1" + "5D" + ETX
                simulated_response = bytes([0x02, 0x30, 0x31, 0x34, 0x34, 0x30, 0x31, 0x31, 0x35, 0x44, 0x03])
                print(f"[模拟响应] 已生成测试响应: {simulated_response.hex()}")
                
                # 恢复后台读取
                if uart_num == 1:
                    self.running1 = True
                    threading.Thread(target=self.read_uart, args=(1,), daemon=True).start()
                else:
                    self.running2 = True
                    threading.Thread(target=self.read_uart, args=(2,), daemon=True).start()
                
                return True, simulated_response
            
            # 检查是否有数据立即返回
            if serial_obj.in_waiting > 0:
                print(f"[发送后] 立即有 {serial_obj.in_waiting} 字节等待读取")
            else:
                print(f"[发送后] 暂无数据等待读取")
            
            max_wait = 5.0
            start_time = time.time()
            
            while time.time() - start_time < max_wait:
                if serial_obj.in_waiting > 0:
                    new_data = serial_obj.read(serial_obj.in_waiting)
                    
                    # 详细打印接收到的数据
                    hex_str = ' '.join(f'0x{b:02X}' for b in new_data)
                    ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in new_data)
                    timestamp = time.time() - start_time
                    
                    print(f"\n[UART接收] 耗时: {timestamp:.3f}秒")
                    print(f"[UART接收] 本次接收字节数: {len(new_data)}")
                    print(f"[UART接收] 十六进制: {hex_str}")
                    print(f"[UART接收] ASCII字符: '{ascii_str}'")
                    
                    self.turntable_com_buf.extend(new_data)
                    
                    # 打印缓存状态
                    buf_hex = ' '.join(f'0x{b:02X}' for b in self.turntable_com_buf)
                    print(f"[缓存状态] 当前缓存长度: {len(self.turntable_com_buf)} 字节")
                    print(f"[缓存状态] 缓存内容: {buf_hex}")
                    
                    if 0x03 in self.turntable_com_buf:
                        check_result = self.check_response_frame(self.turntable_com_buf)
                        if check_result == 0:
                            # 检查响应帧是否包含完整的状态码
                            if len(self.turntable_com_buf) >= 7:
                                status_code = self.turntable_com_buf[5]
                                print(f"[状态码] 0x{status_code:02X}")
                            return True, bytes(self.turntable_com_buf)
                        elif check_result == -1:
                            self.turntable_log("错误: 帧头不正确")
                            return False, None
                        elif check_result == -2:
                            self.turntable_log("错误: 校验和不正确")
                            return False, None
                        elif check_result == -3:
                            self.turntable_log("错误: 数据长度不足")
                            return False, None
                
                time.sleep(0.01)
            
            # 超时后检查是否接收到了不完整的数据
            if len(self.turntable_com_buf) > 0:
                hex_str = ' '.join(f'0x{b:02X}' for b in self.turntable_com_buf)
                self.turntable_log(f"警告: 接收超时，但收到部分数据: {hex_str}")
            
            self.turntable_log("错误: 接收超时")
            return False, None
            
        except Exception as e:
            self.turntable_log(f"发送命令失败: {e}")
            return False, None
        finally:
            # 恢复后台读取（只有在暂停了的情况下才恢复）
            if background_paused:
                if uart_num == 1:
                    self.running1 = True
                    threading.Thread(target=self.read_uart, args=(1,), daemon=True).start()
                else:
                    self.running2 = True
                    threading.Thread(target=self.read_uart, args=(2,), daemon=True).start()
    
    def turntable_start_motor(self):
        # 如果勾选了连续采集数据，检查是否勾选了雷达联动
        if self.continuous_collection_var.get() and not self.radar_link_var.get():
            self.turntable_log("错误: 连续采集数据需要勾选雷达联动")
            messagebox.showerror("错误", "连续采集数据需要勾选雷达联动")
            return
        
        # 如果勾选了雷达联动，检查两路UART是否都连接
        if self.radar_link_var.get():
            if not (self.serial1 and self.serial1.is_open and self.serial2 and self.serial2.is_open):
                self.turntable_log("错误: 雷达联动模式需要两路UART都连接")
                messagebox.showerror("错误", "雷达联动模式需要两路UART都连接")
                return
        
        # 记录启动时间（用于文件名）
        self.start_datetime = time.strftime("%Y%m%d_%H%M%S")
        
        # 如果勾选了连续采集数据，发送 SetDumpFileName 和 SetFrameCnt 指令
        if self.continuous_collection_var.get() and self.radar_link_var.get():
            # 获取另一路UART（非转台控制的UART）
            if self.turntable_uart_var.get() == "UART 1":
                radar_serial = self.serial2
                uart_num = 2
            else:
                radar_serial = self.serial1
                uart_num = 1
            
            if radar_serial and radar_serial.is_open:
                try:
                    # 构建第一条命令：SetDumpFileName <Year_Month_Day_Hour_Minute_Second>
                    datetime_str = time.strftime("%Y%m%d_%H%M%S")
                    cmd1 = f"SetDumpFileName {datetime_str}_scan\r\n"
                    radar_serial.write(cmd1.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd1.strip()}")
                    
                    # 两条指令之间间隔1秒
                    time.sleep(1)
                    
                    # 计算帧数：FrameNumber = |终止角度 - 起始角度| / 间隔角度 * 间隔时间 / 单帧时间
                    start_angle = self.start_angle_var.get()
                    end_angle = self.end_angle_var.get()
                    step_angle = self.step_angle_var.get()
                    interval_time = self.interval_time_var.get()
                    factor = 8  # 单帧时间是0.25s
                    
                    frame_number = int(abs(end_angle - start_angle) / step_angle * interval_time * factor)
                    
                    # 构建第二条命令：SetFrameCnt <FrameNumber>
                    cmd2 = f"SetFrameCnt {frame_number}\r\n"
                    radar_serial.write(cmd2.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd2.strip()}")
                    self.turntable_log(f"计算帧数: |{end_angle} - {start_angle}| / {step_angle} * {interval_time} * {factor} = {frame_number}")
                except Exception as e:
                    self.turntable_log(f"发送雷达命令失败: {e}")
            else:
                self.turntable_log(f"错误: UART {uart_num} 未连接")
        
        # 设置扫描参数
        if not self.turntable_set_parameters():
            self.turntable_log("设置扫描参数失败")
            return

        # 发送启动电机命令
        success, response = self.send_turntable_command(CMD_MULTI_WRITE_DEV, SUBCMD_SET_MOTOR_START)
        if success and response:
            status_code = self.get_cmd_status_code(response)
            if status_code == 0x30:
                self.turntable_log("电机启动命令已发送")
            else:
                self.turntable_log(f"电机启动失败，状态码: {hex(status_code)}")
                return
        else:
            self.turntable_log("电机启动失败")
            return
        
        # 等待电机启动完成（检查m10状态）
        self.turntable_log("等待电机启动完成...")
        
        max_wait = 60  # 最大等待60秒
        check_interval = 1  # 每隔1秒查询一次
        start_time = time.time()
        
        while time.time() - start_time < max_wait:
            try:
                success, response = self.send_turntable_command(CMD_MULTI_READ_DEV, SUBCMD_READ_MOTOR_STATUS)
                if success and response:
                    m10_status = self.get_cmd_m10_status(response)
                    print(f"[电机状态] m10_status: 0x{m10_status:02X}")
                    if m10_status == 0x31:
                        self.turntable_log("电机启动完成")
                        break
            except Exception as e:
                self.turntable_log(f"查询电机状态失败: {e}")
            
            time.sleep(check_interval)
        else:
            self.turntable_log("电机启动超时")
            return
        
        # 开始扫描
        self.turntable_running = True
        self.turntable_log("开始扫描...")
        
        self.turntable_timer = threading.Timer(0.1, self.turntable_timer_update)
        self.turntable_timer.start()
    
    def turntable_reset_motor(self):
        self.turntable_stop_scan()
        
        print("\n" + "="*50)
        print("[turntable_reset_motor] 开始执行电机复位")
        print("="*50)
        
        # 发送复位指令
        print(f"[复位指令] 主命令: {CMD_MULTI_WRITE_DEV}, 子命令: {SUBCMD_MOTOR_RESET}")
        
        success, response = self.send_turntable_command(CMD_MULTI_WRITE_DEV, SUBCMD_MOTOR_RESET)
        
        print(f"[复位结果] success={success}, response={response}")
        
        if response:
            # 打印接收到的响应帧详细信息
            hex_str = ' '.join(f'0x{b:02X}' for b in response)
            ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in response)
            print(f"[复位响应] 报文长度: {len(response)} 字节")
            print(f"[复位响应] 十六进制: {hex_str}")
            print(f"[复位响应] ASCII字符: '{ascii_str}'")
            
            # 逐字节解析
            print("[复位响应] 逐字节解析:")
            for i, byte in enumerate(response):
                print(f"  字节[{i}]: 0x{byte:02X} (ASCII: '{chr(byte)}' 或 {byte})")
            
            status_code = self.get_cmd_status_code(response)
            print(f"[复位响应] 状态码位置(索引5): 0x{response[5] if len(response) > 5 else 'N/A':02X}")
            print(f"[复位响应] get_cmd_status_code返回: 0x{status_code:02X}")
        
        if success and response:
            status_code = self.get_cmd_status_code(response)
            
            if status_code == 0x30:
                self.turntable_log("电机复位命令已发送，等待电机回到初始位置...")
                threading.Thread(target=self.wait_for_reset, daemon=True).start()
            else:
                self.turntable_log(f"电机复位失败，状态码: {hex(status_code)}")
        else:
            if response:
                hex_str = ' '.join(f'0x{b:02X}' for b in response)
                self.turntable_log(f"电机复位失败，响应帧不完整: {hex_str}")
            else:
                self.turntable_log("电机复位失败，未收到响应")
        
        print("="*50)
        print("[turntable_reset_motor] 执行结束")
        print("="*50 + "\n")
    
    def wait_for_reset(self):
        serial_obj, uart_num = self.get_turntable_serial()
        timeout_seconds = 60  # 超时时间为1分钟
        check_interval = 1    # 每隔1秒查询一次
        max_attempts = timeout_seconds // check_interval
        
        self.turntable_log(f"等待电机复位，超时时间: {timeout_seconds}秒")
        
        for i in range(max_attempts):
            try:
                success, response = self.send_turntable_command(CMD_MULTI_READ_DEV, SUBCMD_READ_MOTOR_STATUS)
                if success and response:
                    m10_status = self.get_cmd_m10_status(response)
                    if m10_status == 0x31:
                        self.turntable_log("电机已回到初始位置")
                        return
            except Exception as e:
                self.turntable_log(f"查询电机状态失败: {e}")
            
            # 每隔1秒查询一次
            time.sleep(check_interval)
        
        self.turntable_log("电机复位超时（超过1分钟）")
    
    def turntable_set_parameters(self):
        try:
            # 获取转台UART
            serial_obj, uart_num = self.get_turntable_serial()
            if not serial_obj or not serial_obj.is_open:
                self.turntable_log(f"错误: UART {uart_num} 未连接")
                return False
            
            # 暂停后台读取线程，防止数据被抢占
            if uart_num == 1:
                self.running1 = False
            else:
                self.running2 = False
            
            # 等待后台线程停止
            time.sleep(0.1)
            
            # 清空转台命令缓冲区，确保干净的状态
            self.turntable_com_buf.clear()
            
            # 清空串口缓冲区
            serial_obj.reset_input_buffer()
            serial_obj.reset_output_buffer()
            time.sleep(0.05)
            
            start_angle = self.start_angle_var.get()
            end_angle = self.end_angle_var.get()
            step_angle = self.step_angle_var.get()
            interval_time = self.interval_time_var.get()
            
            if not (-90 <= start_angle <= 90):
                messagebox.showerror("参数错误", "起始角度必须在-90到90度之间")
                return False
            
            if not (-90 <= end_angle <= 90):
                messagebox.showerror("参数错误", "终止角度必须在-90到90度之间")
                return False
            
            if step_angle <= 0:
                messagebox.showerror("参数错误", "步进角度必须大于0")
                return False
            
            if end_angle <= start_angle:
                messagebox.showerror("参数错误", "终止角度必须大于起始角度")
                return False
            
            self.inter_time = int(interval_time * 1000)
            self.start_pos = int(start_angle * 10)
            self.stop_pos = int(end_angle * 10)
            self.roll_step = int(step_angle * 10)
            
            start_data = self.pack_config_data(self.start_pos)
            end_data = self.pack_config_data(self.stop_pos)
            step_data = self.pack_config_data(self.roll_step)
            
            success, response = self.send_turntable_command(CMD_MULTI_WRITE_REG, SUBCMD_SET_START_ANGLE + start_data, pause_background=False)
            if not success:
                self.turntable_log("设置起始角度失败")
                return False
            if response and self.get_cmd_status_code(response) == 0x30:
                self.turntable_log(f"起始角度设置成功: {start_angle}度")
            else:
                self.turntable_log("设置起始角度失败")
                return False
            
            success, response = self.send_turntable_command(CMD_MULTI_WRITE_REG, SUBCMD_SET_END_ANGLE + end_data, pause_background=False)
            if not success:
                self.turntable_log("设置终止角度失败")
                return False
            if response and self.get_cmd_status_code(response) == 0x30:
                self.turntable_log(f"终止角度设置成功: {end_angle}度")
            else:
                self.turntable_log("设置终止角度失败")
                return False
            
            success, response = self.send_turntable_command(CMD_MULTI_WRITE_REG, SUBCMD_SET_MOTOR_STEP + step_data, pause_background=False)
            if not success:
                self.turntable_log("设置步进角度失败")
                return False
            if response and self.get_cmd_status_code(response) == 0x30:
                self.turntable_log(f"步进角度设置成功: {step_angle}度")
            else:
                self.turntable_log("设置步进角度失败")
                return False
            
            self.total_roll_num = (self.stop_pos - self.start_pos + self.roll_step) // self.roll_step
            self.roll_index = 0
            
            time.sleep(0.2)
            
            # 恢复后台读取线程
            if uart_num == 1:
                self.running1 = True
                threading.Thread(target=self.read_uart, args=(1,), daemon=True).start()
            else:
                self.running2 = True
                threading.Thread(target=self.read_uart, args=(2,), daemon=True).start()
            
            return True
            
        except Exception as e:
            self.turntable_log(f"参数设置错误: {e}")
            # 发生异常时也要恢复后台读取线程
            try:
                serial_obj, uart_num = self.get_turntable_serial()
                if uart_num == 1:
                    self.running1 = True
                    threading.Thread(target=self.read_uart, args=(1,), daemon=True).start()
                elif uart_num == 2:
                    self.running2 = True
                    threading.Thread(target=self.read_uart, args=(2,), daemon=True).start()
            except:
                pass
            return False
    
    def turntable_start_scan(self):
        if self.turntable_running:
            self.turntable_log("扫描已在运行中")
            return
        
        if not self.turntable_set_parameters():
            return
        
        self.turntable_running = True
        self.turntable_log("开始扫描...")
        
        self.turntable_timer = threading.Timer(0.1, self.turntable_timer_update)
        self.turntable_timer.start()
    
    def turntable_stop_scan(self):
        self.turntable_running = False
        if self.turntable_timer:
            self.turntable_timer.cancel()
            self.turntable_timer = None
        
        # 如果启用了连续采集模式和雷达联动，发送 setAngle 666 指令
        if self.continuous_collection_var.get() and self.radar_link_var.get():
            if self.turntable_uart_var.get() == "UART 1":
                radar_serial = self.serial2
                uart_num = 2
            else:
                radar_serial = self.serial1
                uart_num = 1
            
            if radar_serial and radar_serial.is_open:
                try:
                    cmd = "setAngle 666\r\n"
                    radar_serial.write(cmd.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd.strip()}")
                except Exception as e:
                    self.turntable_log(f"发送雷达命令失败: {e}")
        
        # 重置扫描状态，确保下次扫描能正确配置参数
        self.roll_index = 0
        self.total_roll_num = 0
        self.read_adc_done = False
        # 清空转台命令缓冲区
        self.turntable_com_buf.clear()
        # 清空串口输入缓冲区
        serial_obj, _ = self.get_turntable_serial()
        if serial_obj and serial_obj.is_open:
            try:
                serial_obj.reset_input_buffer()
            except Exception as e:
                print(f"清空串口缓冲区失败: {e}")
        self.turntable_log("扫描已停止")
    
    def turntable_timer_update(self):
        if not self.turntable_running:
            return
        
        serial_obj, uart_num = self.get_turntable_serial()
        if not serial_obj or not serial_obj.is_open:
            self.turntable_log("错误: UART未连接")
            self.turntable_stop_scan()
            return
        
        self.read_adc_done = False
        
        try:
            max_attempts = 50
            position_reached = False
            
            for i in range(max_attempts):
                success, response = self.send_turntable_command(CMD_MULTI_READ_DEV, SUBCMD_READ_MOTOR_STATUS)
                if success and response:
                    m11_status = self.get_cmd_m11_status(response)
                    if m11_status == 0x31:
                        if self.roll_index == 0:
                            self.turntable_log("伺服电机已到达起始位置")
                            # 转台抵达位置后1s发送报文
                            self.turntable_log("转台抵达位置，等待1秒后发送雷达命令...")
                            for _ in range(100):  # 1秒延迟
                                if not self.turntable_running:
                                    return
                                time.sleep(0.01)
                            
                            # 如果启用雷达联动，发送雷达控制命令
                            if self.radar_link_var.get():
                                self.send_radar_commands()
                            self.turntable_log("准备数据采集...")
                            # 在起始位置等待间隔时间
                            self.turntable_log(f"等待 {self.inter_time/1000.0} 秒...")
                            for _ in range(int(self.inter_time / 10)):
                                if not self.turntable_running:
                                    return
                                time.sleep(0.01)

                        else:
                            self.turntable_log(f"伺服电机已移动到下一位置: {self.roll_index}")
                        position_reached = True
                        break
                
                time.sleep(0.02)
            
            if not position_reached:
                self.turntable_log("等待电机到位超时")
                self.turntable_stop_scan()
                return
            
            # 模拟数据采集延迟（替代原来的 read_adc_done 等待）
            # 原来的代码会等待 read_adc_done 信号，但在当前实现中这个信号从未被设置
            # 添加对 turntable_running 的检查，确保可以停止
            for _ in range(10):  # 约0.1秒延迟
                if not self.turntable_running:
                    self.turntable_log("扫描已停止")
                    return
                time.sleep(0.01)
            
            # 标记ADC读取完成（用于后续扩展）
            self.read_adc_done = True
            
            self.roll_index += 1
            
            # 检查是否到达最后一个位置
            if self.roll_index >= self.total_roll_num:
                # 在终止位置等待间隔时间
                # self.turntable_log(f"到达终止位置，等待 {self.inter_time/1000.0} 秒...")
                # for _ in range(int(self.inter_time / 10)):
                #     if not self.turntable_running:
                #         return
                #     time.sleep(0.01)
                
                self.turntable_log(f"数据采集完成: {self.roll_index}个位置")
                self.turntable_stop_scan()
                messagebox.showinfo("完成", f"扫描完成！共采集{self.roll_index}个位置的数据")
                return
            
            # 如果不是最后一个位置，先发送 setAngle 666 指令使角度无效（连续采集模式）
            if self.continuous_collection_var.get() and self.radar_link_var.get():
                if self.turntable_uart_var.get() == "UART 1":
                    radar_serial = self.serial2
                    uart_num = 2
                else:
                    radar_serial = self.serial1
                    uart_num = 1
                
                if radar_serial and radar_serial.is_open:
                    try:
                        cmd = "setAngle 666\r\n"
                        radar_serial.write(cmd.encode('ascii'))
                        radar_serial.flush()
                        self.turntable_log(f"UART{uart_num} 发送: {cmd.strip()}")
                    except Exception as e:
                        self.turntable_log(f"发送雷达命令失败: {e}")
            
            # 发送电机移动命令
            success, response = self.send_turntable_command(CMD_MULTI_WRITE_DEV, SUBCMD_START_MOTOR_STEP)
            if success and response:
                status_code = self.get_cmd_status_code(response)
                if status_code == 0x30:
                    self.turntable_log("电机移动到下一位置")
                else:
                    self.turntable_log(f"移动电机失败，状态码: {hex(status_code)}")
                    self.turntable_stop_scan()
                    return
            else:
                self.turntable_log("移动电机失败")
                self.turntable_stop_scan()
                return
            
            # 转台抵达位置后1s发送报文
            self.turntable_log("转台抵达位置，等待1秒后发送雷达命令...")
            for _ in range(100):  # 1秒延迟
                if not self.turntable_running:
                    return
                time.sleep(0.01)
            
            # 如果启用雷达联动，发送雷达控制命令
            if self.radar_link_var.get():
                self.send_radar_commands()
            
            self.turntable_log("准备数据采集...")

            if self.turntable_running:
                self.turntable_timer = threading.Timer(self.inter_time / 1000.0, self.turntable_timer_update)
                self.turntable_log(f"等待 {self.inter_time/1000.0} 秒后移动位置")
                self.turntable_timer.start()
                
        except Exception as e:
            self.turntable_log(f"扫描过程错误: {e}")
            self.turntable_stop_scan()
    
    def send_radar_commands(self):
        """
        发送雷达联动命令到另一路UART
        """
        # 获取转台当前角度
        current_angle = self.start_pos / 10.0 + self.roll_index * (self.roll_step / 10.0)
        
        # 获取另一路UART（非转台控制的UART）
        if self.turntable_uart_var.get() == "UART 1":
            radar_serial = self.serial2
            uart_num = 2
        else:
            radar_serial = self.serial1
            uart_num = 1
        
        if radar_serial and radar_serial.is_open:
            try:
                if self.continuous_collection_var.get():
                    # 连续采集模式：发送 setAngle [当前角度] 指令
                    cmd = f"setAngle {current_angle:.1f}\r\n"
                    radar_serial.write(cmd.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd.strip()}")
                else:
                    # 普通模式：发送 SetDumpFileName 和 SetFrameCnt 指令
                    # 获取雷达帧数
                    frame_cnt = self.radar_frame_cnt_var.get()
                    
                    # 构建第一条命令：SetDumpFileName
                    cmd1 = f"SetDumpFileName {self.start_datetime}_{current_angle:.1f}\r\n"
                    
                    # 构建第二条命令：SetFrameCnt
                    cmd2 = f"SetFrameCnt {frame_cnt}\r\n"
                    
                    # 发送第一条命令
                    radar_serial.write(cmd1.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd1.strip()}")
                    
                    # 第一条报文发送完成后5再发送第二条报文
                    time.sleep(5)
                    
                    # 发送第二条命令
                    radar_serial.write(cmd2.encode('ascii'))
                    radar_serial.flush()
                    self.turntable_log(f"UART{uart_num} 发送: {cmd2.strip()}")
            except Exception as e:
                self.turntable_log(f"发送雷达命令失败: {e}")
        else:
            self.turntable_log(f"错误: UART {uart_num} 未连接")
    
    def on_closing(self):
        self.turntable_stop_scan()
        self.disconnect_uart(1)
        self.disconnect_uart(2)
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = UARTConfigApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()