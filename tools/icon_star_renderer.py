import math
from PIL import Image, ImageDraw, ImageFilter

def draw_crystal_starbits(img, cx, cy, is_plus=False):
    """Draws multi-colored crystal starbits floating in orbit."""
    draw = ImageDraw.Draw(img)
    starbits = [
        (cx - 360, cy - 230, 24, (255, 60, 110)),
        (cx + 380, cy - 190, 28, (60, 230, 255)),
        (cx - 320, cy + 280, 26, (120, 255, 90)),
        (cx + 340, cy + 240, 25, (255, 225, 60)),
        (cx - 410, cy + 50,  20, (255, 170, 40)),
        (cx + 420, cy - 40,  22, (200, 100, 255)),
        (cx - 180, cy - 380, 18, (255, 255, 255)),
        (cx + 200, cy + 390, 19, (255, 120, 200)),
    ]
    for sx, sy, rad, col in starbits:
        glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        g_draw = ImageDraw.Draw(glow)
        g_draw.ellipse([sx - rad * 1.8, sy - rad * 1.8, sx + rad * 1.8, sy + rad * 1.8], fill=(*col, 120))
        glow = glow.filter(ImageFilter.GaussianBlur(radius=rad * 0.7))
        img.alpha_composite(glow)

        pts = []
        for i in range(8):
            ang = i * (math.pi / 4.0)
            r_cur = rad if (i % 2 == 0) else (rad * 0.6)
            pts.append((sx + r_cur * math.cos(ang), sy + r_cur * math.sin(ang)))
        draw.polygon(pts, fill=(*col, 255))
        core_pts = []
        for i in range(8):
            ang = i * (math.pi / 4.0)
            r_cur = (rad * 0.45) if (i % 2 == 0) else (rad * 0.25)
            core_pts.append((sx + r_cur * math.cos(ang), sy + r_cur * math.sin(ang)))
        draw.polygon(core_pts, fill=(255, 255, 255, 230))

