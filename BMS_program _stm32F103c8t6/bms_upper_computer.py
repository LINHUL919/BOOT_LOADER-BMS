#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BMS 上位机 - BQ76940 电池管理系统
基于 Python tkinter, 通过串口 ASCII 协议与 STM32 通信
协议: 115200-8N1, 发 "status" 获取一帧数据
"""

# ============================================================
# ★ 有效电芯通道映射 (BQ76940 共15通道, 实际只接了9节)
# ★ ok=1 的通道: 1,2,5,6,7,10,11,12,15
# ★ 修改这个列表即可适配不同接法
# ============================================================
VALID_CHANNELS = [1, 2, 5, 6, 7, 10, 11, 12, 15]
NUM_CELLS = len(VALID_CHANNELS)   # 9

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import serial
import serial.tools.list_ports
import threading
import time
import re
import csv
import os
from datetime import datetime
from collections import deque

try:
    import winsound
except ImportError:
    winsound = None


class BMSData:
    """BMS 数据结构"""
    def __init__(self):
        self.cell_all = {}        # {通道号: mv}  固件上报的全部15通道
        self.cell_ok  = {}        # {通道号: bool} ok标志
        self.pack_mv = 0
        self.soc = 0
        self.balance_mask = 0
        self.temp = 0.0
        self.current_mA = 0
        self.chg_on = False
        self.dsg_on = False
        self.prot_ov = 0
        self.prot_uv = 0
        self.prot_oc = 0
        self.prot_ot = 0
        self.hw_ov = 0
        self.hw_uv = 0
        self.hw_scd = 0
        self.hw_ocd = 0
        self.gain = 0
        self.offset = 0
        self.timestamp = ""

    def valid_voltages(self):
        """返回有效通道的电压列表 (按 VALID_CHANNELS 顺序)"""
        return [self.cell_all.get(ch, 0) for ch in VALID_CHANNELS]

    def cell_max(self):
        vals = [v for v in self.valid_voltages() if v > 0]
        return max(vals) if vals else 0

    def cell_min(self):
        vals = [v for v in self.valid_voltages() if v > 0]
        return min(vals) if vals else 0

    def cell_diff(self):
        return self.cell_max() - self.cell_min()

    def real_pack(self):
        """有效节求和的真实总压"""
        return sum(v for v in self.valid_voltages() if v > 0)


class BMSUpperComputer:
    """BMS 上位机主界面"""

    def __init__(self, root):
        self.root = root
        self.root.title(f"BMS 上位机 - BQ76940 {NUM_CELLS}串")
        self.root.geometry("960x760")
        self.root.resizable(True, True)

        self.ser = None
        self.running = False
        self.auto_refresh = False
        self.bms = BMSData()
        self.log_file = None
        self.log_writer = None
        self.var_alarm_enable = tk.BooleanVar(value=True)
        self.alarm_active_prev = False
        self.history_len = 120
        self.hist_pack_v = deque(maxlen=self.history_len)
        self.hist_current_a = deque(maxlen=self.history_len)
        self.hist_temp = deque(maxlen=self.history_len)
        self.auto_bal_id = None          # 自动均衡定时器ID
        self.auto_bal_phase = 0          # A/B交替相位: 0=A组, 1=B组

        self._build_ui()

    # ==================== 界面搭建 ====================

    def _build_ui(self):
        # ---------- 顶部: 串口连接区 ----------
        frm_conn = ttk.LabelFrame(self.root, text="串口连接")
        frm_conn.pack(fill="x", padx=5, pady=3)

        row_conn = ttk.Frame(frm_conn)
        row_conn.pack(fill="x", padx=4, pady=4)

        ttk.Label(row_conn, text="端口:").pack(side="left", padx=2)
        self.cb_port = ttk.Combobox(row_conn, width=10, state="readonly")
        self.cb_port.pack(side="left", padx=2)

        ttk.Button(row_conn, text="刷新", command=self._scan_ports, width=5).pack(side="left", padx=2)

        ttk.Label(row_conn, text="波特率:").pack(side="left", padx=(8, 2))
        self.cb_baud = ttk.Combobox(row_conn, width=8, state="readonly",
                                     values=["9600", "19200", "38400", "57600", "115200"])
        self.cb_baud.set("115200")
        self.cb_baud.pack(side="left", padx=2)

        self.btn_connect = ttk.Button(row_conn, text="连接", command=self._toggle_connect, width=6)
        self.btn_connect.pack(side="left", padx=6)

        self.lbl_status = ttk.Label(row_conn, text="● 未连接", foreground="gray")
        self.lbl_status.pack(side="left", padx=6)

        ttk.Separator(row_conn, orient="vertical").pack(side="left", fill="y", padx=6)

        self.var_auto = tk.BooleanVar(value=False)
        ttk.Checkbutton(row_conn, text="自动刷新", variable=self.var_auto,
                        command=self._toggle_auto).pack(side="left", padx=4)

        ttk.Label(row_conn, text="间隔(秒):").pack(side="left")
        self.spn_interval = ttk.Spinbox(row_conn, from_=1, to=60, width=3)
        self.spn_interval.set(2)
        self.spn_interval.pack(side="left", padx=2)

        ttk.Checkbutton(row_conn, text="报警", variable=self.var_alarm_enable).pack(side="left", padx=8)

        # ---------- 中部: 左右分栏 ----------
        frm_mid = ttk.Frame(self.root)
        frm_mid.pack(fill="both", expand=True, padx=5, pady=3)

        # ----- 左列: 电芯 + 原始输出 -----
        frm_left = ttk.Frame(frm_mid)
        frm_left.pack(side="left", fill="both", expand=True, padx=(0, 3))

        frm_cells = ttk.LabelFrame(frm_left, text="电芯电压 (mV)")
        frm_cells.pack(fill="x", padx=0, pady=(0, 3))

        self.cell_labels = []
        self.cell_bars = []
        for i in range(NUM_CELLS):
            row = ttk.Frame(frm_cells)
            row.pack(fill="x", padx=4, pady=1)

            lbl_name = ttk.Label(row, text=f"Cell {i+1}:", width=7, anchor="e")
            lbl_name.pack(side="left")

            bar = ttk.Progressbar(row, length=200, maximum=4500, mode="determinate")
            bar.pack(side="left", padx=4, fill="x", expand=True)

            lbl_val = ttk.Label(row, text="---- mV", width=10, anchor="e")
            lbl_val.pack(side="right")

            self.cell_labels.append(lbl_val)
            self.cell_bars.append(bar)

        # 统计行
        frm_stat = ttk.Frame(frm_cells)
        frm_stat.pack(fill="x", padx=6, pady=(4, 4))
        self.lbl_max = ttk.Label(frm_stat, text="Max: ----")
        self.lbl_max.pack(side="left", padx=8)
        self.lbl_min = ttk.Label(frm_stat, text="Min: ----")
        self.lbl_min.pack(side="left", padx=8)
        self.lbl_diff = ttk.Label(frm_stat, text="Diff: ----")
        self.lbl_diff.pack(side="left", padx=8)

        # 串口原始输出 (左下)
        frm_raw = ttk.LabelFrame(frm_left, text="串口原始输出")
        frm_raw.pack(fill="both", expand=True, padx=0, pady=0)

        self.txt_raw = scrolledtext.ScrolledText(frm_raw, height=8, font=("Consolas", 9),
                                                  state="disabled", wrap="word")
        self.txt_raw.pack(fill="both", expand=True, padx=4, pady=(4, 0))

        btn_clear_row = ttk.Frame(frm_raw)
        btn_clear_row.pack(fill="x", padx=4, pady=2)
        ttk.Button(btn_clear_row, text="清空", command=self._clear_raw, width=6).pack(side="right")

        # ----- 右列: 系统信息 + 保护 + 控制 + 曲线 -----
        frm_right = ttk.Frame(frm_mid)
        frm_right.pack(side="left", fill="both", expand=True)

        # 系统信息
        frm_info = ttk.LabelFrame(frm_right, text="系统信息")
        frm_info.pack(fill="x", padx=0, pady=(0, 3))

        info_items = [
            ("总电压:", "pack_mv"),
            ("电流:", "current"),
            ("温度:", "temp"),
            ("SOC:", "soc"),
            ("CHG MOS:", "chg"),
            ("DSG MOS:", "dsg"),
            ("均衡:", "balance"),
        ]
        self.info_labels = {}
        for text, key in info_items:
            row = ttk.Frame(frm_info)
            row.pack(fill="x", padx=5, pady=1)
            ttk.Label(row, text=text, width=10, anchor="e").pack(side="left")
            lbl = ttk.Label(row, text="-----", anchor="w", font=("Consolas", 10))
            lbl.pack(side="left", padx=5, fill="x", expand=True)
            self.info_labels[key] = lbl

        # 保护状态
        frm_prot = ttk.LabelFrame(frm_right, text="保护状态")
        frm_prot.pack(fill="x", padx=0, pady=3)

        self.prot_labels = {}
        prot_row1 = ttk.Frame(frm_prot)
        prot_row1.pack(fill="x", padx=5, pady=3)
        for name in ["OV", "UV", "OC", "OT"]:
            lbl = tk.Label(prot_row1, text=f"  {name}  ", bg="#a6e3a1", fg="black",
                           font=("Consolas", 9, "bold"), relief="raised", bd=1)
            lbl.pack(side="left", padx=4)
            self.prot_labels[name] = lbl

        prot_row2 = ttk.Frame(frm_prot)
        prot_row2.pack(fill="x", padx=5, pady=(0, 3))
        for name in ["HW_OV", "HW_UV", "HW_SCD", "HW_OCD"]:
            lbl = tk.Label(prot_row2, text=f" {name} ", bg="#a6e3a1", fg="black",
                           font=("Consolas", 9, "bold"), relief="raised", bd=1)
            lbl.pack(side="left", padx=4)
            self.prot_labels[name] = lbl

        # 控制命令
        frm_ctrl = ttk.LabelFrame(frm_right, text="控制命令")
        frm_ctrl.pack(fill="x", padx=0, pady=3)

        # 按钮行1:  读取状态 | CHG ON | CHG OFF
        btn_r1 = ttk.Frame(frm_ctrl)
        btn_r1.pack(fill="x", padx=4, pady=2)
        for txt, cmd in [("读取状态", "status"), ("CHG ON", "chg on"), ("CHG OFF", "chg off")]:
            ttk.Button(btn_r1, text=txt, width=10,
                       command=lambda c=cmd: self._send(c)).pack(side="left", padx=2)

        # 按钮行2:  DSG ON | DSG OFF | MOS全开 | MOS全关
        btn_r2 = ttk.Frame(frm_ctrl)
        btn_r2.pack(fill="x", padx=4, pady=2)
        for txt, cmd in [("DSG ON", "dsg on"), ("DSG OFF", "dsg off"),
                         ("MOS全开", "mos on"), ("MOS全关", "mos off")]:
            ttk.Button(btn_r2, text=txt, width=9,
                       command=lambda c=cmd: self._send(c)).pack(side="left", padx=2)

        # 均衡行
        bal_row = ttk.Frame(frm_ctrl)
        bal_row.pack(fill="x", padx=4, pady=2)
        ttk.Label(bal_row, text="均衡:").pack(side="left")
        self.ent_bal = ttk.Entry(bal_row, width=8)
        self.ent_bal.pack(side="left", padx=3)
        ttk.Button(bal_row, text="执行", width=5, command=self._send_bal).pack(side="left", padx=2)
        ttk.Button(bal_row, text="智能均衡", width=8, command=self._auto_balance).pack(side="left", padx=2)
        ttk.Label(bal_row, text=f"(1~{NUM_CELLS}/off)", font=("", 8),
                  foreground="gray").pack(side="left", padx=4)

        # 自动循环均衡行
        autobal_row = ttk.Frame(frm_ctrl)
        autobal_row.pack(fill="x", padx=4, pady=2)
        self.var_auto_bal = tk.BooleanVar(value=False)
        self.chk_auto_bal = ttk.Checkbutton(
            autobal_row, text="自动循环均衡 (15s交替)",
            variable=self.var_auto_bal, command=self._toggle_auto_bal)
        self.chk_auto_bal.pack(side="left")
        self.lbl_auto_bal = ttk.Label(autobal_row, text="", foreground="gray", font=("", 8))
        self.lbl_auto_bal.pack(side="left", padx=6)

        # 自定义命令行
        cust_row = ttk.Frame(frm_ctrl)
        cust_row.pack(fill="x", padx=4, pady=2)
        ttk.Label(cust_row, text="自定义:").pack(side="left")
        self.ent_cmd = ttk.Entry(cust_row, width=18)
        self.ent_cmd.pack(side="left", padx=3, fill="x", expand=True)
        self.ent_cmd.bind("<Return>", lambda e: self._send_custom())
        ttk.Button(cust_row, text="发送", width=5, command=self._send_custom).pack(side="left", padx=2)

        # CSV + 报警状态
        log_row = ttk.Frame(frm_ctrl)
        log_row.pack(fill="x", padx=4, pady=2)
        self.btn_log = ttk.Button(log_row, text="开始记录CSV", width=12, command=self._toggle_log)
        self.btn_log.pack(side="left", padx=2)
        self.lbl_log = ttk.Label(log_row, text="", foreground="gray")
        self.lbl_log.pack(side="left", padx=4)

        alarm_row = ttk.Frame(frm_ctrl)
        alarm_row.pack(fill="x", padx=4, pady=(0, 4))
        ttk.Label(alarm_row, text="报警状态:").pack(side="left")
        self.lbl_alarm = tk.Label(alarm_row, text="正常", fg="green", font=("", 9, "bold"))
        self.lbl_alarm.pack(side="left", padx=6)

        # 实时曲线
        frm_trend = ttk.LabelFrame(frm_right, text="实时曲线 (最近120点)")
        frm_trend.pack(fill="both", expand=True, padx=0, pady=3)
        self.cvs_trend = tk.Canvas(frm_trend, bg="#101418", highlightthickness=0, height=200)
        self.cvs_trend.pack(fill="both", expand=True, padx=4, pady=4)

        frm_legend = ttk.Frame(frm_trend)
        frm_legend.pack(fill="x", padx=6, pady=(0, 4))
        ttk.Label(frm_legend, text="黄:总压(V)", foreground="#c0a030").pack(side="left", padx=6)
        ttk.Label(frm_legend, text="青:电流(A)", foreground="#30a0a8").pack(side="left", padx=6)
        ttk.Label(frm_legend, text="红:温度(℃)", foreground="#c04040").pack(side="left", padx=6)

        # 初始扫描端口
        self._scan_ports()

    # ==================== 串口操作 ====================

    def _scan_ports(self):
        ports = serial.tools.list_ports.comports()
        names = [p.device for p in ports]
        self.cb_port["values"] = names
        if names:
            self.cb_port.set(names[0])

    def _toggle_connect(self):
        if self.ser and self.ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.cb_port.get()
        baud = int(self.cb_baud.get())
        if not port:
            messagebox.showwarning("提示", "请选择串口")
            return
        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.running = True
            self.btn_connect.config(text="断开")
            self.lbl_status.config(text=f"● 已连接 {port}", foreground="green")
            self._append_raw(f"[INFO] 已连接 {port} @ {baud}\n")

            # 启动接收线程
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
        except Exception as e:
            messagebox.showerror("连接失败", str(e))

    def _disconnect(self):
        self.running = False
        self.auto_refresh = False
        self.var_auto.set(False)
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.btn_connect.config(text="连接")
        self.lbl_status.config(text="● 未连接", foreground="gray")

    def _send(self, cmd):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("提示", "请先连接串口")
            return
        try:
            self.ser.write((cmd + "\r\n").encode("ascii"))
            self._append_raw(f">>> {cmd}\n")
        except Exception as e:
            self._append_raw(f"[发送失败] {e}\n")

    def _send_bal(self):
        arg = self.ent_bal.get().strip()
        if not arg:
            return
        # odd/even/off/status 直接透传
        if arg in ("odd", "even", "off", "status"):
            self._send(f"bal {arg}")
            return
        # 数字: 显示编号 1~9 → 真实通道号
        try:
            idx = int(arg)
            if 1 <= idx <= NUM_CELLS:
                ch = VALID_CHANNELS[idx - 1]
                self._send(f"bal {ch}")
                self._append_raw(f"[映射] Cell {idx} → 通道 {ch}\n")
            else:
                self._append_raw(f"[ERR] 请输入 1~{NUM_CELLS} 或 odd/even/off/status\n")
        except ValueError:
            self._append_raw(f"[ERR] 无效输入: {arg}\n")

    @staticmethod
    def _split_nonadj(mask):
        """将16bit mask分成两组, 保证同一CELLBAL寄存器内无相邻bit
        BQ76940规则: CELLBAL1 bit[4:0]=Cell1~5, CELLBAL2=Cell6~10, CELLBAL3=Cell11~15
        同寄存器内相邻bit同时置1会被芯片拒绝"""
        group_a = 0
        group_b = 0
        # 逐寄存器处理 (每5 bit一组)
        for reg_base in (0, 5, 10):
            prev_in_a = False
            for bit_off in range(5):
                bit_pos = reg_base + bit_off
                if not (mask & (1 << bit_pos)):
                    prev_in_a = False
                    continue
                # 如果上一个相邻bit在A组, 当前放B组
                if prev_in_a:
                    group_b |= (1 << bit_pos)
                    prev_in_a = False  # B组之后下一个可以回A
                else:
                    group_a |= (1 << bit_pos)
                    prev_in_a = True
        return group_a, group_b

    def _auto_balance(self, phase=None):
        """智能均衡: 自动分AB组避免相邻冲突, phase=None时单次执行A组"""
        bms = self.bms
        voltages = bms.valid_voltages()
        valid_v = [v for v in voltages if v > 0]
        if not valid_v:
            self._append_raw("[智能均衡] 无有效电压数据, 请先读取状态\n")
            return

        v_min = min(valid_v)
        v_max = max(valid_v)
        diff = v_max - v_min

        if diff < 15:
            self._send("bal off")
            self._append_raw(f"[智能均衡] 压差仅 {diff}mV, 无需均衡, 已关闭\n")
            return

        # 均衡阈值: 高于最低值 10mV 以上的节都需要均衡
        threshold = v_min + 10
        full_mask = 0
        bal_cells = []
        for i, ch in enumerate(VALID_CHANNELS):
            v = voltages[i]
            if v > threshold:
                full_mask |= (1 << (ch - 1))
                bal_cells.append(f"C{i+1}({v})")

        if full_mask == 0:
            self._send("bal off")
            self._append_raw("[智能均衡] 无需均衡\n")
            return

        group_a, group_b = self._split_nonadj(full_mask)

        # 决定当前发哪组
        if phase is None:
            phase = 0  # 手动单击默认A组
        if phase == 0:
            send_mask = group_a
            tag = "A"
        else:
            send_mask = group_b if group_b else group_a
            tag = "B" if group_b else "A"

        self._send(f"bal mask 0x{send_mask:04X}")
        self._append_raw(
            f"[智能均衡-{tag}组] 最低={v_min} 压差={diff}mV\n"
            f"  全部需均衡: {', '.join(bal_cells)}\n"
            f"  完整mask=0x{full_mask:04X} → A=0x{group_a:04X} B=0x{group_b:04X}\n"
            f"  本次发送{tag}组: 0x{send_mask:04X}\n"
        )

    def _toggle_auto_bal(self):
        """切换自动循环均衡"""
        if self.var_auto_bal.get():
            self.auto_bal_phase = 0
            self._append_raw("[自动均衡] 已开启, 每15秒A/B组交替均衡\n")
            self.lbl_auto_bal.config(text="运行中...")
            self._auto_bal_tick()
        else:
            if self.auto_bal_id is not None:
                self.root.after_cancel(self.auto_bal_id)
                self.auto_bal_id = None
            self.lbl_auto_bal.config(text="")
            self._append_raw("[自动均衡] 已关闭\n")

    def _auto_bal_tick(self):
        """自动均衡定时器: 读状态→1.5s后发当前相位组→15s后切换相位再来"""
        if not self.var_auto_bal.get():
            return
        phase = self.auto_bal_phase
        # 发读取状态命令获取最新电压
        self._send("status")
        # 等 1.5 秒让数据返回并解析完, 再执行智能均衡
        self.root.after(1500, lambda: self._auto_balance(phase=phase))
        # 切换相位
        self.auto_bal_phase = 1 - self.auto_bal_phase
        # 15 秒后再次执行
        self.auto_bal_id = self.root.after(15000, self._auto_bal_tick)

    def _send_custom(self):
        cmd = self.ent_cmd.get().strip()
        if cmd:
            self._send(cmd)
            self.ent_cmd.delete(0, "end")

    # ==================== 接收与解析 ====================

    def _rx_loop(self):
        """后台线程: 持续接收串口数据, 检测完整帧后解析"""
        buf = ""
        while self.running:
            try:
                if self.ser and self.ser.is_open and self.ser.in_waiting:
                    chunk = self.ser.read(self.ser.in_waiting)
                    text = chunk.decode("ascii", errors="replace")
                    buf += text
                    self.root.after(0, self._append_raw, text)

                    # 以 "Filter:" 行作为帧结束标志
                    if "Filter:" in buf:
                        self._parse_frame(buf)
                        buf = ""
                    # 备用: 参考程序的分隔符
                    elif "========================" in buf:
                        self._parse_frame(buf)
                        buf = ""
                else:
                    time.sleep(0.05)
            except Exception:
                time.sleep(0.1)

    def _parse_frame(self, text):
        """解析固件 BQ76940_PrintData 输出"""
        bms = self.bms
        bms.timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Cell01: 3789 mV (raw=3784, ok=1, bad=0)
        for m in re.finditer(r"Cell(\d+):\s*(\d+)\s*mV\s*\(raw=(\d+),\s*ok=(\d+)", text):
            ch = int(m.group(1))
            mv = int(m.group(2))
            ok = int(m.group(4))
            bms.cell_all[ch] = mv
            bms.cell_ok[ch] = (ok == 1)

        m = re.search(r"Pack:\s*(\d+)\s*mV", text)
        if m:
            bms.pack_mv = int(m.group(1))

        m = re.search(r"SOC:\s*(\d+)\s*%", text)
        if m:
            bms.soc = int(m.group(1))

        m = re.search(r"Bal:\s*0x([0-9A-Fa-f]+)", text)
        if m:
            bms.balance_mask = int(m.group(1), 16)

        m = re.search(r"Temp:\s*([\d.]+)\s*C", text)
        if m:
            bms.temp = float(m.group(1))

        m = re.search(r"Curr:\s*(-?\d+)\s*mA", text)
        if m:
            bms.current_mA = int(m.group(1))

        m = re.search(r"CHG:\s*(\w+)\s+DSG:\s*(\w+)", text)
        if m:
            bms.chg_on = (m.group(1) == "ON")
            bms.dsg_on = (m.group(2) == "ON")

        m = re.search(r"Prot:\s*OV=(\d+)\s+UV=(\d+)\s+OC=(\d+)\s+OT=(\d+)", text)
        if m:
            bms.prot_ov = int(m.group(1))
            bms.prot_uv = int(m.group(2))
            bms.prot_oc = int(m.group(3))
            bms.prot_ot = int(m.group(4))

        m = re.search(r"HW:\s*OV=(\d+)\s+UV=(\d+)\s+SCD=(\d+)\s+OCD=(\d+)", text)
        if m:
            bms.hw_ov = int(m.group(1))
            bms.hw_uv = int(m.group(2))
            bms.hw_scd = int(m.group(3))
            bms.hw_ocd = int(m.group(4))

        m = re.search(r"GAIN:\s*(\d+)\s*uV.*Offset:\s*(-?\d+)\s*mV", text)
        if m:
            bms.gain = int(m.group(1))
            bms.offset = int(m.group(2))

        self.root.after(0, self._update_ui)
        if self.log_writer:
            self._write_csv_row()

    # ==================== UI 刷新 ====================

    def _update_ui(self):
        bms = self.bms
        voltages = bms.valid_voltages()  # 9个有效节电压

        # --- 电芯电压进度条 ---
        for i in range(NUM_CELLS):
            v = voltages[i]
            self.cell_bars[i]["value"] = v
            if v > 0:
                self.cell_labels[i].config(text=f"{v} mV")
                if v > 4200 or v < 2800:
                    self.cell_labels[i].config(foreground="red")
                elif v < 3200 or v > 4100:
                    self.cell_labels[i].config(foreground="#CC8800")
                else:
                    self.cell_labels[i].config(foreground="black")
            else:
                self.cell_labels[i].config(text="---- mV", foreground="gray")

        # --- 统计 ---
        self.lbl_max.config(text=f"Max: {bms.cell_max()} mV")
        self.lbl_min.config(text=f"Min: {bms.cell_min()} mV")
        diff = bms.cell_diff()
        color = "red" if diff > 50 else ("#CC8800" if diff > 20 else "black")
        self.lbl_diff.config(text=f"Diff: {diff} mV", foreground=color)

        # --- 系统信息 (总压用有效节求和) ---
        real_pack = bms.real_pack()
        self.info_labels["pack_mv"].config(
            text=f"{real_pack} mV  ({real_pack/1000:.2f} V)")
        self.info_labels["current"].config(text=f"{bms.current_mA} mA")
        self.info_labels["temp"].config(text=f"{bms.temp:.1f} °C")
        self.info_labels["soc"].config(text=f"{bms.soc} %")
        self.info_labels["chg"].config(
            text="ON" if bms.chg_on else "OFF",
            foreground="green" if bms.chg_on else "red")
        self.info_labels["dsg"].config(
            text="ON" if bms.dsg_on else "OFF",
            foreground="green" if bms.dsg_on else "red")

        if bms.balance_mask == 0:
            self.info_labels["balance"].config(text="无均衡")
        else:
            cells = [str(ch) for ch in VALID_CHANNELS if bms.balance_mask & (1 << (ch - 1))]
            self.info_labels["balance"].config(
                text=f"0x{bms.balance_mask:04X} ({','.join(cells) if cells else 'none'})")

        # --- 保护灯 ---
        prot_map = {
            "OV": bms.prot_ov, "UV": bms.prot_uv,
            "OC": bms.prot_oc, "OT": bms.prot_ot,
            "HW_OV": bms.hw_ov, "HW_UV": bms.hw_uv,
            "HW_SCD": bms.hw_scd, "HW_OCD": bms.hw_ocd,
        }
        for name, val in prot_map.items():
            lbl = self.prot_labels[name]
            if val:
                lbl.config(bg="#f38ba8", text=f" {name}! ")
            else:
                lbl.config(bg="#a6e3a1", text=f"  {name}  " if len(name) <= 3 else f" {name} ")

        # --- 曲线 + 报警 ---
        self._update_trends()
        self._update_alarm()

    def _update_trends(self):
        bms = self.bms
        real_pack = bms.real_pack()
        self.hist_pack_v.append(real_pack / 1000.0)
        self.hist_current_a.append(bms.current_mA / 1000.0)
        self.hist_temp.append(bms.temp)
        self._draw_trend()

    def _draw_trend(self):
        c = self.cvs_trend
        c.delete("all")
        c.update_idletasks()
        w = c.winfo_width() or 380
        h = c.winfo_height() or 200
        left, right = 50, w - 8
        gap = 4  # 子区域间距

        series = [
            (list(self.hist_pack_v),    "#f5c542", "总压V"),
            (list(self.hist_current_a), "#39d0d8", "电流A"),
            (list(self.hist_temp),      "#ff6b6b", "温度℃"),
        ]
        n_rows = len(series)
        row_h = (h - 16 - gap * (n_rows - 1)) / n_rows  # 每行可用高度

        for row, (vals, color, label) in enumerate(series):
            top = int(8 + row * (row_h + gap))
            bottom = int(top + row_h)

            # 背景框 + 网格
            c.create_rectangle(left, top, right, bottom, outline="#38424f")
            mid_y = (top + bottom) // 2
            c.create_line(left, mid_y, right, mid_y, fill="#1f2a33", dash=(2, 4))

            # 标签
            c.create_text(left + 4, top + 2, text=label, fill=color,
                          anchor="nw", font=("Consolas", 7))

            # 画曲线
            self._draw_series(c, vals, left, top + 2, right, bottom - 2, color, "")

        c.create_text(left, h - 4, text="旧←", fill="#9aa4b2", anchor="w", font=("Consolas", 8))
        c.create_text(right, h - 4, text="→新", fill="#9aa4b2", anchor="e", font=("Consolas", 8))

    @staticmethod
    def _draw_series(canvas, values, left, top, right, bottom, color, unit=""):
        if len(values) < 2:
            return
        vmin, vmax = min(values), max(values)
        if abs(vmax - vmin) < 1e-6:
            vmax = vmin + 1.0

        canvas.create_text(left - 4, top, text=f"{vmax:.1f}{unit}",
                           fill=color, anchor="e", font=("Consolas", 7))
        canvas.create_text(left - 4, bottom, text=f"{vmin:.1f}{unit}",
                           fill=color, anchor="e", font=("Consolas", 7))

        latest = values[-1]
        ratio_l = (latest - vmin) / (vmax - vmin)
        y_l = bottom - (bottom - top) * ratio_l
        canvas.create_text(right + 2, y_l, text=f"{latest:.1f}",
                           fill=color, anchor="w", font=("Consolas", 7))

        points = []
        n = len(values)
        for i, v in enumerate(values):
            x = left + (right - left) * i / (n - 1)
            ratio = (v - vmin) / (vmax - vmin)
            y = bottom - (bottom - top) * ratio
            points.extend((x, y))
        canvas.create_line(*points, fill=color, width=2, smooth=True)

    def _update_alarm(self):
        bms = self.bms
        reasons = []
        if bms.prot_ov or bms.hw_ov:
            reasons.append("过压")
        if bms.prot_uv or bms.hw_uv:
            reasons.append("欠压")
        if bms.prot_oc or bms.hw_ocd or bms.hw_scd:
            reasons.append("过流")
        if bms.prot_ot:
            reasons.append("过温")
        if bms.cell_diff() > 80:
            reasons.append("压差过大")

        alarm_active = len(reasons) > 0
        if alarm_active:
            txt = "; ".join(reasons)
            self.lbl_alarm.config(text=txt, fg="red")
        else:
            self.lbl_alarm.config(text="正常", fg="green")

        if self.var_alarm_enable.get() and alarm_active and not self.alarm_active_prev:
            if winsound:
                try:
                    winsound.Beep(2000, 250)
                    winsound.Beep(1800, 250)
                except Exception:
                    self.root.bell()
            else:
                self.root.bell()
            messagebox.showwarning("BMS报警", f"检测到异常: {txt}")

        self.alarm_active_prev = alarm_active

    # ==================== 自动刷新 ====================

    def _toggle_auto(self):
        self.auto_refresh = self.var_auto.get()
        if self.auto_refresh:
            self._auto_tick()

    def _auto_tick(self):
        if not self.auto_refresh:
            return
        self._send("status")
        try:
            interval = int(float(self.spn_interval.get()) * 1000)
        except ValueError:
            interval = 2000
        self.root.after(interval, self._auto_tick)

    # ==================== CSV 记录 ====================

    def _toggle_log(self):
        if self.log_file:
            self._stop_log()
        else:
            self._start_log()

    def _start_log(self):
        fname = f"bms_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        try:
            self.log_file = open(fname, "w", newline="", encoding="utf-8-sig")
            self.log_writer = csv.writer(self.log_file)
            header = ["时间"]
            for i in range(NUM_CELLS):
                header.append(f"Cell{i+1}(mV)")
            header += ["总电压(mV)", "电流(mA)", "温度(℃)", "SOC(%)",
                       "CHG", "DSG", "均衡Mask", "OV", "UV", "OC", "OT"]
            self.log_writer.writerow(header)
            self.btn_log.config(text="停止记录")
            self.lbl_log.config(text=f"记录中: {fname}", foreground="red")
        except Exception as e:
            messagebox.showerror("错误", f"创建日志失败: {e}")

    def _stop_log(self):
        if self.log_file:
            self.log_file.close()
            self.log_file = None
            self.log_writer = None
        self.btn_log.config(text="开始记录CSV")
        self.lbl_log.config(text="已停止", foreground="gray")

    def _write_csv_row(self):
        if not self.log_writer:
            return
        bms = self.bms
        voltages = bms.valid_voltages()
        row = [bms.timestamp]
        for v in voltages:
            row.append(v)
        real_pack = bms.real_pack()
        row += [real_pack, bms.current_mA, bms.temp, bms.soc,
                "ON" if bms.chg_on else "OFF",
                "ON" if bms.dsg_on else "OFF",
                f"0x{bms.balance_mask:04X}",
                bms.prot_ov, bms.prot_uv, bms.prot_oc, bms.prot_ot]
        self.log_writer.writerow(row)
        self.log_file.flush()

    # ==================== 工具 ====================

    def _append_raw(self, text):
        self.txt_raw.config(state="normal")
        self.txt_raw.insert("end", text)
        self.txt_raw.see("end")
        lines = int(self.txt_raw.index("end-1c").split(".")[0])
        if lines > 500:
            self.txt_raw.delete("1.0", f"{lines - 400}.0")
        self.txt_raw.config(state="disabled")

    def _clear_raw(self):
        self.txt_raw.config(state="normal")
        self.txt_raw.delete("1.0", "end")
        self.txt_raw.config(state="disabled")

    def on_close(self):
        self.running = False
        self.auto_refresh = False
        if self.log_file:
            self.log_file.close()
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()


# ==================== 程序入口 ====================
if __name__ == "__main__":
    root = tk.Tk()
    app = BMSUpperComputer(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
