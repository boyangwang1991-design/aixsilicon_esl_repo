"""Independent untimed byte oracle. No SystemC or scheduling code imported.

Trace contract requires a happens-before edge for overlapping writes/read-writes;
reject data races rather than invent a global ordering absent from the hardware.
"""

from __future__ import annotations


def evaluate(config, rows):
    stores = [bytearray(e["size"]) for e in config["endpoints"]]
    ancestors, accesses, expected = {}, {}, {}

    def region(address, length):
        for i, e in enumerate(config["endpoints"]):
            if e["base"] <= address and address + length <= e["base"] + e["size"]:
                return i, address - e["base"]
        raise ValueError("oracle: unmapped range")

    def read(address, count):
        i, offset = region(address, count)
        return bytes(stores[i][offset : offset + count])

    def write(address, data, enables=None):
        i, offset = region(address, len(data))
        for n, byte in enumerate(data):
            if enables is None or enables[n]:
                stores[i][offset + n] = byte

    for row in rows:
        identity = row["id"]
        parents = set(row["depends"])
        for parent in row["depends"]:
            parents.update(ancestors[parent])
        ancestors[identity] = parents
        op, a, n = row["op"], row["address"], row["bytes"]
        spans = []
        if op in ("READ", "COPY", "MULTICAST"):
            spans.append((a, a + n, False))
        if op == "WRITE":
            spans.append((a, a + n, True))
        for d in row["destinations"]:
            spans.append((d, d + n, True))
        for prior, others in accesses.items():
            if prior in parents:
                continue
            if any(a < d and c < b and (w or v) for a, b, w in spans for c, d, v in others):
                raise ValueError(f"data race: {prior} -> {identity} needs dependency")
        accesses[identity] = spans
        if op == "READ":
            expected[identity] = read(a, n)
        elif op == "WRITE":
            enables = bytes.fromhex(row["enables"]) if row["enables"] else None
            write(a, bytes.fromhex(row["data"]), enables)
        elif op in ("COPY", "MULTICAST"):
            data = read(a, n)
            for destination in row["destinations"]:
                if a < destination + n and destination < a + n:
                    raise ValueError("DMA overlap forbidden")
                write(destination, data)
    return expected, stores