def render_mario_galaxy_star_3d(img, cx, cy, R_out=265, is_plus=False):
    """Renders the authentic 3D faceted Super Mario Galaxy Power Star."""
    W, H = img.size
    R_in = R_out * 0.44
    Z_center = R_out * 0.68

    aura = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    a_draw = ImageDraw.Draw(aura)
    pts_aura = []
    for i in range(10):
        ang = -math.pi / 2.0 + i * (math.pi / 5.0)
        r = (R_out + 45) if (i % 2 == 0) else (R_in + 35)
        pts_aura.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    aura_col = (255, 215, 60, 160) if not is_plus else (0, 230, 255, 175)
    a_draw.polygon(pts_aura, fill=aura_col)
    aura = aura.filter(ImageFilter.GaussianBlur(radius=32))
    img.alpha_composite(aura)

    outer_pts_2d = []
    outer_pts_3d = []
    for i in range(10):
        ang = -math.pi / 2.0 + i * (math.pi / 5.0)
        r = R_out if (i % 2 == 0) else R_in
        px = cx + r * math.cos(ang)
        py = cy + r * math.sin(ang)
        outer_pts_2d.append((px, py))
        outer_pts_3d.append((px, py, 0.0))

    center_3d = (cx, cy, Z_center)

    star_base = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sb_draw = ImageDraw.Draw(star_base)
    pts_border = []
    for i in range(10):
        ang = -math.pi / 2.0 + i * (math.pi / 5.0)
        r = (R_out + 6) if (i % 2 == 0) else (R_in + 4)
        pts_border.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    border_col = (185, 80, 0, 255) if not is_plus else (120, 15, 110, 255)
    sb_draw.polygon(pts_border, fill=border_col)

    lx, ly, lz = -0.48, -0.62, 0.62
    l_len = math.sqrt(lx*lx + ly*ly + lz*lz)
    lx, ly, lz = lx / l_len, ly / l_len, lz / l_len

    vx, vy, vz = 0.0, 0.0, 1.0
    hx, hy, hz = lx + vx, ly + vy, lz + vz
    h_len = math.sqrt(hx*hx + hy*hy + hz*hz)
    hx, hy, hz = hx / h_len, hy / h_len, hz / h_len

    for i in range(10):
        p1 = outer_pts_3d[i]
        p2 = outer_pts_3d[(i + 1) % 10]
        v1 = (p1[0] - center_3d[0], p1[1] - center_3d[1], p1[2] - center_3d[2])
        v2 = (p2[0] - center_3d[0], p2[1] - center_3d[1], p2[2] - center_3d[2])
        nx = v1[1] * v2[2] - v1[2] * v2[1]
        ny = v1[2] * v2[0] - v1[0] * v2[2]
        nz = v1[0] * v2[1] - v1[1] * v2[0]
        n_len = math.sqrt(nx*nx + ny*ny + nz*nz)
        if n_len > 0:
            nx, ny, nz = nx / n_len, ny / n_len, nz / n_len
            if nz < 0:
                nx, ny, nz = -nx, -ny, -nz
        diff = max(0.0, nx * lx + ny * ly + nz * lz)
        spec = max(0.0, nx * hx + ny * hy + nz * hz) ** 16

        if not is_plus:
            base_r, base_g, base_b = 255, 218, 20
            r = int(min(255, base_r * (0.35 + 0.65 * diff) + 240 * spec))
            g = int(min(255, base_g * (0.35 + 0.65 * diff) + 240 * spec))
            b = int(min(255, base_b * (0.25 + 0.40 * diff) + 210 * spec))
        else:
            base_r, base_g, base_b = 245, 235, 125
            r = int(min(255, base_r * (0.40 + 0.60 * diff) + 250 * spec))
            g = int(min(255, base_g * (0.40 + 0.60 * diff) + 250 * spec))
            b = int(min(255, base_b * (0.30 + 0.50 * diff) + 240 * spec))

        tri_2d = [(cx, cy), outer_pts_2d[i], outer_pts_2d[(i + 1) % 10]]
        sb_draw.polygon(tri_2d, fill=(r, g, b, 255))

    img.alpha_composite(star_base)

    soft = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    s_draw = ImageDraw.Draw(soft)
    center_glow = (255, 240, 110, 140) if not is_plus else (255, 245, 160, 150)
    s_draw.ellipse([cx - R_in * 0.8, cy - R_in * 0.7, cx + R_in * 0.8, cy + R_in * 0.8], fill=center_glow)
    soft = soft.filter(ImageFilter.GaussianBlur(radius=24))
    img.alpha_composite(soft)

    spec_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sp_draw = ImageDraw.Draw(spec_layer)
    top_p = outer_pts_2d[0]
    sp_draw.ellipse([top_p[0] - 25, top_p[1] + 15, top_p[0] + 25, top_p[1] + 65], fill=(255, 255, 255, 190))
    spec_layer = spec_layer.filter(ImageFilter.GaussianBlur(radius=10))
    img.alpha_composite(spec_layer)

    eye_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    eye_draw = ImageDraw.Draw(eye_layer)
    eye_w = R_out * 0.125
    eye_h = R_out * 0.34
    eye_spacing = R_out * 0.22
    eye_cy = cy + R_out * 0.05

    left_eye_cx = cx - eye_spacing * 0.5
    right_eye_cx = cx + eye_spacing * 0.5

    for ecx in [left_eye_cx, right_eye_cx]:
        eye_draw.ellipse([ecx - eye_w * 0.5, eye_cy - eye_h * 0.5 + 3, ecx + eye_w * 0.5, eye_cy + eye_h * 0.5 + 3], fill=(140, 70, 0, 170))
        eye_draw.ellipse([ecx - eye_w * 0.5, eye_cy - eye_h * 0.5, ecx + eye_w * 0.5, eye_cy + eye_h * 0.5], fill=(16, 18, 30, 255))
        glint_w = eye_w * 0.46
        glint_h = eye_h * 0.36
        glint_cx = ecx + eye_w * 0.10
        glint_cy = eye_cy - eye_h * 0.24
        eye_draw.ellipse([glint_cx - glint_w * 0.5, glint_cy - glint_h * 0.5, glint_cx + glint_w * 0.5, glint_cy + glint_h * 0.5], fill=(255, 255, 255, 255))
        sec_w = eye_w * 0.22
        sec_h = eye_h * 0.18
        sec_cx = ecx - eye_w * 0.12
        sec_cy = eye_cy + eye_h * 0.26
        eye_draw.ellipse([sec_cx - sec_w * 0.5, sec_cy - sec_h * 0.5, sec_cx + sec_w * 0.5, sec_cy + sec_h * 0.5], fill=(255, 255, 255, 200))

    blush = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bl_draw = ImageDraw.Draw(blush)
    blush_w = R_out * 0.18
    blush_h = R_out * 0.10
    blush_y = eye_cy + eye_h * 0.46
    for ecx in [left_eye_cx - eye_w * 0.9, right_eye_cx + eye_w * 0.9]:
        bl_draw.ellipse([ecx - blush_w * 0.5, blush_y - blush_h * 0.5, ecx + blush_w * 0.5, blush_y + blush_h * 0.5], fill=(255, 125, 55, 130))
    blush = blush.filter(ImageFilter.GaussianBlur(radius=8))
    img.alpha_composite(blush)
    img.alpha_composite(eye_layer)

