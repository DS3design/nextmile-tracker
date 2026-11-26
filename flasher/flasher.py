"""Next Mile Tracker flasher - standalone Windows app.

Pick a USB port (or auto-detect), detect which display chip the board has,
and flash the correct firmware. Firmware images live in the `firmware` folder
next to the exe and are described by `manifest.json`, so new boards and new
builds can be added without rebuilding the app.
"""
import json
import os
import queue
import re
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial
import serial.tools.list_ports
import esptool

HERE = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
FW_DIR = os.path.join(HERE, 'firmware')
MANIFEST = os.path.join(FW_DIR, 'manifest.json')
PY = sys.executable


def load_manifest():
    with open(MANIFEST, 'r', encoding='utf-8') as f:
        return json.load(f)


def fw_path(name):
    p = os.path.join(FW_DIR, name)
    if not os.path.isfile(p):
        raise FileNotFoundError(f'missing firmware file: {name}')
    return p


def list_ports():
    out = []
    for p in serial.tools.list_ports.comports():
        label = f'{p.device} - {p.description}'
        usb = any(k in (p.description or '') for k in ('CH340', 'CH341', 'USB-SERIAL', 'USB2.0-Serial', 'CP210', 'FTDI'))
        out.append((p.device, label, usb))
    return out


def esptool_write(port, images, log, baud='460800'):
    """images: list of (offset, filepath). Runs esptool in-process."""
    args = ['--chip', 'esp32', '--port', port, '--baud', baud,
            'write-flash'] + [x for pair in images for x in pair]
    oldout, olderr = sys.stdout, sys.stderr

    class Tee:
        def write(self, s):
            log(s.rstrip())
            olderr.write(s)
        def flush(self):
            pass

    sys.stderr = Tee()
    sys.stdout = Tee()
    try:
        esptool.main(args)
    finally:
        sys.stdout, sys.stderr = oldout, olderr


