/* 协议固件（-Variant proto）
 *
 * 6 字节帧：[class, sub, b2, b3, b4, b5]
 *   class 0  sub 0x4D         握手（设备发 MSN02，主机回 MSNCN）
 *   class 2  sub 0            x0, y0
 *   class 2  sub 1            x1, y1（终点，不是宽高）
 *   class 2  sub 2            底色 / 页号
 *   class 2  sub 3  op 0      纯色填充   colour = b3:b4
 *                   op 7      开始流式   set_window(x0,y0,x1-1,y1-1) + CS 拉低
 *                   op 8      推一段     len = b3:b4（满段 0x0100）
 *                   op 9      结束
 *                   op 0x0A   旋转（暂不处理）
 *   class 2  sub 4            段缓冲填充：4 字节循环铺满 256 字节
 *   class 4                   段缓冲写入：seg[b1*4 ..] = b2..b5
 *   class 8                   读 ADC：回 [0x08, ch, 0, 0, hi, lo]
 */
#include "fw.h"

static uint8_t seg[256];
static uint8_t rx[1024];
static uint8_t ver_pending;
static uint8_t ver_val;
static uint32_t raw_left;                 /* 还剩多少原始像素字节 */
static uint8_t stage[2][512];
static uint8_t stage_idx;
static uint32_t stage_len;
static uint8_t rle_mode;                 /* 0=原样 1=行程编码 */
static uint8_t rle_acc[3];
static uint8_t rle_n;

static uint32_t frame_px;

/* 16x16 分块签名：记录每一块“设备实际画上去的像素”，主机发帧后核对，
 * 只补发没画对的块。签名 = 加和 ^ 异或（每块 32 位，折成 16 位比对）。 */
#define TILE_COLS 10u
#define TILE_ROWS 5u
#define TILE_N    (TILE_COLS * TILE_ROWS)
static uint32_t tile_sum[TILE_N];
static uint32_t tile_xor[TILE_N];
static uint8_t  tile_written[TILE_N];
static int      tile_x;
static int      tile_y;
static int      tile_x0;
static int      tile_y0;
static int      tile_w;
static uint8_t  tile_hi;
static uint8_t  tile_half;
static uint8_t  sig_buf[TILE_N * 2u];
static uint16_t sig_left;
static uint8_t  sig_pos;
static uint8_t  sig_rx;
static uint8_t  up_rx_on;
static uint16_t up_rx_left;
static uint32_t up_idle;                 /* iterations spent waiting for upload data */
static uint32_t stream_bytes;

static uint8_t standby_mode = 1u;
static uint8_t boot_shown;    /* 屏幕上现在是开机画面：按 boot_hold 秒数停留 */
static uint8_t blink_first = 1u;      /* 待机画面还没画过 */
static uint8_t blink_on = 1u;         /* 当前方块颜色 1=白 0=黄 */
static uint32_t idle_ms;              /* 没收到任何字节的粗略毫秒数 */
static uint32_t win_px;   /* 当前流的窗口像素数，用于整帧角标判定 */

static settings_t g_settings;     /* NOR 里的设置：开机/待机模式 */
uint8_t g_boot_report;            /* 开机自检结果位图：bit0 FLASH / bit1 TOUCH / bit2 LCD / bit3 USB / bit4 SETTINGS */
static uint8_t    img_idx;        /* 待机轮播的当前张 */
static uint8_t    asset_rx;       /* 1 = 正在收资产数据 */
static uint32_t   asset_rx_left;  /* 还差多少字节 */
static uint8_t    asset_win;       /* 1 = 分窗模式，收满一窗回一个 ack */
static uint32_t   asset_idle;      /* 等资产数据时空转了多少圈（不用 SysTick） */
static uint32_t   asset_last_tick; /* 上一次收到资产数据的时刻（防卡死） */

static void standby_draw(void);
static void standby_tick(void);
static void iwdg_feed(void);

/* ---------- 复位原因面包屑 ----------
 * 放在链接脚本的 .noinit 段（RAM 最底部，不加载、不清零），不复位电源就跨复位保留。
 * 每次走到关键阶段就记一个码；异常复位（看门狗/硬件错）后这个码还留着，
 * 上位机一问就知道"上次是死在哪一步"。
 *   1=main 开始  2=usb_init 完成  3=开机画面  4=主循环  5=收像素
 *   6=写资产    7=淡入淡出      8=整片擦除  9=待机动画 */
#define TRACE_MAGIC 0x4D534252u

typedef struct {
    uint32_t magic;
    uint32_t stage;
    uint32_t boots;
    uint32_t aux;
} trace_t;

static volatile trace_t g_trace __attribute__((section(".noinit")));

#define TRACE (&g_trace)
#define TRACE_STAGE(s) (TRACE->stage = (uint32_t)(s))

static void trace_boot(void)
{
    if (TRACE->magic != TRACE_MAGIC) {
        TRACE->magic = TRACE_MAGIC;
        TRACE->stage = 0u;
        TRACE->boots = 0u;
        TRACE->aux = 0u;
    }
    TRACE->boots = TRACE->boots + 1u;
}


/* 待机动画里所有图形都是方形的：这颗屏分辨率低，圆画出来锯齿太明显，
 * 用户明确要求“用同大小的正方形”。 */
#define BALL_SZ   10          /* 正方形球：原来 4 的 2.5 倍 */
#define DOT_SZ     6          /* 拖尾点：原来 4 的 1.5 倍 */
#define TRAIL_N   13          /* 拖尾点数：原来 9 的约 1.5 倍 */
#define SCR_W     160
#define SCR_H      80

static int32_t ball_x5, ball_y5;      /* 位置以 1/5 像素存，速度才能是 0.6 倍 */
static int16_t ball_dx5, ball_dy5;    /* 9/5=1.8 px/步、6/5=1.2 px/步 */
static int16_t trail_cx[TRAIL_N], trail_cy[TRAIL_N];
static uint8_t trail_n;
static uint8_t trail_skip;
static uint16_t ball_col;
static uint8_t ball_col_idx;
static int16_t drawn_x, drawn_y;      /* 上一帧球的左上角 */
static uint8_t drawn_valid;

static const uint16_t BALL_COLS[6] = {
    0xFFFFu, 0xF800u, 0xFFE0u, 0x07E0u, 0x07FFu, 0xF81Fu
};

