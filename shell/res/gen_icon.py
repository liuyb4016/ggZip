# 生成 shell/res/ggzip.ico —— 32x32 32bpp：蓝色圆角方块 + 白色拉链
# 纯标准库实现（不依赖 PIL），便于 CI 与本地重新生成。
import struct

W = H = 32
BOX = (47, 109, 181, 255)      # 蓝
DARK = (31, 76, 133, 255)      # 深蓝（拉链齿 / 描边）
WHITE = (245, 249, 253, 255)   # 拉链底


def inside_rounded(x, y, r=7):
    if x < r and y < r and (x - r) ** 2 + (y - r) ** 2 > r * r:
        return False
    if x >= W - r and y < r and (x - (W - 1 - r)) ** 2 + (y - r) ** 2 > r * r:
        return False
    if x < r and y >= H - r and (x - r) ** 2 + (y - (H - 1 - r)) ** 2 > r * r:
        return False
    if x >= W - r and y >= H - r and (x - (W - 1 - r)) ** 2 + (y - (H - 1 - r)) ** 2 > r * r:
        return False
    return True


def pixel(x, y):
    if not inside_rounded(x, y):
        return (0, 0, 0, 0)
    # 拉链条：x 13..18
    if 13 <= x <= 18:
        if y <= 3 or y >= 22:
            pass                      # 上下留出边距
        elif y >= 23:
            return WHITE              # 拉头部分
        elif (y // 3) % 2 == 0 and 15 <= x <= 16:
            return DARK               # 中缝链齿
        return WHITE
    # 四角小铆钉点缀
    if (x in (4, 27) and y in (4, 27)):
        return DARK
    return BOX


def main():
    xor = bytearray()
    for y in range(H - 1, -1, -1):        # BMP 自底向上
        for x in range(W):
            r, g, b, a = pixel(x, y)
            xor += struct.pack("<BBBB", b, g, r, a)
    and_mask = bytearray()
    for y in range(H - 1, -1, -1):
        row = 0
        for x in range(W):
            if pixel(x, y)[3] == 0:
                row |= 1 << (31 - x)      # 1 = 透明位
        and_mask += struct.pack(">I", row)

    bih = struct.pack("<IiiHHIIiiII", 40, W, H * 2, 1, 32, 0,
                      len(xor) + len(and_mask), 0, 0, 0, 0)
    image = bih + bytes(xor) + bytes(and_mask)

    ico = struct.pack("<HHH", 0, 1, 1)
    ico += struct.pack("<BBBBHHII", W, H, 0, 0, 1, 32, len(image), 6 + 16)
    ico += image

    with open(__file__.replace("gen_icon.py", "ggzip.ico"), "wb") as f:
        f.write(ico)
    print("ggzip.ico written,", len(ico), "bytes")


if __name__ == "__main__":
    main()
