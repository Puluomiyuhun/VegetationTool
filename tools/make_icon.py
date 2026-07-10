"""生成 SlowTree 应用图标 (assets/app.ico)。
风格: 圆角渐变背景 + 程序化树 (棕色主干 + 分叉枝条 + 绿色叶簇), 呼应 🌳 SlowTree 品牌。
纯 PIL 绘制, 无需外部素材; 输出多分辨率 .ico。
"""
import math, os
from PIL import Image, ImageDraw

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def draw_icon(S):
    # 超采样抗锯齿
    ss = 4
    N = S * ss
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 圆角渐变背景 (深青绿 -> 明绿)
    top = (32, 92, 66)
    bot = (76, 175, 80)
    bg = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    bgd = ImageDraw.Draw(bg)
    for y in range(N):
        c = lerp(top, bot, y / N)
        bgd.line([(0, y), (N, y)], fill=c + (255,))
    # 圆角遮罩
    mask = Image.new("L", (N, N), 0)
    md = ImageDraw.Draw(mask)
    r = int(N * 0.22)
    md.rounded_rectangle([0, 0, N - 1, N - 1], radius=r, fill=255)
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)

    cx = N // 2

    # ---- 树干 (从底部向上, 略带锥度) ----
    trunk_col = (94, 60, 32)
    trunk_col2 = (120, 78, 42)
    base_y = int(N * 0.86)
    top_y = int(N * 0.46)
    bw_bot = N * 0.075
    bw_top = N * 0.032
    d.polygon([
        (cx - bw_bot, base_y), (cx + bw_bot, base_y),
        (cx + bw_top, top_y), (cx - bw_top, top_y)
    ], fill=trunk_col)
    # 主干高光
    d.polygon([
        (cx - bw_bot * 0.35, base_y), (cx + bw_bot * 0.1, base_y),
        (cx + bw_top * 0.1, top_y), (cx - bw_top * 0.35, top_y)
    ], fill=trunk_col2)

    # ---- 分叉枝条 ----
    def branch(x, y, ang, length, width, depth):
        if depth == 0 or length < N * 0.03:
            return
        ex = x + math.cos(ang) * length
        ey = y - math.sin(ang) * length
        d.line([(x, y), (ex, ey)], fill=trunk_col, width=max(1, int(width)))
        branch(ex, ey, ang + 0.5, length * 0.7, width * 0.65, depth - 1)
        branch(ex, ey, ang - 0.5, length * 0.7, width * 0.65, depth - 1)

    fork_y = int(N * 0.5)
    branch(cx, fork_y, math.radians(70), N * 0.16, bw_top * 1.6, 3)
    branch(cx, fork_y, math.radians(110), N * 0.16, bw_top * 1.6, 3)

    # ---- 叶簇 (多个绿色圆, 深浅错落) ----
    greens = [(56, 142, 60), (76, 175, 80), (104, 196, 108), (46, 125, 50)]
    canopy = [
        (cx, int(N * 0.34), N * 0.24),
        (cx - N * 0.19, int(N * 0.44), N * 0.17),
        (cx + N * 0.19, int(N * 0.44), N * 0.17),
        (cx - N * 0.10, int(N * 0.28), N * 0.15),
        (cx + N * 0.10, int(N * 0.28), N * 0.15),
        (cx, int(N * 0.46), N * 0.16),
    ]
    for i, (ux, uy, ur) in enumerate(canopy):
        col = greens[i % len(greens)]
        d.ellipse([ux - ur, uy - ur, ux + ur, uy + ur], fill=col + (255,))
    # 顶部高光叶
    d.ellipse([cx - N * 0.11, int(N * 0.20), cx + N * 0.05, int(N * 0.34)],
              fill=(140, 210, 140, 255))

    img = img.resize((S, S), Image.LANCZOS)
    return img

def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets")
    os.makedirs(out_dir, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [draw_icon(s) for s in sizes]
    ico_path = os.path.join(out_dir, "app.ico")
    imgs[-1].save(ico_path, format="ICO",
                  sizes=[(s, s) for s in sizes])
    # 顺便存一张 png 预览
    imgs[-1].save(os.path.join(out_dir, "app_icon.png"))
    print("wrote", os.path.abspath(ico_path))

if __name__ == "__main__":
    main()