/* 把颜色按 num/den 变暗（拖尾与球同色，越老越暗） */
static uint16_t dim_color(uint16_t c, uint8_t num, uint8_t den)
{
    uint16_t r = (uint16_t)((c >> 11) & 0x1Fu);
    uint16_t g = (uint16_t)((c >> 5) & 0x3Fu);
    uint16_t b = (uint16_t)(c & 0x1Fu);

    r = (uint16_t)(r * num / den);
    g = (uint16_t)(g * num / den);
    b = (uint16_t)(b * num / den);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int16_t ball_left(void) { return (int16_t)(ball_x5 / 5); }
static int16_t ball_top(void)  { return (int16_t)(ball_y5 / 5); }
static int16_t ball_cx(void)   { return (int16_t)(ball_left() + BALL_SZ / 2); }
static int16_t ball_cy(void)   { return (int16_t)(ball_top() + BALL_SZ / 2); }

static uint32_t rng_state;

/* 简单的 LCG：每次进待机都换一个起点/方向/颜色，不用真随机数发生器 */
static uint32_t rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFFu;
}

static void ball_reset(void)
{
    uint8_t k;
    int32_t x, y;

    /* 每次进待机都混一次新种子：触摸通道读数每次都不一样（基准在 3900 上下浮动），
     * 再叠一个调用计数，保证同一块板子连续几次进待机也不会走同一条线。
     * 这里刻意不用 SysTick —— 这颗片的 SysTick 分频/方向不可靠。 */
    static uint32_t rng_calls;

    rng_state ^= ((uint32_t)adc_read(9u) << 7);
    rng_state ^= ((rng_calls++) * 2654435761u);
    if (rng_state == 0u)
        rng_state = 0x2545F491u;

    /* 起点：离四边各留 10 像素，避免一出生就贴边 */
    x = (int32_t)(rng_next() % (uint32_t)(SCR_W - BALL_SZ - 20)) + 10;
    y = (int32_t)(rng_next() % (uint32_t)(SCR_H - BALL_SZ - 20)) + 10;
    ball_x5 = x * 5;
    ball_y5 = y * 5;

    /* 方向：四个斜向里随机一个，速度仍是 1.8 / 1.2 像素每步（0.6 倍速） */
    ball_dx5 = (int16_t)((rng_next() & 1u) ? 9 : -9);
    ball_dy5 = (int16_t)((rng_next() & 1u) ? 6 : -6);

    ball_col_idx = (uint8_t)(rng_next() % 6u);
    ball_col = BALL_COLS[ball_col_idx];
    trail_n = 0u;
    trail_skip = 0u;
    drawn_valid = 0u;
    for (k = 0u; k < TRAIL_N; k++) {
        trail_cx[k] = ball_cx();
        trail_cy[k] = ball_cy();
    }
}

/* 擦掉旧球身上没被新球盖住的部分。正方形做差集很简单：
 * 上面几条、下面几条整条擦，中间重叠的行只擦左右两小条。
 * 每步只挪 1~2 像素，所以擦掉的永远是细边，屏上不会出现“整个球突然消失”。 */
static void erase_ball_leftover(int16_t nx, int16_t ny)
{
    int16_t ox0, oy0, ox1, oy1, nx0, ny0, nx1, ny1;
    int16_t y;

    if (!drawn_valid)
        return;
    ox0 = drawn_x; oy0 = drawn_y;
    ox1 = (int16_t)(ox0 + BALL_SZ - 1); oy1 = (int16_t)(oy0 + BALL_SZ - 1);
    nx0 = nx; ny0 = ny;
    nx1 = (int16_t)(nx0 + BALL_SZ - 1); ny1 = (int16_t)(ny0 + BALL_SZ - 1);

    for (y = oy0; y <= oy1; y++) {
        if (y < 0 || y >= SCR_H)
            continue;
        if (y < ny0 || y > ny1) {
            lcd_fill(ox0, y, BALL_SZ, 1, 0x0000u);
            continue;
        }
        if (ox0 < nx0) {
            int16_t w = (int16_t)((nx0 < ox1 + 1 ? nx0 : ox1 + 1) - ox0);

            if (w > 0)
                lcd_fill(ox0, y, w, 1, 0x0000u);
        }
        if (ox1 > nx1) {
            int16_t x = (int16_t)((nx1 + 1 > ox0 ? nx1 + 1 : ox0));

            if (x <= ox1)
                lcd_fill(x, y, (int16_t)(ox1 - x + 1), 1, 0x0000u);
        }
    }
}

static void ball_step(void)
{
    uint8_t k;
    uint8_t hit = 0u;
    int32_t nx5, ny5;
    int16_t ocx = ball_cx();
    int16_t ocy = ball_cy();

    /* 1) 每两步落一个拖尾点（1.8 px/步 → 约 3.6 px 一点） */
    if (trail_skip == 0u) {
        for (k = TRAIL_N - 1u; k > 0u; k--) {
            trail_cx[k] = trail_cx[k - 1u];
            trail_cy[k] = trail_cy[k - 1u];
        }
        trail_cx[0] = ocx;
        trail_cy[0] = ocy;
        if (trail_n < TRAIL_N)
            trail_n++;
    }
    trail_skip = (uint8_t)(trail_skip ^ 1u);

    /* 2) 移动并处理反弹（1/5 像素累加，0.6 倍速是准的） */
    nx5 = ball_x5 + ball_dx5;
    ny5 = ball_y5 + ball_dy5;
    if (nx5 <= 0) { nx5 = 0; ball_dx5 = (int16_t)(-ball_dx5); hit = 1u; }
    if (nx5 >= (int32_t)(SCR_W - BALL_SZ) * 5) {
        nx5 = (int32_t)(SCR_W - BALL_SZ) * 5;
        ball_dx5 = (int16_t)(-ball_dx5); hit = 1u;
    }
    if (ny5 <= 0) { ny5 = 0; ball_dy5 = (int16_t)(-ball_dy5); hit = 1u; }
    if (ny5 >= (int32_t)(SCR_H - BALL_SZ) * 5) {
        ny5 = (int32_t)(SCR_H - BALL_SZ) * 5;
        ball_dy5 = (int16_t)(-ball_dy5); hit = 1u;
    }
    ball_x5 = nx5;
    ball_y5 = ny5;

    /* 撞边就换色，拖尾跟着一起换 */
    if (hit) {
        ball_col_idx = (uint8_t)((ball_col_idx + 1u) % 6u);
        ball_col = BALL_COLS[ball_col_idx];
    }

    /* 3) 只擦细边，再画拖尾、最后画球 */
    erase_ball_leftover(ball_left(), ball_top());
    if (trail_n >= TRAIL_N)
        lcd_fill((int16_t)(trail_cx[TRAIL_N - 1u] - DOT_SZ / 2),
                 (int16_t)(trail_cy[TRAIL_N - 1u] - DOT_SZ / 2),
                 DOT_SZ, DOT_SZ, 0x0000u);

    for (k = 0u; k < trail_n; k++) {
        uint8_t num = (uint8_t)(TRAIL_N - k);

        lcd_fill((int16_t)(trail_cx[k] - DOT_SZ / 2),
                 (int16_t)(trail_cy[k] - DOT_SZ / 2),
                 DOT_SZ, DOT_SZ,
                 dim_color(ball_col, num, (uint8_t)(TRAIL_N + 1u)));
    }
    lcd_fill(ball_left(), ball_top(), BALL_SZ, BALL_SZ, ball_col);

    drawn_x = ball_left();
    drawn_y = ball_top();
    drawn_valid = 1u;
}

