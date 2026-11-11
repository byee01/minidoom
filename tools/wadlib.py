"""Minimal WAD reader/writer shared by the build tools."""
import struct


class Wad:
    def __init__(self, kind, lumps):
        self.kind = kind            # b"IWAD" / b"PWAD"
        self.lumps = lumps          # list of (name:str, data:bytes)

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            buf = f.read()
        kind, n, diroff = struct.unpack_from("<4sii", buf, 0)
        lumps = []
        for i in range(n):
            off, size, name = struct.unpack_from("<ii8s", buf, diroff + 16 * i)
            name = name.rstrip(b"\0").decode("ascii", "replace").upper()
            lumps.append((name, buf[off:off + size]))
        return cls(kind, lumps)

    def save(self, path):
        body = bytearray()
        dir_entries = []
        off = 12
        for name, data in self.lumps:
            dir_entries.append((off, len(data), name))
            body += data
            off += len(data)
        out = bytearray(struct.pack("<4sii", self.kind, len(self.lumps), off))
        out += body
        for o, s, name in dir_entries:
            out += struct.pack("<ii8s", o if s else 0, s, name.encode("ascii"))
        with open(path, "wb") as f:
            f.write(out)
        return len(out)

    def index(self, name):
        """Last lump with this name (Doom semantics), or -1."""
        for i in range(len(self.lumps) - 1, -1, -1):
            if self.lumps[i][0] == name:
                return i
        return -1

    def get(self, name):
        i = self.index(name)
        return self.lumps[i][1] if i >= 0 else None