def draw_plus_crest(img, W, H):
    """Draws a bold, high-tech, glowing 3D Plus badge in the lower-right."""
    badge_cx = W * 0.77
    badge_cy = H * 0.76
    badge_r = W * 0.175
    draw = ImageDraw.Draw(img)

    aura = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    a_draw = ImageDraw.Draw(aura)
    a_draw.ellipse([badge_cx - badge_r * 1.5, badge_cy - badge_r * 1.5, badge_cx + badge_r * 1.5, badge_cy + badge_r * 1.5], fill=(0, 235, 255, 180))
    aura = aura.filter(ImageFilter.GaussianBlur(radius=28))
    img.alpha_composite(aura)

    draw.ellipse([badge_cx - badge_r - 8, badge_cy - badge_r - 8, badge_cx + badge_r + 8, badge_cy + badge_r + 8], fill=(0, 229, 255, 255))
    draw.ellipse([badge_cx - badge_r - 3, badge_cy - badge_r - 3, badge_cx + badge_r + 3, badge_cy + badge_r + 3], fill=(255, 215, 0, 255))
    draw.ellipse([badge_cx - badge_r, badge_cy - badge_r, badge_cx + badge_r, badge_cy + badge_r], fill=(18, 8, 42, 255))
    draw.ellipse([badge_cx - badge_r * 0.88, badge_cy - badge_r * 0.88, badge_cx + badge_r * 0.88, badge_cy + badge_r * 0.88], fill=(32, 14, 68, 255))

    plus_len = badge_r * 0.62
    plus_w = badge_r * 0.27
    rad = plus_w * 0.45

    draw.rounded_rectangle([badge_cx - plus_len, badge_cy - plus_w * 0.5 + 3, badge_cx + plus_len, badge_cy + plus_w * 0.5 + 3], radius=rad, fill=(5, 2, 15, 200))
    draw.rounded_rectangle([badge_cx - plus_w * 0.5, badge_cy - plus_len + 3, badge_cx + plus_w * 0.5, badge_cy + plus_len + 3], radius=rad, fill=(5, 2, 15, 200))

    draw.rounded_rectangle([badge_cx - plus_len, badge_cy - plus_w * 0.5, badge_cx + plus_len, badge_cy + plus_w * 0.5], radius=rad, fill=(255, 255, 255, 255))
    draw.rounded_rectangle([badge_cx - plus_w * 0.5, badge_cy - plus_len, badge_cx + plus_w * 0.5, badge_cy + plus_len], radius=rad, fill=(255, 255, 255, 255))

    draw.rounded_rectangle([badge_cx - (plus_len - 3), badge_cy - (plus_w * 0.5 - 3), badge_cx + (plus_len - 3), badge_cy + (plus_w * 0.5 - 3)], radius=rad * 0.8, fill=(0, 240, 255, 255))
    draw.rounded_rectangle([badge_cx - (plus_w * 0.5 - 3), badge_cy - (plus_len - 3), badge_cx + (plus_w * 0.5 - 3), badge_cy + (plus_len - 3)], radius=rad * 0.8, fill=(255, 220, 60, 255))

    draw.arc([badge_cx - badge_r * 0.86, badge_cy - badge_r * 0.86, badge_cx + badge_r * 0.86, badge_cy + badge_r * 0.86], start=190, end=350, fill=(255, 255, 255, 220), width=4)