/* 画待机画面：黑屏 / 四角方块 / 图片轮播 / Ping-Pong */
/* 从 from 的下一个开始，找一个有图的槽；都没有返回 0xFF */
static uint8_t next_valid_slot(uint8_t from)
{
    uint8_t k;

    for (k = 1u; k <= STANDBY_SLOTS; k++) {
        uint8_t idx = (uint8_t)((from + k) % STANDBY_SLOTS);

        if (asset_valid(ASSET_STANDBY, idx))
            return idx;
    }
    return 0xFFu;
}

/* ---------- 待机图片交叉淡入淡出 ----------
 * 这块板上没有帧缓存，所以是"边读边混边推"：一条 2 行带子（640 字节）
 * 从 NOR 各读一份，按权重混合后整条推给屏。8 级权重用 >>3 完成，
 * SPI 24 MHz 下整屏一次过渡约 170 ms（8 步）。
 * 注意：读 flash 时必须让屏 CS 保持高（PA6 一脚两用），所以每条带子
 * 都是"先读、再开窗、再推、再抬 CS"，跟 asset_draw 一样。 */
#define FADE_STEPS 8u

static uint8_t fade_a[640];
static uint8_t fade_b[640];

static void fade_rows(const uint8_t *a, const uint8_t *b, uint32_t wa, uint32_t wb)
{
    int i;

    for (i = 0; i < 640; i += 2) {
        uint16_t pa = (uint16_t)(((uint16_t)a[i] << 8) | a[i + 1]);
        uint16_t pb = (uint16_t)(((uint16_t)b[i] << 8) | b[i + 1]);
        uint16_t r = (uint16_t)(((((pa >> 11) & 0x1Fu) * wa) + (((pb >> 11) & 0x1Fu) * wb)) >> 3);
        uint16_t g = (uint16_t)(((((pa >> 5) & 0x3Fu) * wa) + (((pb >> 5) & 0x3Fu) * wb)) >> 3);
        uint16_t bl = (uint16_t)((((pa & 0x1Fu) * wa) + ((pb & 0x1Fu) * wb)) >> 3);
        uint16_t o = (uint16_t)((r << 11) | (g << 5) | bl);

        fade_a[i] = (uint8_t)(o >> 8);
        fade_a[i + 1] = (uint8_t)(o & 0xFFu);
    }
}

/* from = 0xFF 表示从黑场淡入 */
static void standby_fade(uint8_t from, uint8_t to)
{
    uint8_t step;
    int y;
    uint32_t a_base;
    uint32_t b_base;

    if (!asset_valid(ASSET_STANDBY, to))
        return;
    a_base = (from == 0xFFu) ? 0u : asset_data_addr(ASSET_STANDBY, from);
    b_base = asset_data_addr(ASSET_STANDBY, to);

    TRACE_STAGE(7u);
    for (step = 1u; step <= FADE_STEPS; step++) {
        uint32_t wa = (uint32_t)(FADE_STEPS - step);
        uint32_t wb = (uint32_t)step;
        uint32_t ao = a_base;
        uint32_t bo = b_base;

        for (y = 0; y < 80; y += 2) {
            int i;

            if (from == 0xFFu) {
                for (i = 0; i < 640; i++)
                    fade_a[i] = 0u;                 /* 黑场 */
            } else {
                flash_read(ao, fade_a, 640);
                ao += 640u;
            }
            flash_read(bo, fade_b, 640);
            bo += 640u;
            fade_rows(fade_a, fade_b, wa, wb);
            lcd_stream_begin(0, y, 160, y + 2);
            lcd_raw_write(fade_a, 640);
            lcd_raw_idle();
        }
        iwdg_feed();
    }
}

static void standby_draw(void)
{
    if (g_settings.standby_mode == 0u) {
        lcd_fill(0, 0, 160, 80, 0x0000);
        return;
    }
    if (g_settings.standby_mode == 2u) {
        uint8_t first = next_valid_slot(0xFFu);    /* 0xFF 当作“从头找” */

        img_idx = (first == 0xFFu) ? 0u : first;
        if (first == 0xFFu || !asset_valid(ASSET_STANDBY, img_idx))
            lcd_fill(0, 0, 160, 80, 0x0000);
        else
            standby_fade(0xFFu, img_idx);          /* 从黑场淡入 */
        return;
    }
    if (g_settings.standby_mode == 3u) {
        lcd_fill(0, 0, 160, 80, 0x0000);
        ball_reset();
        lcd_fill(ball_left(), ball_top(), BALL_SZ, BALL_SZ, ball_col);
        drawn_x = ball_left();
        drawn_y = ball_top();
        drawn_valid = 1u;
        return;
    }
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(0, 0, 8, 8, 0xFFFFu);
    lcd_fill(152, 0, 8, 8, 0xFFFFu);
    lcd_fill(0, 72, 8, 8, 0xFFFFu);
    lcd_fill(152, 72, 8, 8, 0xFFFFu);
}

/* 待机画面的一次心跳：方块翻色 / 换下一张图 */
static void standby_tick(void)
{
    TRACE_STAGE(9u);
    if (g_settings.standby_mode == 2u) {
        uint8_t nx = next_valid_slot(img_idx);

        if (nx == 0xFFu) {
            lcd_fill(0, 0, 160, 80, 0x0000);
            return;
        }
        {
            uint8_t prev = asset_valid(ASSET_STANDBY, img_idx) ? img_idx : 0xFFu;

            img_idx = nx;
            standby_fade(prev, img_idx);           /* 上一张 → 这张，跨 8 步过渡 */
        }
        return;
    }
    if (g_settings.standby_mode == 3u) {
        ball_step();
        return;
    }
    if (g_settings.standby_mode == 1u) {
        uint16_t col;

        blink_on = (uint8_t)(blink_on ^ 1u);
        col = blink_on ? 0xFFFFu : 0xFC80u;   /* deeper amber, easier to tell from white */
        lcd_fill(0, 0, 8, 8, col);
        lcd_fill(152, 0, 8, 8, col);
        lcd_fill(0, 72, 8, 8, col);
        lcd_fill(152, 72, 8, 8, col);
    }
}

static void tile_reset_region(int rx0, int ry0, int rx1, int ry1)
{
    int tx, ty;

    for (ty = ry0 / 16; ty < (ry1 + 15) / 16; ty++) {
        if (ty < 0 || ty >= (int)TILE_ROWS)
            continue;
        for (tx = rx0 / 16; tx < (rx1 + 15) / 16; tx++) {
            uint32_t t;

            if (tx < 0 || tx >= (int)TILE_COLS)
                continue;
            t = (uint32_t)(ty * (int)TILE_COLS + tx);
            tile_sum[t] = 0u;
            tile_xor[t] = 0u;
            tile_written[t] = 1u;
        }
    }
}