def reset_and_read(port, seconds, log):
    """Reset the board and read serial lines for `seconds`. Returns lines."""
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.rts = True
    time.sleep(0.2)
    s.rts = False
    t = time.time()
    buf = b''
    lines = []
    while time.time() - t < seconds:
        buf += s.read(4096)
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            txt = line.decode('utf-8', 'replace').rstrip()
            lines.append(txt)
            log(txt)
    s.close()
    return lines


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Next Mile Tracker Flasher')
        self.minsize(560, 520)
        self.m = load_manifest()
        self.boards = {b['id']: b for b in self.m['boards']}
        self.work_q = queue.Queue()
        self.busy = False
        self.detected = None
        self._build()
        self.refresh_ports()
        self.after(200, self._drain)

    def _build(self):
        top = ttk.Frame(self, padding=10)
        top.pack(fill='x')

        ttk.Label(top, text='USB port:').grid(row=0, column=0, sticky='w')
        self.port_var = tk.StringVar()
        self.port_menu = ttk.Combobox(top, textvariable=self.port_var, state='readonly', width=38)
        self.port_menu.grid(row=0, column=1, sticky='w')
        ttk.Button(top, text='Refresh', command=self.refresh_ports).grid(row=0, column=2, padx=6)

        ttk.Label(top, text='Board:').grid(row=1, column=0, sticky='w', pady=(8, 0))
        self.board_var = tk.StringVar()
        self.board_menu = ttk.Combobox(top, textvariable=self.board_var, state='readonly', width=38,
                                       values=[f"{b['id']} - {b['name']}" for b in self.m['boards']])
        self.board_menu.grid(row=1, column=1, sticky='w', pady=(8, 0))
        if self.m['boards']:
            self.board_menu.current(0)

        ttk.Label(top, text='Firmware:').grid(row=2, column=0, sticky='w', pady=(8, 0))
        self.fw_var = tk.StringVar()
        self.fw_menu = ttk.Combobox(top, textvariable=self.fw_var, state='readonly', width=38)
        self.fw_menu.grid(row=2, column=1, sticky='w', pady=(8, 0))
        self.board_menu.bind('<<ComboboxSelected>>', lambda e: self._fill_fw())

        btns = ttk.Frame(self, padding=10)
        btns.pack(fill='x')
        self.detect_btn = ttk.Button(btns, text='Detect board', command=self.do_detect)
        self.detect_btn.pack(side='left')
        self.flash_btn = ttk.Button(btns, text='Flash', command=self.do_flash)
        self.flash_btn.pack(side='left', padx=8)
        self.flip_btn = ttk.Button(btns, text='Screen is upside down', command=self.do_flip, state='disabled')
        self.flip_btn.pack(side='left')
        self.keep_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(btns, text='Keep saved miles/settings (skip filesystem)', variable=self.keep_var).pack(side='left', padx=10)

        self.status_var = tk.StringVar(value='Plug in a board, pick its port, then Detect board.')
        ttk.Label(self, textvariable=self.status_var, padding=(10, 4)).pack(fill='x')

        self.logw = tk.Text(self, height=18, bg='#111', fg='#ddd', font=('Consolas', 9))
        self.logw.pack(fill='both', expand=True, padx=10, pady=(0, 10))
        self._fill_fw()

    def _fill_fw(self):
        sel = self.board_var.get().split(' - ')[0]
        b = self.boards.get(sel)
        if not b:
            return
        self.fw_menu['values'] = [f"{f['id']} - {f['name']}" for f in b['firmwares']]
        if b['firmwares']:
            self.fw_menu.current(0)

    def log(self, s):
        if s:
            self.work_q.put(('log', s))

    def _drain(self):
        try:
            while True:
                kind, payload = self.work_q.get_nowait()
                if kind == 'log':
                    self.logw.insert('end', payload + '\n')
                    self.logw.see('end')
                elif kind == 'status':
                    self.status_var.set(payload)
                elif kind == 'done':
                    self.busy = False
                    for b in (self.detect_btn, self.flash_btn):
                        b.config(state='normal')
                    ok, msg = payload
                    if ok:
                        self.flip_btn.config(state='normal')
                elif kind == 'detected':
                    fw_id = payload
                    self._select_fw(fw_id)
        except queue.Empty:
            pass
        self.after(150, self._drain)

    def _select_fw(self, fw_id):
        sel = self.board_var.get().split(' - ')[0]
        b = self.boards.get(sel)
        for i, f in enumerate(b['firmwares']):
            if f['id'] == fw_id:
                self.fw_menu.current(i)
                self.detected = fw_id
                self.work_q.put(('status', f'Detected {f["name"]}. Click Flash.'))
                return

    def refresh_ports(self):
        ports = list_ports()
        self.port_menu['values'] = [lbl for _, lbl, _ in ports]
        devs = {lbl: dev for dev, lbl, _ in ports}
        self._port_map = devs
        if ports:
            usb = [lbl for _, lbl, u in ports if u]
            self.port_menu.current(0 if not usb else [lbl for _, lbl, _ in ports].index(usb[0]))

    def _port(self):
        lbl = self.port_var.get()
        dev = self._port_map.get(lbl, lbl.split(' ')[0])
        return dev

    def _board(self):
        return self.boards[self.board_var.get().split(' - ')[0]]

    def _fw(self):
        fid = self.fw_var.get().split(' - ')[0]
        return next(f for f in self._board()['firmwares'] if f['id'] == fid)

    def _start(self, target, status):
        if self.busy:
            return
        self.busy = True
        for b in (self.detect_btn, self.flash_btn, self.flip_btn):
            b.config(state='disabled')
        self.work_q.put(('status', status))
        threading.Thread(target=target, daemon=True).start()

    def do_detect(self):
        self._start(self._detect_work, 'Detecting board...')

    def _detect_work(self):
        port = self._port()
        b = self._board()
        probe = b.get('probe')
        if not probe:
            self.work_q.put(('status', 'This board has no detection probe - pick the firmware manually.'))
            self.work_q.put(('done', (True, '')))
            return
        try:
            self.log(f'--- flashing probe to {port} ---')
            esptool_write(port, self._base_images(b, for_probe=True) + [(b.get('app_offset', '0x10000'), fw_path(probe))], self.log)
            self.log('--- resetting, reading probe output ---')
            lines = reset_and_read(port, 8, self.log)
            result = None
            for ln in lines:
                mm = re.search(r'RESULT=(\w+)', ln)
                if mm:
                    result = mm.group(1)
                    break
            if result and result != 'UNKNOWN':
                fid = b['detect_map'].get(result)
                if fid:
                    self.work_q.put(('detected', fid))
                else:
                    self.work_q.put(('status', f'Probe found {result} but no firmware matches it.'))
            else:
                self.work_q.put(('status', f'Could not identify the panel ({result}). Pick the firmware manually.'))
            self.work_q.put(('done', (True, '')))
        except Exception as e:
            self.work_q.put(('status', f'Detect failed: {e}'))
            self.work_q.put(('done', (False, '')))

    def _base_images(self, b, for_probe=False):
        imgs = []
        for off, name in b.get('base', []):
            if self.keep_var.get() and not for_probe and 'littlefs' in name:
                continue
            imgs.append((off, fw_path(name)))
        return imgs

    def do_flash(self):
        self._start(lambda: self._flash_work(self._fw()['file']), 'Flashing...')

    def do_flip(self):
        fid = self.fw_var.get().split(' - ')[0]
        flip = fid + '_flip'
        if flip.endswith('_flip_flip'):
            flip = fid[:-5]
        b = self._board()
        if any(f['id'] == flip for f in b['firmwares']):
            self._select_fw(flip)
            self._start(lambda: self._flash_work(next(f for f in b['firmwares'] if f['id'] == flip)['file']),
                        'Flashing rotated firmware...')
        else:
            messagebox.showinfo('No rotated build', 'There is no rotated variant for this firmware yet.')

    def _flash_work(self, fwfile):
        port = self._port()
        b = self._board()
        try:
            images = self._base_images(b) + [(b.get('app_offset', '0x10000'), fw_path(fwfile))]
            self.log(f'--- flashing {fwfile} to {port} ---')
            esptool_write(port, images, self.log)
            self.log('--- resetting, reading boot output ---')
            lines = reset_and_read(port, 15, self.log)
            ok = any('StartAP' in ln or 'NextMile' in ln for ln in lines)
            if ok:
                self.work_q.put(('status', 'Done - setup WiFi on the screen. If it is upside down, use the rotate button.'))
            else:
                self.work_q.put(('status', 'Flashed, but no setup output seen. Check the screen and the log.'))
            self.work_q.put(('done', (True, '')))
        except Exception as e:
            self.work_q.put(('status', f'Flash failed: {e}'))
            self.work_q.put(('done', (False, '')))


if __name__ == '__main__':
    App().mainloop()
