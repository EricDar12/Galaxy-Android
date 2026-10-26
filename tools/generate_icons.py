import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from tools.icon_star_renderer import draw_crystal_starbits, render_mario_galaxy_star_3d, draw_plus_crest

def create_galaxy_nebula(W, H, is_plus=False):
    """Generates a deep, rich cosmic galaxy background."""
    y, x = np.ogrid[:H, :W]
    cx, cy = W / 2.0, H / 2.0
    nx = (x - cx) / (W / 2.0)
    ny = (y - cy) / (H / 2.0)
    r = np.sqrt(nx * nx + ny * ny)
    theta = np.arctan2(ny, nx)

    if not is_plus:
        bg_r = 7.0 + 15.0 * (1.0 - np.clip(r, 0, 1.2))
        bg_g = 10.0 + 18.0 * (1.0 - np.clip(r, 0, 1.2))
        bg_b = 26.0 + 44.0 * (1.0 - np.clip(r, 0, 1.2))
    else:
        bg_r = 16.0 + 26.0 * (1.0 - np.clip(r, 0, 1.2))
        bg_g = 6.0 + 12.0 * (1.0 - np.clip(r, 0, 1.2))
        bg_b = 30.0 + 55.0 * (1.0 - np.clip(r, 0, 1.2))

    log_r = np.log(np.clip(r, 0.04, 2.0))
    spiral_angle = theta - 3.4 * log_r
    arm1 = np.clip(np.cos(spiral_angle), 0, 1) ** 3.5
    arm2 = np.clip(np.cos(spiral_angle + math.pi), 0, 1) ** 3.5
    arms = (arm1 + arm2) * np.exp(-1.4 * r)

    if not is_plus:
        bg_r += arms * 70.0 + np.exp(-2.5 * r) * 75.0
        bg_g += arms * 105.0 + np.exp(-2.5 * r) * 70.0
        bg_b += arms * 200.0 + np.exp(-2.0 * r) * 125.0
    else:
        bg_r += arms * 145.0 + np.exp(-2.5 * r) * 110.0
        bg_g += arms * 45.0 + np.exp(-2.5 * r) * 50.0
        bg_b += arms * 210.0 + np.exp(-2.0 * r) * 140.0

    core = np.exp(-3.8 * (r ** 2))
    bg_r += core * 125.0
    bg_g += core * 140.0
    bg_b += core * 165.0

    img_arr = np.dstack([
        np.clip(bg_r, 0, 255).astype(np.uint8),
        np.clip(bg_g, 0, 255).astype(np.uint8),
        np.clip(bg_b, 0, 255).astype(np.uint8),
        np.full((H, W), 255, dtype=np.uint8)
    ])
    nebula = Image.fromarray(img_arr, "RGBA")

    np.random.seed(101 if not is_plus else 202)
    star_draw = ImageDraw.Draw(nebula)
    for _ in range(450):
        sx = np.random.randint(0, W)
        sy = np.random.randint(0, H)
        bright = np.random.randint(140, 255)
        sz = np.random.choice([1, 1, 1, 2, 2, 3], p=[0.60, 0.20, 0.10, 0.05, 0.03, 0.02])
        col = (bright, bright, bright, np.random.randint(120, 250))
        star_draw.ellipse([sx - sz, sy - sz, sx + sz, sy + sz], fill=col)

    return nebula