static void tile_feed_byte(uint8_t b)
{
    uint32_t t;

    if (!tile_half) {
        tile_hi = b;
        tile_half = 1u;
        return;
    }
    tile_half = 0u;
    if (tile_w > 0 && tile_x >= tile_x0 && tile_x < tile_x0 + tile_w &&
        tile_y >= tile_y0 && tile_y < 80) {
        t = (uint32_t)((tile_y >> 4) * (int)TILE_COLS + (tile_x >> 4));
        if (t < TILE_N) {
            tile_sum[t] += (uint32_t)tile_hi * 3u + (uint32_t)b + 1u;
            tile_xor[t] ^= ((uint32_t)tile_hi << 8) | (uint32_t)b;
            tile_written[t] = 1u;
        }
    }
    tile_x++;
    if (tile_w > 0 && tile_x >= tile_x0 + tile_w) {
        tile_x = tile_x0;
        tile_y++;
    }
}

/* 所有送屏的像素都必须经过这里，才能边写边累积分块签名 */
static void stream_write(const uint8_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        tile_feed_byte(p[i]);
    lcd_raw_write(p, n);
}

static void stage_flush(void)
{
    TRACE_STAGE(5u);
    if (stage_len > 0u) {
        frame_px += (uint32_t)(stage_len >> 1);
        stream_write(stage[stage_idx], stage_len);
        stage_len = 0u;
        stage_idx ^= 1u;
    }
}

/* 行程编码：每三个字节一组 [像素数, 高字节, 低字节]，展开后进 stage。 */
static void rle_feed(uint8_t b)
{
    rle_acc[rle_n++] = b;
    if (rle_n < 3u)
        return;
    rle_n = 0u;
    {
        uint32_t cnt = rle_acc[0];
        uint8_t hi = rle_acc[1];
        uint8_t lo = rle_acc[2];

        while (cnt-- > 0u) {
            if (stage_len + 2u > (uint32_t)sizeof(stage[0]))
                stage_flush();
            stage[stage_idx][stage_len++] = hi;
            stage[stage_idx][stage_len++] = lo;
        }
    }
}
static uint32_t t_frame_start;
static volatile uint16_t last_frame_ms;

static uint32_t systick_now(void)
{
    return *(volatile uint32_t *)0xE000F008u;
}

static void systick_init(void)
{
    *(volatile uint32_t *)0xE000F010u = 0x00FFFFFFu;   /* CMP */
    *(volatile uint32_t *)0xE000F000u = 0x00000009u;   /* STRE | STE */
}
static volatile uint32_t frame_done;

static int x0, y0, x1, y1;
static int connected;
static uint8_t page_no;

static const uint8_t hello[6] = { 0x00, 0x4D, 0x53, 0x4E, 0x30, 0x32 };
static uint32_t hb_hold;		/* >0：暂缓心跳，给应答包让路 */

static void seg_fill4(const uint8_t *p)
{
    int i;
    for (i = 0; i < 256; i += 4) {
        seg[i]     = p[0];
        seg[i + 1] = p[1];
        seg[i + 2] = p[2];
        seg[i + 3] = p[3];
    }
}

