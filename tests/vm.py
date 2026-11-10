"""Machine virtuelle QEMU pilotee au clavier PS/2 via le moniteur."""
import os, re, socket, subprocess, tempfile, time

IMG = "build/lilos.img"
QEMU = os.environ.get("QEMU", "qemu-system-i386")
KEYS = {" ": "spc", "\n": "ret", "-": "minus", ".": "dot", "\b": "backspace",
        "\x01": "up", "\x02": "down", "\x0c": "ctrl-l", "/": "slash",
        ",": "comma", ";": "semicolon", "'": "apostrophe", "=": "equal",
        "[": "bracket_left", "]": "bracket_right", "\\": "backslash", "`": "grave_accent",
        "*": "shift-8", "+": "shift-equal", "%": "shift-5", "_": "shift-minus"}


class Vm:
    def __init__(self, cpu=None):
        self.dir = tempfile.mkdtemp()
        self.log = os.path.join(self.dir, "serial.log")
        self.mon = os.path.join(self.dir, "mon")
        self.proc = subprocess.Popen(
            [QEMU, "-drive", f"format=raw,file={IMG}", "-display", "none",
             "-serial", f"file:{self.log}", "-monitor", f"unix:{self.mon},server,nowait",
             "-no-reboot"] + (["-cpu", cpu] if cpu else []),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(self.mon):
                break
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(self.mon)
        self.sock.settimeout(0.05)
        self.drain()

    def drain(self):
        try:
            while self.sock.recv(4096):
                pass
        except (socket.timeout, OSError):
            pass

    def text(self):
        try:
            return open(self.log, errors="replace").read()
        except FileNotFoundError:
            return ""

    def wait(self, pattern, since=0, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.text()[since:])
            if m:
                return True
            time.sleep(0.1)
        return False

    def type(self, line, delay=0.06, enter=True):
        for c in line + ("\n" if enter else ""):
            key = f"shift-{c.lower()}" if c.isupper() else KEYS.get(c, c)
            self.sock.sendall(f"sendkey {key}\n".encode())
            time.sleep(delay)
            self.drain()

    def close(self):
        self.proc.kill()
        self.proc.wait()