def draw_smooth_ring_arc(img, cx, cy, rx, ry, tilt_deg, thick, fill_color, spec_color, angle_range=(0, 360)):
    W, H = img.size
    start_deg, end_deg = angle_range
    rad_tilt = math.radians(tilt_deg)
    cos_t, sin_t = math.cos(rad_tilt), math.sin(rad_tilt)

    aura_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    a_draw = ImageDraw.Draw(aura_layer)
    for deg in range(start_deg, end_deg, 2):
        rad = math.radians(deg)
        x_e = rx * math.cos(rad)
        y_e = ry * math.sin(rad)
        xr = cx + x_e * cos_t - y_e * sin_t
        yr = cy + x_e * sin_t + y_e * cos_t
        r_glow = thick * 1.5
        a_draw.ellipse([xr - r_glow, yr - r_glow, xr + r_glow, yr + r_glow], fill=(*fill_color[:3], 90))
    aura_layer = aura_layer.filter(ImageFilter.GaussianBlur(radius=18))
    img.alpha_composite(aura_layer)

    ring_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    r_draw = ImageDraw.Draw(ring_layer)
    for deg in range(start_deg, end_deg, 1):
        rad = math.radians(deg)
        x_e = rx * math.cos(rad)
        y_e = ry * math.sin(rad)
        xr = cx + x_e * cos_t - y_e * sin_t
        yr = cy + x_e * sin_t + y_e * cos_t
        depth_scale = 1.0 + 0.22 * math.sin(rad - math.radians(tilt_deg))
        cur_thick = thick * depth_scale * 0.5
        r_draw.ellipse([xr - cur_thick, yr - cur_thick, xr + cur_thick, yr + cur_thick], fill=fill_color)

    for deg in range(start_deg, end_deg, 1):
        rad = math.radians(deg)
        x_e = rx * math.cos(rad)
        y_e = ry * math.sin(rad)
        xr = cx + x_e * cos_t - y_e * sin_t
        yr = cy + x_e * sin_t + y_e * cos_t
        spec_factor = max(0.0, math.sin(rad * 2.0 - math.pi * 0.3)) ** 4
        if spec_factor > 0.05:
            s_rad = thick * 0.28 * spec_factor
            r_draw.ellipse([xr - s_rad, yr - s_rad - 2, xr + s_rad, yr + s_rad - 2], fill=(*spec_color[:3], int(230 * spec_factor)))

    img.alpha_composite(ring_layer)

def generate_icon(output_webp, is_plus=False):
    W, H = 1024, 1024
    cx, cy = 512, 512

    canvas = create_galaxy_nebula(W, H, is_plus=is_plus)
    draw_crystal_starbits(canvas, cx, cy, is_plus=is_plus)

    ring_col = (250, 190, 25, 255) if not is_plus else (175, 225, 255, 255)
    spec_col = (255, 255, 235, 255) if not is_plus else (255, 255, 255, 255)

    draw_smooth_ring_arc(canvas, cx, cy, rx=350, ry=160, tilt_deg=-22, thick=26, fill_color=ring_col, spec_color=spec_col, angle_range=(180, 360))

    if is_plus:
        ring2_col = (255, 120, 220, 255)
        draw_smooth_ring_arc(canvas, cx, cy, rx=410, ry=185, tilt_deg=26, thick=20, fill_color=ring2_col, spec_color=(255, 255, 255, 255), angle_range=(180, 360))

    render_mario_galaxy_star_3d(canvas, cx, cy, R_out=265, is_plus=is_plus)

    draw_smooth_ring_arc(canvas, cx, cy, rx=350, ry=160, tilt_deg=-22, thick=26, fill_color=ring_col, spec_color=spec_col, angle_range=(0, 180))

    if is_plus:
        draw_smooth_ring_arc(canvas, cx, cy, rx=410, ry=185, tilt_deg=26, thick=20, fill_color=ring2_col, spec_color=(255, 255, 255, 255), angle_range=(0, 180))
        draw_plus_crest(canvas, W, H)

    out_512 = canvas.resize((512, 512), Image.Resampling.LANCZOS)
    out_512.save(output_webp, "WEBP", quality=96)
    png_path = output_webp.replace(".webp", ".png")
    out_512.save(png_path, "PNG")
    print(f"Generated {output_webp} and {png_path} successfully!")

if __name__ == "__main__":
    os.makedirs("out-icons", exist_ok=True)
    generate_icon("out-icons/ic_launcher.webp", is_plus=False)
    generate_icon("out-icons/ic_launcher_plus.webp", is_plus=True)