static void handle_frame(const uint8_t *f)
{
    uint8_t cls = f[0];
    uint8_t sub = f[1];
    uint16_t a = (uint16_t)((f[2] << 8) | f[3]);
    uint16_t b = (uint16_t)((f[4] << 8) | f[5]);

    if (cls == 0x00u) {
        if (sub == 0x4Du) {
            connected = 1;
            usb_write(f, 6);
            return;
        }
        if (sub == 0x56u) {                       /* 版本查询：不主动发送，只应答 */
            uint8_t st[6];
            uint16_t tv = adc_read(9u);
            st[0] = 0x56u;
            st[1] = FW_VER_MAJOR;
            st[2] = FW_VER_MINOR;
            st[3] = (uint8_t)((tv >> 8) & 0xFFu);
            st[4] = (uint8_t)(tv & 0xFFu);
            st[5] = 0xAAu;
            usb_write(st, 6);
            return;
        }
        return;
    }

    if (cls == 0x02u) {
        switch (sub) {
        case 0x00:
            x0 = a;
            y0 = b;
            return;
        case 0x01:
            x1 = a;
            y1 = b;
            return;
        case 0x02:
            page_no = (uint8_t)a;
            return;
        case 0x03:
            switch (f[2]) {
            case 0x00:
                lcd_fill(x0, y0, x1 - x0, y1 - y0, (uint16_t)((f[3] << 8) | f[4]));
                return;
            case 0x04:                            /* 纯色填充，参数与上面相同 */
                lcd_fill(x0, y0, x1 - x0, y1 - y0, (uint16_t)((f[3] << 8) | f[4]));
                return;
            case 0x07:
                stream_bytes = 0u;
                t_frame_start = systick_now();
                standby_mode = 0u;
                boot_shown = 0u;
                win_px = (x1 > x0 && y1 > y0)
                       ? (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0) : 0u;
                lcd_stream_begin(x0, y0, x1, y1);
                tile_x0 = x0;
                tile_y0 = y0;
                tile_x = x0;
                tile_y = y0;
                tile_w = x1 - x0;
                tile_half = 0u;
                tile_reset_region(x0, y0, x1, y1);
                return;
            case 0x08:
                stream_write(seg, (uint32_t)((f[3] << 8) | f[4]));
                return;
            case 0x0Du:                          /* 压缩段：f[5]=1 行程编码 */
                stage_flush();
                rle_mode = f[5];
                rle_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                if (raw_left > 8192u) raw_left = 8192u;
                return;
            case 0x0Cu:
                if (stage_len > 0u) { stream_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                rle_mode = 0u;               /* 原样段：必须关掉上一段的行程解码 */
                rle_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                if (raw_left > 8192u) raw_left = 8192u;
                return;
            case 0x09: {
                uint32_t frame_px_before = frame_px;
                uint32_t win_px_before = win_px;
                if (stage_len > 0u) { stream_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                lcd_raw_idle();
                win_px = 0u;      /* 完整性色块已移除：像素对不对由裁决字节告诉上位机 */
                frame_px = 0u;
                frame_done++;
                last_frame_ms = (uint16_t)((systick_now() - t_frame_start) / 48000u);
                /* 每帧一个裁决字节：0x06 = 像素数对得上，0x15 = 少收了。
                 * 主机读它就能“发一帧等一次”，天然限速，永远不把接收环挤爆。 */
                ver_val = (uint8_t)((win_px_before == 0u || frame_px_before == win_px_before)
                                    ? 0x06u : 0x15u);
                ver_pending = 1u;
                return;
            }
            default:
                return;
            }
        case 0x04:
            seg_fill4(&f[2]);
            return;
        default:
            return;
        }
    }

    if (cls == 0x04u) {
        uint32_t base = (uint32_t)f[1] * 4u;
        if (base + 4u <= 256u) {
            seg[base]     = f[2];
            seg[base + 1] = f[3];
            seg[base + 2] = f[4];
            seg[base + 3] = f[5];
        }
        return;
    }

    if (cls == 0x13u) {                       /* 自更新：提交镜像（crc32 四字节） */
        uint8_t rr[6];
        uint32_t crc = ((uint32_t)f[2] << 24) | ((uint32_t)f[3] << 16) |
                       ((uint32_t)f[4] << 8) | (uint32_t)f[5];
        uint8_t ok = (uint8_t)(update_stage_commit(crc) ? 0x06u : 0x15u);

        up_rx_on = 0u;
        rr[0] = 0x13u; rr[1] = 0x00u; rr[2] = ok;
        rr[3] = 0u; rr[4] = 0u; rr[5] = 0u;
        usb_write(rr, 6);
        return;
    }
    if (cls == 0x14u) {                       /* 自更新：置“请更新”并立即擦写内部 flash */
        uint8_t rr[6];

        update_set_apply_flag();
        rr[0] = 0x14u; rr[1] = 0x00u; rr[2] = 0x06u;
        rr[3] = 0u; rr[4] = 0u; rr[5] = 0u;
        usb_write(rr, 6);
        delay_ms(60);
        /* v77 起由 0x0000 的常驻引导负责搬运：这里只写标志然后复位，
           应用自己的代码永远不会覆盖 0x0000-0x0FFF，所以不可能把自己写死。 */
        *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;
        for (;;)
            ;
        return;
    }

    if (cls == 0x0Au) {                       /* 资产：开机/待机图 + 设置 */
        uint8_t r[12];

        if (sub == 0x00u || sub == 0x07u) {    /* 查询 */
            r[0] = 0x0Au;
            r[1] = sub;
            r[2] = (uint8_t)(asset_valid(ASSET_BOOT, 0u) ? 1u : 0u);
            r[3] = 0u;
            {
                uint8_t k;
                for (k = 0u; k < 8u; k++)
                    if (asset_valid(ASSET_STANDBY, k))
                        r[3] = (uint8_t)(r[3] | (1u << k));
            }
            r[4] = g_settings.boot_mode;
            r[5] = g_settings.standby_mode;
            r[6] = g_settings.standby_count;
            r[7] = (uint8_t)((g_settings.dwell >> 8) & 0xFFu);
            r[8] = (uint8_t)(g_settings.dwell & 0xFFu);
            r[9] = FW_VER_MAJOR;
            r[10] = g_settings.boot_hold;
            r[11] = 0xAAu;
            usb_write(r, 12);
            return;
        }
        if (sub == 0x0Bu) {                    /* 读开机自检结果 */
            r[0] = 0x0Au; r[1] = 0x0Bu;
            r[2] = g_boot_report;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x12u) {                    /* 免拆自更新：begin（01）/ 一窗数据（02） */
            uint8_t ok;

            if (f[2] == 0x01u) {
                uint32_t len = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

                stream_bytes = 0u;      /* the stream budget must not cut an upload short */
                up_idle = 0u;
                up_rx_on = 0u;
                ok = (uint8_t)(update_stage_begin(len) ? 0x06u : 0x15u);
                r[0] = 0x0Au; r[1] = 0x12u; r[2] = ok;
                r[3] = 0u; r[4] = 0u; r[5] = 0u;
                usb_write(r, 6);
                return;
            }
            if (f[2] == 0x02u) {
                uint32_t wl = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

                if (wl == 0u || wl > 2048u)
                    wl = 2048u;
                up_rx_on = 1u;
                up_idle = 0u;
                up_rx_left = (uint16_t)wl;
                return;                        /* 收满回 ack */
            }
            r[0] = 0x0Au; r[1] = 0x12u; r[2] = 0x15u;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x0Fu) {                    /* 上次自更新的结果码 */
            uint8_t rr[6];

            rr[0] = 0x0Au; rr[1] = 0x0Fu; rr[2] = g_up_result;
            rr[3] = 0u; rr[4] = 0u; rr[5] = 0u;
            usb_write(rr, 6);
            return;
        }
        if (sub == 0x11u) {                    /* 分块签名核对：紧跟着就是 100 字节签名，收满回掩码 */
            sig_rx = 1u;
            sig_left = (uint16_t)(TILE_N * 2u);
            sig_pos = 0u;
            return;
        }
        if (sub == 0x01u) {                    /* 开始写：kind idx len_hi len_lo */
            uint32_t len = ((uint32_t)f[4] << 8) | (uint32_t)f[5];
            lcd_raw_idle();
            standby_mode = 0u;
            asset_begin(f[2], f[3], len);
            asset_rx = 0u;              /* 分窗模式：数据由 0A 08 一段一段喂 */
            asset_rx_left = 0u;
            asset_win = 0u;
            asset_last_tick = systick_now();
            r[0] = 0x0Au; r[1] = 0x01u;
            /* 注意：这里必须问 asset_writing()，不能再问 asset_rx——
             * 分窗模式下 asset_rx 恒为 0，之前照抄它导致 begin 永远回 0x15。 */
            r[2] = (uint8_t)(asset_writing() ? 0x06u : 0x15u);
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x08u) {                    /* 送来一段数据：len_hi len_lo，收满回 ack */
            uint32_t wl = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

            if (wl == 0u || wl > 4096u)
                wl = 4096u;
            if (!asset_writing()) {
                r[0] = 0x0Au; r[1] = 0x08u; r[2] = 0x15u;
                r[3] = 0u; r[4] = 0u; r[5] = 0u;
                usb_write(r, 6);
                return;
            }
            asset_rx = 1u;
            asset_rx_left = wl;
            asset_win = 1u;
            asset_last_tick = systick_now();
            return;                        /* 收满了再回 ack */
        }
        if (sub == 0x09u) {
            /* 开机画面停留秒数（1..60），只改设置区，不碰图片 */
            uint8_t secs = f[2];

            if (secs < 1u)
                secs = 1u;
            if (secs > 60u)
                secs = 60u;
            g_settings.boot_hold = secs;
            (void)settings_save(&g_settings);
            r[2] = 0x06u;
            r[3] = secs;
            usb_write(r, 6u);
            return;
        }
        if (sub == 0x02u) {                    /* 放弃这次写入 */
            asset_rx = 0u;
            asset_rx_left = 0u;
            r[0] = 0x0Au; r[1] = 0x02u; r[2] = 0x06u;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x03u) {                    /* 提交：crc32 在 b2..b5 */
            uint32_t crc = ((uint32_t)f[2] << 24) | ((uint32_t)f[3] << 16) |
                           ((uint32_t)f[4] << 8) | (uint32_t)f[5];
            uint8_t ok;

            asset_rx = 0u;
            asset_rx_left = 0u;
            ok = (uint8_t)(asset_finish(crc) ? 0x06u : 0x15u);
            r[0] = 0x0Au; r[1] = 0x03u; r[2] = ok;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x04u) {                    /* 立即显示某张图（预览） */
            uint8_t ok;

            lcd_raw_idle();
            standby_mode = 0u;
            blink_first = 0u;
            ok = (uint8_t)(asset_draw(f[2], f[3]) ? 0x06u : 0x15u);
            r[0] = 0x0Au; r[1] = 0x04u; r[2] = ok;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x05u) {                    /* 擦掉某槽 */
            asset_erase(f[2], f[3]);
            r[0] = 0x0Au; r[1] = 0x05u; r[2] = 0x06u;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x0Du) {                    /* 复位面包屑：上次停在哪一步 */
            uint32_t boots = TRACE->boots;

            r[0] = 0x0Au; r[1] = 0x0Du;
            r[2] = (uint8_t)(TRACE->stage & 0xFu);
            r[3] = (uint8_t)(boots & 0xFFu);
            r[4] = (uint8_t)((boots >> 8) & 0xFFu);
            r[5] = 0xAAu;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x0Eu) {                    /* 资产回读校验：kind idx */
            uint32_t calc = 0u;
            uint8_t ok = (uint8_t)asset_verify(f[2], f[3], &calc);

            r[0] = 0x0Au; r[1] = 0x0Eu;
            r[2] = (uint8_t)(ok ? 0x06u : 0x15u);
            r[3] = (uint8_t)(calc & 0xFFu);
            r[4] = (uint8_t)((calc >> 8) & 0xFFu);
            r[5] = 0xAAu;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x10u) {                    /* 动画步进探针：跑 n 次待机心跳，用来验证不会卡死 */
            uint8_t n = f[2];
            uint8_t i;

            if (n == 0u)
                n = 1u;
            if (n > 40u)
                n = 40u;
            for (i = 0u; i < n; i++) {
                standby_tick();
                iwdg_feed();
            }
            r[0] = 0x0Au; r[1] = 0x10u; r[2] = 0x06u;
            r[3] = n; r[4] = 0u; r[5] = 0xAAu;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x0Cu) {                    /* 读 NOR：addr(3) + len(1)，最多 48 字节 */
            uint32_t addr = ((uint32_t)f[2] << 16) | ((uint32_t)f[3] << 8) | (uint32_t)f[4];
            uint8_t n = f[5];
            uint8_t buf[52];

            if (n == 0u)
                n = 1u;
            if (n > 48u)
                n = 48u;
            flash_read(addr, &buf[3], (int)n);
            buf[0] = 0x0Au; buf[1] = 0x0Cu; buf[2] = n;
            buf[3u + n] = 0xAAu;
            usb_write(buf, (int)n + 4);
            return;
        }
        if (sub == 0x0Au) {                    /* 抹掉整片 1 MB NOR：恢复出厂 */
            TRACE_STAGE(8u);
            flash_erase_chip_now();            /* 0xC7 并等就绪，内部边等边喂狗 */
            asset_invalidate();                /* 缓存作废，否则仍以为槽里有图 */
            settings_default(&g_settings);     /* 只改内存；NOR 留空，下次上电自动用默认 */
            r[0] = 0x0Au; r[1] = 0x0Au; r[2] = 0x06u;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x06u) {                    /* 存设置：boot standby count dwell */
            g_settings.boot_mode = f[2];
            g_settings.standby_mode = f[3];
            g_settings.standby_count = f[4];
            g_settings.dwell = (uint16_t)f[5];

            if (g_settings.boot_mode > 2u)     /* 0=不画 1=图片 2=自检 */
                g_settings.boot_mode = 0u;
            if (g_settings.standby_mode > 4u)
                g_settings.standby_mode = 1u;
            if (g_settings.standby_count == 0u || g_settings.standby_count > 8u)
                g_settings.standby_count = 1u;
            if (g_settings.dwell == 0u)
                g_settings.dwell = 3u;
            settings_save(&g_settings);
            if (g_settings.boot_mode == 2u) {
                /* 选中“自检”就当场把上一次的自检结果画出来，不用拔电才看得到 */
                uint8_t rep = g_boot_report;
                uint8_t passed = (uint8_t)(((rep & 0x01u) ? 1 : 0) +
                                           ((rep & 0x02u) ? 1 : 0) +
                                           ((rep & 0x04u) ? 1 : 0) +
                                           ((rep & 0x08u) ? 1 : 0) +
                                           ((rep & 0x10u) ? 1 : 0));

                boot_screen_begin();
                boot_screen_row(0u, "FLASH", (uint8_t)((rep & 0x01u) ? 1u : 2u));
                boot_screen_row(1u, "TOUCH", (uint8_t)((rep & 0x02u) ? 1u : 2u));
                boot_screen_row(2u, "LCD",   (uint8_t)((rep & 0x04u) ? 1u : 2u));
                boot_screen_row(3u, "USB",   (uint8_t)((rep & 0x08u) ? 1u : 2u));
                boot_screen_row(4u, "CFG",   (uint8_t)((rep & 0x10u) ? 1u : 2u));
                boot_screen_done(passed, 5u);
                blink_first = 1u;
            } else if (standby_mode) {
                blink_first = 1u;
                standby_draw();
            }
            r[0] = 0x0Au; r[1] = 0x06u; r[2] = 0x06u;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        return;
    }

    if (cls == 0x08u) {
        uint8_t reply[6];
        uint16_t v = adc_read(f[1]);                            /* ADC 尚未接入 */
        reply[0] = 0x08;
        reply[1] = f[1];
        reply[2] = 0;
        reply[3] = 0;
        reply[4] = (uint8_t)(v >> 8);
        reply[5] = (uint8_t)v;
        usb_write(reply, 6);
        return;
    }
}


/* --- 独立看门狗：主循环卡死时自动复位，不用再拔插 USB --- */
static void iwdg_start(void)
{
    *(volatile uint32_t *)0x40003000u = 0x5555u;   /* 解锁 PR/RL */
    *(volatile uint32_t *)0x40003004u = 0x0003u;   /* LSI/16 ≈ 1.6 s */
    *(volatile uint32_t *)0x40003008u = 0x0FFFu;
    *(volatile uint32_t *)0x40003000u = 0xCCCCu;   /* 启动 */
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;   /* 喂一次 */
}

static void iwdg_feed(void)
{
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
}


int main(void)
{
    int rx_len = 0;

    trace_boot();
    TRACE_STAGE(1u);

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);

    /* 上次点过“一键刷机”：镜像已在 NOR 里，这里把它写进内部 flash 并重启。
     * update_apply() 跑在 RAM 里；体检不过就返回，设备继续用老固件，不会变砖。 */
    /* v76: 自动搬运已停用 —— NOR 里可能留着上一版失败的镜像，
       上电时先别碰内部 flash，等主机显式下发命令再搬。 */
    /* 搬运交给 0x0000 的常驻引导 */
    if (0) {
        (void)update_apply();
        update_clear_apply_flag();
    }

    {
        /* 开机自检：结果既画在屏上（boot_mode==2），也存进 g_boot_report 供上位机查 */
        uint8_t rep = 0u;
        uint8_t id[3];
        uint16_t touch;
        int cfg_ok;
        uint8_t passed;

        cfg_ok = settings_load(&g_settings);

        adc_init();      /* 先把 ADC 弄好，免得刚枚举完就被阻住 */
        usb_init();
        TRACE_STAGE(2u);

        flash_read_id(id);
        if (id[0] == 0x85u && id[1] == 0x60u && id[2] == 0x14u)
            rep |= 0x01u;
        (void)adc_read(9u);                  /* 第一拍只是启动转换 */
        delay_ms(2);
        touch = adc_read(9u);
        if (touch > 100u && touch < 4200u)
            rep |= 0x02u;
        rep |= 0x04u;                        /* LCD：能走到这里说明初始化没卡住 */
        rep |= 0x08u;                        /* USB：端点已配好，没有卡死 */
        if (cfg_ok)
            rep |= 0x10u;
        g_boot_report = rep;

        passed = (uint8_t)(((rep & 0x01u) ? 1 : 0) + ((rep & 0x02u) ? 1 : 0) +
                           ((rep & 0x04u) ? 1 : 0) + ((rep & 0x08u) ? 1 : 0) +
                           ((rep & 0x10u) ? 1 : 0));

        if (g_settings.boot_mode == 2u) {
            /* 自检画面固定停留 1.5 秒：太短看不清，而且这个模式不需要用户配秒数 */
            uint32_t ms = 1500u;

            boot_screen_begin();
            boot_screen_row(0u, "FLASH", (uint8_t)((rep & 0x01u) ? 1u : 2u));
            boot_screen_row(1u, "TOUCH", (uint8_t)((rep & 0x02u) ? 1u : 2u));
            boot_screen_row(2u, "LCD",   (uint8_t)((rep & 0x04u) ? 1u : 2u));
            boot_screen_row(3u, "USB",   (uint8_t)((rep & 0x08u) ? 1u : 2u));
            boot_screen_row(4u, "CFG",   (uint8_t)((rep & 0x10u) ? 1u : 2u));
            boot_screen_done(passed, 5u);
            while (ms > 0u) {
                iwdg_feed();
                delay_ms(1);
                ms--;
            }
            boot_shown = 0u;
            standby_draw();
        } else if (g_settings.boot_mode == 1u && asset_draw(ASSET_BOOT, 0u)) {
            standby_mode = 0u;            /* 先停在开机画面上，按 boot_hold 秒数再进待机 */
            boot_shown = 1u;
        } else {
            standby_draw();
        }
        blink_first = 0u;
        TRACE_STAGE(3u);
    }

    /* SysTick 自检：10 ms 应当约 480 000 个计数 */
    {
        uint32_t a, b, d;
        uint8_t c[6];
        systick_init();
        a = systick_now();
        delay_ms(10);
        b = systick_now();
        d = (b - a) / 48000u;      /* 折算成毫秒，应当接近 10 */
        c[0] = 0x77u;
        c[1] = (uint8_t)(d & 0xFFu);
        c[2] = (uint8_t)((d >> 8) & 0xFFu);
        c[3] = 0;
        c[4] = 0;
        c[5] = 0xAAu;
        usb_write(c, 6);
    }

    iwdg_start();

    for (;;) {
        iwdg_feed();
        TRACE_STAGE(4u);
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;
        static uint32_t stall;

        while (rx_len < (int)sizeof(rx)) {
            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
            if (n <= 0)
                break;
            rx_len += n;
            quiet = 0u;
            hello_tick = 0u;
            stall = 0u;
            idle_ms = 0u;
        }

        /* 原始像素流中途断掉时放弃本段：否则剩下的字会被当成像素，
         * 之后的帧头全部错位，表现为画面冻结且切插件也无法恢复。 */
        if (raw_left > 0u && rx_len == 0) {
            if (++stall > 4000u) {
                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;
                lcd_raw_idle();
                stall = 0u;
            }
        }

        /* 上位机断开（或一直没发帧）超过 4 秒，就回到待机画面，
         * 免得屏幕永远停在最后一帧上。计时用 delay_ms(1) 累加，
         * 空闲时主循环本来也没事可做，阻塞 1 ms 不影响任何东西。 */
        if (rx_len == 0 && raw_left == 0u) {
            if (idle_ms < 61000u)
                idle_ms++;
        } else {
            idle_ms = 0u;
        }

        if (standby_mode == 0u
            && idle_ms >= (boot_shown ? ((uint32_t)g_settings.boot_hold * 1000u) : 4000u)) {
            uint32_t k;

            standby_mode = 1u;
            blink_first = 1u;
            blink_on = 1u;
            stage_len = 0u;
            stage_idx = 0u;
            rle_n = 0u;
            frame_px = 0u;
            lcd_raw_idle();
            standby_draw();
            for (k = 0u; k < 6u; k++)
                iwdg_feed(), delay_ms(1);
        }

        /* 待机画面：正中 8x8 方块，白/黄每秒交替一次。主机一开流就停，
         * 免得它跟画面抢窗口。
         *
         * 计时不用 SysTick（它的计数方向/分频都不直观，之前算出过纯白），
         * 而是老老实实数 1000 个 delay_ms(1)：delay_ms 是实测标定的
         * “3000 次空循环约 1 ms”，一秒很稳。等待期间照样喂狗、照样收字节，
         * 一有数据立刻结束等待回去处理协议，不会漏包。 */
        if (standby_mode) {
            uint32_t i;

            if (blink_first) {
                blink_first = 0u;
                standby_draw();
            } else {
                uint32_t ticks = 600u;

                /* 图片轮播按设置的“每张停留秒数”，方块轮播固定 1 秒 */
                if (g_settings.standby_mode == 2u)
                    ticks = (uint32_t)g_settings.dwell * 1000u;
                else if (g_settings.standby_mode == 3u)
                    ticks = 20u;        /* 弹球：约 20 ms 一步，50 步/秒 */

                for (i = 0u; i < ticks; i++) {
                    iwdg_feed();
                    n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
                    if (n > 0) {
                        rx_len += n;
                        quiet = 0u;
                        hello_tick = 0u;
                        stall = 0u;
                        break;
                    }
                    /* 待机时主循环几乎不转，打招呼的快计数器涨不起来，
                     * 主机就会“找不到设备”。这里每约 100 拍补一次 hello。 */
                    if ((i % 50u) == 0u)
                        usb_write(hello, 6);
                    delay_ms(1);
                }
                if (rx_len == 0)
                    standby_tick();
            }
        }

        /* 资产写到一半没下文（主机崩了/线松了）：数空转圈数后作废，别把设备卡成哑巴。
         * 这里不能用 SysTick：这颗片的 SysTick 计数方向/分频都不直观，之前用它
         * 算时间出过“纯白方块”和“看门狗立刻触发”的巧合事故。 */
        if (asset_rx) {
            TRACE_STAGE(6u);
            if (rx_len == 0) {
                if (++asset_idle > 4000000u) {
                    asset_rx = 0u;
                    asset_rx_left = 0u;
                    asset_win = 0u;
                    asset_idle = 0u;
                    asset_abort();
                }
            } else {
                asset_idle = 0u;
            }
        } else {
            asset_idle = 0u;
        }

        if (up_rx_on) {               /* half-open window: give up after a while */
            if (rx_len == 0) {
                if (++up_idle > 4000000u) {
                    up_rx_on = 0u;
                    up_rx_left = 0u;
                    up_idle = 0u;
                }
            } else {
                up_idle = 0u;
            }
        } else {
            up_idle = 0u;
        }

        if (rx_len > 0) {
            int pos = 0;

            while (pos < rx_len) {
                if (stream_bytes > 40000u) {
                    /* 声明长度和实际字节数已经对不上：作废这一段，并丢掉本批剩余字节，
                     * 免得把后面的字节当像素或当命令误读。下一轮从干净状态重来。 */
                    raw_left = 0u;
                    stage_len = 0u;
                    rle_n = 0u;
                    sig_rx = 0u;
                    sig_left = 0u;
                    sig_pos = 0u;
                    asset_rx = 0u;
                    asset_rx_left = 0u;
                    asset_win = 0u;
                    stream_bytes = 0u;
                    lcd_raw_idle();
                    rx_len = 0;
                    pos = 0;
                    break;
                }

                if (up_rx_on) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint8_t ack[6];

                    if (take > (uint32_t)up_rx_left)
                        take = (uint32_t)up_rx_left;
                    update_stage_feed(rx + pos, (int)take);
                    pos += (int)take;
                    up_rx_left = (uint16_t)(up_rx_left - (uint16_t)take);
                    stream_bytes += take;
                    if (up_rx_left == 0u) {
                        up_rx_on = 0u;
                        ack[0] = 0x0Au; ack[1] = 0x12u; ack[2] = 0x06u;
                        ack[3] = 0u; ack[4] = 0u; ack[5] = 0u;
                        usb_write(ack, 6);
                    }
                    continue;
                }

                if (asset_rx) {
                    uint32_t take = (uint32_t)(rx_len - pos);

                    if (take > asset_rx_left)
                        take = asset_rx_left;
                    asset_feed(rx + pos, (int)take);
                    pos += (int)take;
                    asset_rx_left -= take;
                    stream_bytes += take;
                    asset_last_tick = systick_now();
                    if (asset_rx_left == 0u) {
                        asset_rx = 0u;
                        if (asset_win) {
                            static const uint8_t wack[6] = {0x0Au, 0x08u, 0x06u, 0u, 0u, 0u};
                            asset_win = 0u;
                            usb_write(wack, 6);
                        }
                    }
                    stall = 0u;
                    continue;
                }

                if (sig_rx) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t i;

                    if (take > (uint32_t)sig_left)
                        take = (uint32_t)sig_left;
                    for (i = 0; i < take; i++)
                        sig_buf[sig_pos + (uint8_t)i] = rx[pos + (int)i];
                    sig_pos = (uint8_t)(sig_pos + (uint8_t)take);
                    sig_left = (uint16_t)(sig_left - (uint16_t)take);
                    stream_bytes += take;
                    pos += (int)take;
                    stall = 0u;
                    if (sig_left == 0u) {
                        uint8_t rep[10];
                        uint8_t bit;

                        sig_rx = 0u;
                        hb_hold = 400u;      /* 回包期间先别发心跳，EP3 只有一笔在途缓冲 */
                        rep[0] = 0x0Au;
                        rep[1] = 0x11u;
                        rep[2] = 0u;
                        rep[3] = 0u;
                        rep[4] = 0u;
                        rep[5] = 0u;
                        rep[6] = 0u;
                        rep[7] = 0u;
                        rep[8] = 0u;
                        rep[9] = 0xAAu;
                        for (bit = 0u; bit < (uint8_t)TILE_N; bit++) {
                            uint16_t want = (uint16_t)(sig_buf[bit * 2u] |
                                                       ((uint16_t)sig_buf[bit * 2u + 1u] << 8));
                            uint16_t got = (uint16_t)(tile_sum[bit] ^ tile_xor[bit]);

                            if (want != got) {
                                uint8_t idx = (uint8_t)(bit / 8u);
                                if (idx < 7u)
                                    rep[2u + idx] = (uint8_t)(rep[2u + idx] | (1u << (bit % 8u)));
                            }
                        }
                        usb_write(rep, 10);
                    }
                    continue;
                }

                if (raw_left > 0u) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;

                    if (rle_mode) {
                        for (i = 0; i < take; i++)
                            rle_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                        stream_bytes += take;
                        if (raw_left == 0u)
                            stage_flush();
                    } else {
                        uint32_t room = (uint32_t)sizeof(stage[0]) - stage_len;

                        if (take > room)
                            take = room;
                        for (i = 0; i < take; i++)
                            stage[stage_idx][stage_len + i] = rx[pos + (int)i];

                        stage_len += take;
                        pos += (int)take;
                        raw_left -= take;
                        stream_bytes += take;

                        if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u))
                            stage_flush();
                    }
                    stall = 0u;
                    continue;
                }

                if (rx_len - pos >= 6) {
                    handle_frame(rx + pos);
                    pos += 6;
                    continue;
                }
                break;
            }

            if (pos > 0) {
                int k;
                for (k = pos; k < rx_len; k++)
                    rx[k - pos] = rx[k];
                rx_len -= pos;
            }
        }

        /* 串口安静一段时间就周期性打招呼：主机的握手只在这时能被满足，
         * 一旦有数据进来立刻停，避免握手包混进图像流。 */
        usb_pump();
        if (ver_pending && usb_write(&ver_val, 1u))
            ver_pending = 0u;

        if (hb_hold > 0u) {
            hb_hold--;
        } else if (++quiet >= 120000u) {
            if (hello_tick == 0u)
                usb_write(hello, 6);
            if (++hello_tick >= 15000u)
                hello_tick = 0u;
        }

        /* 空闲时按 1 ms 一拍走，给 idle_ms 一个真实的时间基准；
         * 有数据时保持全速，不拖慢收流。 */
        if (rx_len == 0 && raw_left == 0u)
            delay_ms(1);
    }
}
