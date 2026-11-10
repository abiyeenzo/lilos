"""Machine virtuelle QEMU pilotee au clavier PS/2 via le moniteur."""
import os, re, socket, subprocess, tempfile, time

IMG = "build/lilos.img"
QEMU = os.environ.get("QEMU", "qemu-system-i386")
# Disposition AZERTY francaise. sendkey designe la touche par sa position
# physique (nom US) : cette table donne ce que la touche produit en AZERTY.
# (normal, majuscule, AltGr)
AZERTY = {
    "grave_accent": ("²", None, None),
    "1": ("&", "1", None), "2": ("é", "2", "~"), "3": ('"', "3", "#"), "4": ("'", "4", "{"),
    "5": ("(", "5", "["), "6": ("-", "6", "|"), "7": ("è", "7", "`"), "8": ("_", "8", "\\"),
    "9": ("ç", "9", "^"), "0": ("à", "0", "@"), "minus": (")", "°", "]"), "equal": ("=", "+", "}"),
    "q": ("a", "A", None), "w": ("z", "Z", None), "e": ("e", "E", None), "r": ("r", "R", None),
    "t": ("t", "T", None), "y": ("y", "Y", None), "u": ("u", "U", None), "i": ("i", "I", None),
    "o": ("o", "O", None), "p": ("p", "P", None),
    "bracket_left": ("^", "¨", None), "bracket_right": ("$", "£", None),
    "a": ("q", "Q", None), "s": ("s", "S", None), "d": ("d", "D", None), "f": ("f", "F", None),
    "g": ("g", "G", None), "h": ("h", "H", None), "j": ("j", "J", None), "k": ("k", "K", None),
    "l": ("l", "L", None), "semicolon": ("m", "M", None), "apostrophe": ("ù", "%", None),
    "backslash": ("*", "µ", None),
    "z": ("w", "W", None), "x": ("x", "X", None), "c": ("c", "C", None), "v": ("v", "V", None),
    "b": ("b", "B", None), "n": ("n", "N", None), "m": (",", "?", None),
    "comma": (";", ".", None), "dot": (":", "/", None), "slash": ("!", "§", None),
    "less": ("<", ">", None),
}
CHAR2KEY = {"\x03": "ctrl-c", " ": "spc", "\n": "ret", "\b": "backspace", "\x01": "up", "\x02": "down",
            "\x0c": "ctrl-l", "\t": "tab"}
for _key, (_n, _s, _a) in AZERTY.items():
    for _ch, _mod in ((_n, ""), (_s, "shift-"), (_a, "alt_r-")):
        if _ch:
            CHAR2KEY[_ch] = _mod + _key


class Vm:
    def __init__(self, cpu=None, net=False):
        self.dir = tempfile.mkdtemp()
        self.log = os.path.join(self.dir, "serial.log")
        self.mon = os.path.join(self.dir, "mon")
        self.proc = subprocess.Popen(
            [QEMU, "-drive", f"format=raw,file={IMG}", "-display", "none",
             "-serial", f"file:{self.log}", "-monitor", f"unix:{self.mon},server,nowait",
             "-no-reboot"] + (["-cpu", cpu] if cpu else []) + (["-nic", "user,model=rtl8139"] if net else []),
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
            return open(self.log, encoding="utf-8", errors="replace").read()
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
            key = CHAR2KEY[c]
            self.sock.sendall(f"sendkey {key}\n".encode())
            time.sleep(delay)
            self.drain()

    def close(self):
        self.proc.kill()
        self.proc.wait()




PROMPT = r"\w+[#$] "
ROOT_PW = "rootpw1"
USER, USER_PW = "alice", "alicepw"


def _step(vm, pattern, text, pos, timeout=20):
    """Attend un message apres pos, puis tape la reponse. Retourne la nouvelle position."""
    if not vm.wait(pattern, pos, timeout):
        raise RuntimeError(f"attendu /{pattern}/ :\n" + vm.text()[pos:])
    end = len(vm.text())
    vm.type(text, delay=0.03)
    return end


def provision(vm):
    """Premier demarrage : mot de passe root, puis un compte utilisateur."""
    p = 0
    p = _step(vm, r"Nouveau mot de passe", ROOT_PW, p)
    p = _step(vm, r"Confirmez", ROOT_PW, p)
    p = _step(vm, r"Nom du compte", USER, p)
    p = _step(vm, r"Nouveau mot de passe", USER_PW, p)
    _step(vm, r"Confirmez", USER_PW, p)


def login(vm, user="root", pw=ROOT_PW, expect_prompt=True):
    pos = max(0, len(vm.text()) - 4000)
    p = _step(vm, r"lilos login :", user, pos)
    p = _step(vm, r"Mot de passe :", pw, p)
    if expect_prompt and not vm.wait(PROMPT, p, 20):
        raise RuntimeError("connexion echouee :\n" + vm.text()[p:])
