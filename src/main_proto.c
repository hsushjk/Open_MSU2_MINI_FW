/* 协议固件（-Variant proto）
 *
 * 6 字节帧：[class, sub, b2, b3, b4, b5]
 *   class 0  sub 0x4D         握手（设备发 OPMSU，主机回 OKMSU）
 *   class 2  sub 0            x0, y0
 *   class 2  sub 1            x1, y1（终点，不是宽高）
 *   class 2  sub 3  op 0      纯色填充   colour = b3:b4
 *                   op 7      开始流式   set_window(x0,y0,x1-1,y1-1) + CS 拉低
 *                   op 8      推一段     len = b3:b4（满段 0x0100）
 *                   op 9      结束
 *                   op 0x0A   旋转（暂不处理）
 *   class 2  sub 4            段缓冲填充：4 字节循环铺满 256 字节
 *   class 4                   段缓冲写入：seg[b1*4 ..] = b2..b5
 *   class 8                   读 ADC：回 [0x08, ch, 0, 0, hi, lo]
 *
 * 未实现 class 2 sub 2（页号）：收到该帧一律忽略。 */
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
static uint8_t  up_committed;                 /* 本次上电已提交镜像（0A 13 回 06） */
static uint16_t up_rx_left;
static uint32_t up_idle;                 /* iterations spent waiting for upload data */
static uint32_t stream_bytes;

static uint8_t standby_mode = 1u;
static uint8_t boot_shown;    /* 屏幕上现在是开机画面：按 boot_hold 秒数停留 */
static uint8_t blink_first = 1u;      /* 待机画面还没画过 */
static uint8_t blink_on = 1u;         /* 当前方块颜色 1=白 0=黄 */
static uint32_t standby_wait;         /* 待机动画已攒的拍数（主循环每圈 +1） */
static uint32_t idle_ms;              /* 没收到任何字节的粗略毫秒数 */
static uint32_t win_px;   /* 当前流的窗口像素数，用于整帧角标判定 */

/* 待机动画节拍：主循环空闲时每圈末尾 delay_ms(1)，所以 1000 拍 ≈ 1 秒
 * （DELAY_MS_LOOPS 就是 delay_ms() 的 1 ms 空循环数）。方块翻色、换图
 * 都只累加这个拍数，不调用 delay_ms 长等待，USB 与命令处理一刻都不停。 */
#define STANDBY_BLINK_TICKS 1000u

/* OTA 上传窗口半开会话的作废阈值（单位：主循环圈数，不是毫秒也不是字节）。
 * 空闲时一圈 ≈ 1 ms，所以 400 圈 ≈ 0.4 秒：正常 1 KB 窗口之间的间隔只有
 * 几毫秒，够用；主机中途崩溃也能在几百毫秒内恢复。 */
#define UP_IDLE_LIMIT       400u

/* 半帧/残包自愈：6 字节定界只要错位 1..5 字节就永远补不齐，表现为“设备还在
 * 打招呼、但所有命令都不应”，只能拔插。完全空闲（没有 raw_left / 资产 / OTA /
 * 签名收流）时若手上还留着 1..5 字节，且已经 100 ms 没有新字节，就整段丢掉；
 * 下一帧从干净边界重新对齐。计时用 SysTick（48000 计数 ≈ 1 ms）。
 * 注意：收流中途绝不跑这个判断——半帧停顿 100 ms 在直连 USB 上不会出现。 */
#define RX_IDLE_FLUSH_MS    100u

/* 空闲时连续收到这么多个 0x00 一律当主机垫的对齐填充丢掉：6 字节帧最多
 * 5 个前导 0，而 class 0 sub 0 本身就是空帧，所以丢它一定安全。
 * 只在完全空闲时做，收流（像素/资产/OTA/签名）途中不动。 */
#define RX_PAD_RUN          8

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
 * 上位机一问（0A 0D）就知道"上次是死在哪一步"。
 *   1=main 开始  2=usb_init 完成  3=开机画面  4=主循环  5=收像素
 *   6=写资产    7=淡入淡出      8=整片擦除  9=待机动画
 *  10=OTA 搬运（update_apply；成功就复位，失败则返回让当前固件继续运行）
 *
 * 复位原因从 RCC->RSTSCKR 读（地址和位定义见下面），last_stage 是"本次启动前
 * 最后记录到的阶段码"，也就是上次复位时停在哪一步。 */
#define TRACE_MAGIC 0x4D534256u   /* 'MSBV' v2：结构改过，旧面包屑自动作废 */

typedef struct {
    uint32_t magic;
    uint32_t stage;       /* 当前阶段码 */
    uint32_t boots;       /* 累计启动次数 */
    uint32_t last_stage;  /* 上次复位前最后记录到的阶段码 */
    uint8_t  reset;       /* 本次启动的复位原因位图 */
} trace_t;

static volatile trace_t g_trace __attribute__((section(".noinit")));

#define TRACE (&g_trace)
#define TRACE_STAGE(s) (TRACE->stage = (uint32_t)(s))

/* RCC->RSTSCKR。地址取自 WCH ch32x035.h：RCC_BASE = 0x40021000，
 * RCC_TypeDef 依次是 CTLR / CFGR0 / RESERVED0 / APB2PRSTR / APB1PRSTR /
 * AHBPCENR / APB2PCENR / APB1PCENR / RESERVED1 / RSTSCKR / AHBRSTR，
 * 所以 RSTSCKR = 0x40021024。位定义同样取自该头文件（注意这颗片 bit25 是
 * OPARSTF，不是 STM32 的 OBLRSTF）。 */
#define RCC_RSTSCKR      (*(volatile uint32_t *)0x40021024u)
#define RSTSCKR_RMVF     0x01000000u
#define RSTSCKR_OPARSTF  0x02000000u
#define RSTSCKR_PINRSTF  0x04000000u
#define RSTSCKR_PORRSTF  0x08000000u
#define RSTSCKR_SFTRSTF  0x10000000u
#define RSTSCKR_IWDGRSTF 0x20000000u
#define RSTSCKR_WWDGRSTF 0x40000000u
#define RSTSCKR_LPWRRSTF 0x80000000u

/* 复位原因位图（0A 0D 回包第 6 字节）：
 *   bit0 上电/掉电  bit1 看门狗  bit2 软件复位  bit3 其它（OPA/PIN/WWDG/LPWR）
 * 必须在 iwdg_start() 之前调用，并且读一次就清 RMVF，否则下次复位会沿用这次的标志。 */
static uint8_t reset_cause_read(void)
{
    uint32_t v = RCC_RSTSCKR;
    uint8_t flags = 0u;

    if (v & RSTSCKR_PORRSTF)
        flags |= 0x01u;
    if (v & RSTSCKR_IWDGRSTF)
        flags |= 0x02u;
    if (v & RSTSCKR_SFTRSTF)
        flags |= 0x04u;
    if (v & (RSTSCKR_OPARSTF | RSTSCKR_PINRSTF | RSTSCKR_WWDGRSTF | RSTSCKR_LPWRRSTF))
        flags |= 0x08u;
    RCC_RSTSCKR = RSTSCKR_RMVF;
    return flags;
}

static void trace_boot(uint8_t reset_flags)
{
    uint32_t prev = 0u;

    if (TRACE->magic == TRACE_MAGIC)
        prev = TRACE->stage;      /* 上电 / 旧布局时 magic 对不上，prev 保持 0 */
    else {
        TRACE->magic = TRACE_MAGIC;
        TRACE->boots = 0u;
    }
    TRACE->stage = 0u;
    TRACE->last_stage = prev;
    TRACE->reset = reset_flags;
    TRACE->boots = TRACE->boots + 1u;
}


/* 待机动画里所有图形都是方形的：这颗屏分辨率低，圆画出来锯齿太明显，
 * 用户明确要求“用同大小的正方形”。 */
#define BALL_SZ   10          /* 球边长（像素） */
#define DOT_SZ     6          /* 拖尾点边长（像素） */
#define TRAIL_N   13          /* 拖尾点数 */
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

/* 轮播上限：standby_count 有效（1..STANDBY_SLOTS）时只在前 N 个槽里循环；
 * 协议与存档都钳过，这里按内存值处理：异常值退回全部槽重画。 */
static uint8_t standby_limit(void)
{
    if (g_settings.standby_count >= 1u && g_settings.standby_count <= STANDBY_SLOTS)
        return (uint8_t)g_settings.standby_count;
    return (uint8_t)STANDBY_SLOTS;
}

/* 从 from 的下一个开始，在限定的前 lim 个槽里找一个有图的槽；都没有返回 0xFF。
 * from 越界（张数改小后旧索引 >= lim，或 0xFF“从头找”）就从槽 0 起扫，
 * 即回落到第一个有效槽，不会停在越界槽上把画面留空。 */
static uint8_t next_valid_slot(uint8_t from)
{
    uint8_t k;
    uint8_t lim = standby_limit();
    uint8_t base = (from < lim) ? from : (uint8_t)(lim - 1u);   /* 0xFF/越界 = 从头找 */

    for (k = 1u; k <= lim; k++) {
        uint8_t idx = (uint8_t)((base + k) % lim);

        if (asset_valid(ASSET_STANDBY, idx))    /* 掩码用真实槽号：idx < lim <= STANDBY_SLOTS */
            return idx;
    }
    return 0xFFu;
}

/* ---------- 待机图片交叉淡入淡出（非阻塞状态机） ----------
 * 这块板上没有帧缓存，所以是"边读边混边推"：一条 2 行带子（640 字节）
 * 从 NOR 各读一份，按权重混合后整条推给屏，8 级权重用 >>3 完成。
 * 注意：读 flash 时必须让屏 CS 保持高（PA6 一脚两用），所以每条带子
 * 都是"先读、再开窗、再推、再抬 CS"，跟 asset_draw 一样。
 *
 * 淡入必须在主循环里分步推进，不能一次调用里同步跑完：8 档权重 × 40 条带子
 * 要两秒以上，全程阻塞会让主循环停摆，上位机这时推帧就会把接收环塞死、
 * 主机 write 超时，只能靠重连+握手恢复。所以拆成三件小事：
 *   standby_fade()      只校验 + 打开两条 lane + 记下目标，立即返回；
 *   standby_fade_step() 主循环每圈调用一次，只解 1 条带子（640 B = 2 行）；
 *   fade_step/fade_band 记进度，40 条带子换一档权重，8 档走完收尾。
 * 末档权重 (0, 8) 推上去的就是 to 的完整内容，不用再整幅补画一遍。
 * 一条带子约 3–10 ms，推完就回主循环，所以淡入全程任何时刻都能在 ~10 ms
 * 内处理下一条命令（USB 服务不会饿死）。
 *
 * 两条 lane 都走 assets.c 的 asset_fade_open/asset_fade_band：fmt=0（裸
 * RGB565）和 fmt=1（RLE）共用同一条带子路径——fmt=0 时 lane 里的 addr 正好
 * 是 asset_data_addr()，输出的就是裸 RGB565 数据；黑场（from = 0xFF）
 * 当一路虚拟帧（lane 里 fmt=2 填 0）。槽头格式只在 standby_fade() 里读一次。
 * 主循环那边：fade_active 还在就不再累计 standby_wait，轮播计时不会和淡入
 * 叠加；主机来帧/贴图把 standby_mode 清 0 时静默放弃剩下的带子。 */
#define FADE_STEPS 8u
#define FADE_BANDS 40u                  /* 80 行 / 每条带子 2 行 */

static uint8_t fade_a[640];
static uint8_t fade_b[640];
static uint8_t fade_active;             /* 1 = 淡入进行中，主循环每圈推一条带子 */
static uint8_t fade_step;               /* 0..FADE_STEPS-1：当前权重档 */
static uint8_t fade_band;               /* 0..FADE_BANDS-1：当前带子 */
static uint8_t fade_from;               /* 淡出端槽号，0xFF = 黑场 */
static uint8_t fade_to;                 /* 淡入端槽号 */

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

/* 交叉淡入的初始化：只做校验、打开两条 lane、记下目标，立刻返回。
 * 320 条带子交给主循环一圈一条慢慢推（standby_fade_step）。
 * from = 0xFF 表示从黑场淡入（lane 里就是一路全 0 的虚拟帧）。 */
static void standby_fade(uint8_t from, uint8_t to)
{
    uint8_t fmt_a;
    uint8_t fmt_b;

    fade_active = 0u;               /* 上一次没走完就又要切换：直接丢弃旧进度 */
    lcd_raw_idle();                 /* 下面要读槽头（PA6 一脚两用），先抬高屏 CS */
    if (!asset_valid(ASSET_STANDBY, to))
        return;
    fmt_a = (from == 0xFFu) ? 0u : asset_format(ASSET_STANDBY, from);
    fmt_b = asset_format(ASSET_STANDBY, to);
    if (fmt_a > 1u || fmt_b > 1u) {     /* 槽头不合法：别把压缩字节当像素读 */
        (void)asset_draw(ASSET_STANDBY, to);
        return;
    }
    fade_from = from;
    fade_to = to;
    fade_step = 0u;
    fade_band = 0u;
    asset_fade_open(0u, ASSET_STANDBY, from, (uint8_t)(from == 0xFFu));
    asset_fade_open(1u, ASSET_STANDBY, to, 0u);
    fade_active = 1u;
}
/* 握手/心跳包：设备发 \0OPMSU，主机回 \0OKMSU。待机和淡入时也periodically 补发。 */
static const uint8_t hello[6] = { 0x00, 0x4F, 0x50, 0x4D, 0x53, 0x55 };


/* 推一条带子（640 B，混色 + 推屏 ≈ 3–10 ms）就返回，主循环靠它把整段淡入
 * 摊到 320 圈里。带子读完一条就 lcd_raw_idle()，既满足"读 NOR 前屏 CS 必须
 * 高"，也让下一次主循环能先处理 USB。每推一条喂一次 IWDG。 */
static void standby_fade_step(void)
{
    uint32_t wa;
    uint32_t wb;
    int y;

    if (!fade_active)
        return;
    TRACE_STAGE(7u);                /* 看门狗复位后 0A 0D 能看出死在淡入里 */
    wa = (uint32_t)(FADE_STEPS - (fade_step + 1u));
    wb = (uint32_t)(fade_step + 1u);
    y = (int)fade_band * 2;
    if (!asset_fade_band(0u, fade_a) || !asset_fade_band(1u, fade_b)) {
        /* 槽数据坏了：已经推出去的半幅收不回来，整幅重画成 to */
        fade_active = 0u;
        (void)asset_draw(ASSET_STANDBY, fade_to);
        return;
    }
    fade_rows(fade_a, fade_b, wa, wb);
    lcd_stream_begin(0, y, 160, y + 2);
    lcd_raw_write(fade_a, 640);
    lcd_raw_idle();
    iwdg_feed();                    /* 单带 3–10 ms，远短于 IWDG 周期 */
    /* 淡入期间 standby_wait 不涨，主循环里那条 hello 就永远轮不到；
     * 主机恰好此时打开串口会等不到问候包。每 8 条带子补一次。 */
    if ((fade_band & 7u) == 0u)
        usb_write(hello, 6);

    if (++fade_band < FADE_BANDS)
        return;
    fade_band = 0u;
    if (++fade_step < FADE_STEPS) {
        /* 下一档权重又要从第 0 行解起：两条 lane 都回到槽头（老代码每步重开） */
        asset_fade_open(0u, ASSET_STANDBY, fade_from, (uint8_t)(fade_from == 0xFFu));
        asset_fade_open(1u, ASSET_STANDBY, fade_to, 0u);
        return;
    }
    fade_active = 0u;               /* 末档权重 (0,8)：屏上已经是 to 的整幅内容 */
}

/* 诊断命令（0A 10 / 0A 16）要"整段淡入跑完再回包"：上位机拿它们的往返耗时
 * 测淡入总时长，所以这里就地把剩下的带子推完（约 1–3 秒，中途喂狗）。
 * 主循环那条非阻塞路径不受影响，两条路径用的是同一个 standby_fade_step。 */
static void fade_run_sync(void)
{
    while (fade_active) {
        standby_fade_step();
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
        /* 0xFF 当作“从头找”；范围由 standby_limit() 限定在前 count 个槽 */
        uint8_t first = next_valid_slot(0xFFu);

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
        uint8_t lim = standby_limit();
        /* 张数改小后当前索引可能已经越界：当作“从头找”，回落到第一个有效槽 */
        uint8_t nx = next_valid_slot((img_idx < lim) ? img_idx : 0xFFu);

        if (nx == 0xFFu) {
            lcd_fill(0, 0, 160, 80, 0x0000);
            return;
        }
        {
            /* 淡出端仍用当前实际显示的那个槽（越界但有效的槽也要能读，
             * 否则会先黑一下再淡入） */
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

/* 行程编码：每三个字节一组 [像素数, 高字节, 低字节]，展开后进 stage。
 * 展开总量以本帧窗口像素数为上界：主机给的游程与窗口对不上时，超出的部分
 * 不再展开，只置失败标记，本帧裁决字节回 0x15，stage 不会被无限灌满。 */
static uint32_t rle_px;                   /* 本帧已展开的像素数 */
static uint8_t  rle_over;                 /* 本帧展开超界 */

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
        uint32_t limit = win_px ? win_px : 12800u;   /* 无窗口时按整屏算 */

        while (cnt-- > 0u) {
            if (rle_px >= limit) {
                rle_over = 1u;
                return;
            }
            if (stage_len + 2u > (uint32_t)sizeof(stage[0]))
                stage_flush();
            stage[stage_idx][stage_len++] = hi;
            stage[stage_idx][stage_len++] = lo;
            rle_px++;
        }
    }
}

static uint32_t systick_now(void)
{
    return *(volatile uint32_t *)0xE000F008u;
}

static void systick_init(void)
{
    *(volatile uint32_t *)0xE000F010u = 0x00FFFFFFu;   /* CMP */
    *(volatile uint32_t *)0xE000F000u = 0x00000009u;   /* STRE | STE */
}

static int x0, y0, x1, y1;
static int connected;

/* 握手串：\0OPMSU（见上方 hello 定义）。版本号不编进握手，
 * 只从 class 0 sub 0x56 的版本帧取。 */
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

/* ---------- class 0x08 的 ADC/触摸滤波 ----------
 * adc_read() 是“取上一拍已完成的采样、启动这一拍转换、立即返回”的非阻塞写法，
 * 所以这里一次命令只取一个采样、绝不等待（等待会让 USB 停摆）。
 * 历史保留 3 个，取中值去掉单点尖峰；再叠 8 LSB 滞回——手指没动时触摸通道
 * 基准值在 3900 上下抖，没有滞回的话每次都在 ±1 上跳。
 * 回包长度不变，仍是 6 字节 [08, ch, 0, 0, hi, lo]。 */
#define TOUCH_HIST  3u
#define TOUCH_HYST  8u

static uint16_t adc_filtered(uint8_t ch)
{
    static uint8_t  f_ch = 0xFFu;
    static uint16_t hist[TOUCH_HIST];
    static uint8_t  have;
    static uint16_t out;
    uint16_t v;
    uint16_t mn;
    uint16_t mx;
    uint16_t med;
    uint8_t i;

    v = adc_read(ch);
    if (ch != f_ch) {                 /* 换通道：历史作废，别把别的脚的电压算进来 */
        f_ch = ch;
        have = 0u;
        out = v;
    }
    if (have < TOUCH_HIST) {
        hist[have] = v;
        have++;
        out = v;
        return v;                     /* 头两拍还没有中值可算，先原样回 */
    }
    hist[0] = hist[1];
    hist[1] = hist[2];
    hist[2] = v;

    mn = hist[0];
    mx = hist[0];
    for (i = 1u; i < TOUCH_HIST; i++) {
        if (hist[i] < mn)
            mn = hist[i];
        if (hist[i] > mx)
            mx = hist[i];
    }
    med = (uint16_t)((uint32_t)hist[0] + hist[1] + hist[2] - mn - mx);

    if (med > out) {
        if ((uint16_t)(med - out) > TOUCH_HYST)
            out = med;
    } else if ((uint16_t)(out - med) > TOUCH_HYST) {
        out = med;
    }
    return out;
}

static void handle_frame(const uint8_t *f)
{
    uint8_t cls = f[0];
    uint8_t sub = f[1];
    uint16_t a = (uint16_t)((f[2] << 8) | f[3]);
    uint16_t b = (uint16_t)((f[4] << 8) | f[5]);

    if (cls == 0x00u) {
        if (sub == 0x4Fu && f[2] == 0x4Bu && f[3] == 0x4Du &&
            f[4] == 0x53u && f[5] == 0x55u) {   /* 主机回 \0OKMSU */
            connected = 1;
            usb_write(f, 6);
            return;
        }
        if (sub == 0x56u) {                       /* 版本查询：7 字节
                                                   * 56 MA MI PA TH TL AA */
            uint8_t st[7];
            uint16_t tv = adc_read(9u);
            st[0] = 0x56u;
            st[1] = FW_VER_MAJOR;
            st[2] = FW_VER_MINOR;
            st[3] = FW_VER_PATCH;
            st[4] = (uint8_t)((tv >> 8) & 0xFFu);
            st[5] = (uint8_t)(tv & 0xFFu);
            st[6] = 0xAAu;
            usb_write(st, 7);
            return;
        }
        return;
    }

    if (cls == 0x02u) {
        switch (sub) {
        case 0x00:                            /* 起点：收到就钳进屏幕 */
            x0 = (a < 160u) ? (int)a : 159;
            y0 = (b < 80u) ? (int)b : 79;
            return;
        case 0x01:                            /* 终点（不含） */
            x1 = (a < 160u) ? (int)a : 160;
            y1 = (b < 80u) ? (int)b : 80;
            return;
        case 0x03:
            switch (f[2]) {
            case 0x00:                        /* 纯色填充 */
            case 0x04:                        /* 与 0x00 同义的旧写法，一并接受 */
                lcd_fill(x0, y0, x1 - x0, y1 - y0, (uint16_t)((f[3] << 8) | f[4]));
                return;
            case 0x07: {                      /* 开始流式 */
                uint8_t bad[6] = {0x02u, 0x03u, 0x15u, 0u, 0u, 0u};

                if (x1 <= x0 || y1 <= y0) {   /* 空窗口/反向窗口：不开始，直接报错 */
                    usb_write(bad, 6);
                    return;
                }
                stream_bytes = 0u;
                standby_mode = 0u;
                boot_shown = 0u;
                rle_px = 0u;
                rle_over = 0u;
                win_px = (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0);
                lcd_stream_begin(x0, y0, x1, y1);
                tile_x0 = x0;
                tile_y0 = y0;
                tile_x = x0;
                tile_y = y0;
                tile_w = x1 - x0;
                tile_half = 0u;
                tile_reset_region(x0, y0, x1, y1);
                return;
            }
            case 0x08: {                      /* 推一段：长度封顶到段缓冲大小 */
                uint32_t n = (uint32_t)((f[3] << 8) | f[4]);

                if (n > sizeof seg)
                    n = sizeof seg;
                stream_write(seg, n);
                return;
            }
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
                win_px = 0u;
                frame_px = 0u;
                /* 每帧一个裁决字节：0x06 = 像素数对得上，0x15 = 少收/超收。
                 * 主机读它就能“发一帧等一次”，天然限速，不会把接收环挤爆。 */
                ver_val = (uint8_t)((rle_over != 0u || win_px_before == 0u ||
                                     frame_px_before != win_px_before)
                                    ? 0x15u : 0x06u);
                rle_over = 0u;
                rle_px = 0u;
                ver_pending = 1u;
                return;
            }
            default:
                return;
            }
        case 0x04:                            /* 段缓冲填充：先等上一段 DMA 读完 */
            lcd_stream_wait();
            seg_fill4(&f[2]);
            return;
        default:
            return;
        }
    }

    if (cls == 0x04u) {                       /* 段缓冲写入：先等上一段 DMA 读完 */
        uint32_t base = (uint32_t)f[1] * 4u;

        lcd_stream_wait();
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
        uint8_t ok;

        lcd_raw_idle();                       /* 后面要读 NOR，先把屏流收尾 */
        ok = (uint8_t)(update_stage_commit(crc) ? 0x06u : 0x15u);

        if (ok == 0x06u) { up_committed = 1u; } else { up_committed = 0u; }

        up_rx_on = 0u;
        rr[0] = 0x13u; rr[1] = 0x00u; rr[2] = ok;
        rr[3] = 0u; rr[4] = 0u; rr[5] = 0u;
        usb_write(rr, 6);
        return;
    }
    if (cls == 0x14u) {                       /* 自更新：就地搬运并复位（摊运在 RAM） */
        uint8_t rr[6];

        lcd_raw_idle();                       /* PA6 要当 NOR MISO 用，先关屏流 */
        /* 危险命令：会擦内部 flash。必须同时满足
           ① 帧里带确认令牌 A5 5A；
           ② 本次上电先成功提交过镜像（0A 13 回 06）；
           ③ 当前不在收流途中（raw_left/up_rx/资产/签名都要空）。
           这样即使流错位、像素字节被误当命令，也不可能凑出一次擦写。 */
        if (f[2] != 0xA5u || f[3] != 0x5Au || up_committed == 0u ||
            raw_left > 0u || up_rx_on != 0u || asset_rx != 0u || sig_rx != 0u) {
            rr[0] = 0x14u; rr[1] = 0x00u; rr[2] = 0x15u;
            rr[3] = 0xFEu; rr[4] = 0u; rr[5] = 0u;
            usb_write(rr, 6);
            return;
        }
        up_committed = 0u;

        update_set_apply_flag();
        rr[0] = 0x14u; rr[1] = 0x00u; rr[2] = 0x06u;
        rr[3] = 0u; rr[4] = 0u; rr[5] = 0u;
        usb_write(rr, 6);
        delay_ms(80);
        /* 先把屏流收尾、抬高屏 CS：update_apply() 里的 RAM 版 NOR 读是软件
           位翻转 SPI，屏 CS 还低着的话 NOR 时钟会被屏当像素数据吃掉。 */
        lcd_raw_idle();
        /* update_apply() 全身在 RAM：先整段重算 CRC 比对镜像头（对不上就返回 5，
           内部 flash 一个字节没动），再体检 0xF000 页；都过了才关中断、擦应用区、
           逐页搬、再复位。 */
        TRACE_STAGE(10u);             /* OTA 搬运：搬一半挂了也能从面包屑看出来 */
        (void)update_apply();
        /* 能走到这里说明搬运失败（否则已经复位） */
        update_clear_apply_flag();
        rr[0] = 0x14u; rr[1] = 0x00u; rr[2] = 0x15u;
        rr[3] = g_up_result; rr[4] = 0u; rr[5] = 0u;
        usb_write(rr, 6);
        return;
    }

    if (cls == 0x0Au) {                       /* 资产：开机/待机图 + 设置 */
        uint8_t r[14];

        /* 0A 里的命令可能读 NOR 或直接刷屏：先结束正在进行的屏流（PA6 复用） */
        lcd_raw_idle();

        if (sub == 0x00u || sub == 0x07u) {    /* 查询（14 字节，16 个待机槽） */
            uint16_t mask = 0u;
            uint8_t k;

            r[0] = 0x0Au;
            r[1] = sub;
            r[2] = (uint8_t)(asset_valid(ASSET_BOOT, 0u) ? 1u : 0u);
            for (k = 0u; k < STANDBY_SLOTS; k++)
                if (asset_valid(ASSET_STANDBY, k))
                    mask = (uint16_t)(mask | (uint16_t)(1u << k));
            r[3] = (uint8_t)(mask & 0xFFu);
            r[4] = (uint8_t)((mask >> 8) & 0xFFu);
            r[5] = g_settings.boot_mode;
            r[6] = g_settings.standby_mode;
            r[7] = g_settings.standby_count;
            r[8] = (uint8_t)((g_settings.dwell >> 8) & 0xFFu);
            r[9] = (uint8_t)(g_settings.dwell & 0xFFu);
            r[10] = FW_VER_MAJOR;             /* 老字段：只报主版本，完整三段见 0x56 查询 */
            r[11] = g_settings.boot_hold;
            r[12] = 0u;
            r[13] = 0xAAu;
            usb_write(r, 14);
            return;
        }
        if (sub == 0x0Bu) {                    /* 读开机自检结果 */
            r[0] = 0x0Au; r[1] = 0x0Bu;
            r[2] = g_boot_report;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x18u) {                    /* 把槽 N 的图画到 (x, y)：
                                                * 帧 [0A,18,kind,idx,x,y]，x/y 是有符号 int8，
                                                * 允许负偏移/裁剪；回 [0A,18,06/15,0,0,0] */
            int bx = (int)(int8_t)f[4];
            int by = (int)(int8_t)f[5];
            uint8_t ok;

            lcd_raw_idle();               /* 先抬屏 CS：asset_blit 里要读 NOR */
            standby_mode = 0u;            /* 画上去的图别被待机动画一秒后盖掉 */
            blink_first = 0u;
            ok = (uint8_t)(asset_blit(f[2], f[3], bx, by) ? 0x06u : 0x15u);
            r[0] = 0x0Au; r[1] = 0x18u; r[2] = ok;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x12u) {                    /* 免拆自更新：begin（01）/ 一窗数据（02） */
            uint8_t ok;

            if (f[2] == 0x01u) {
                uint32_t len = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

                stream_bytes = 0u;      /* 上传不受像素流预算限制 */
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

                /* 没成功 begin、或窗口长度为 0/超限，全部丢弃并回错。绝不能拿
                 * 静态的 stage 地址当前值继续写：那会把数据写进 NOR 的开机图。 */
                if (!update_stage_ready() || wl == 0u || wl > 2048u) {
                    r[0] = 0x0Au; r[1] = 0x12u; r[2] = 0x15u;
                    r[3] = 0u; r[4] = 0u; r[5] = 0u;
                    usb_write(r, 6);
                    return;
                }
                stream_bytes = 0u;      /* 每窗重新计预算，>40 KB 的镜像也不会被截断 */
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
        if (sub == 0x01u) {                    /* 开始写：kind(bit7=fmt) idx len_hi len_lo */
            uint8_t kf = f[2];
            uint32_t len = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

            lcd_raw_idle();
            standby_mode = 0u;
            /* kind 最高位携带数据格式：0 = 裸 RGB565（默认），
             * 1 = RLE。低 7 位还是槽类型，所以旧上位机发 0/1 完全不受影响。 */
            asset_begin((uint8_t)(kf & 0x7Fu), f[3], len,
                        (uint8_t)((kf >> 7) & 1u));
            asset_rx = 0u;              /* 分窗模式：数据由 0A 08 一段一段喂 */
            asset_rx_left = 0u;
            asset_win = 0u;
            asset_last_tick = systick_now();
            r[0] = 0x0Au; r[1] = 0x01u;
            /* 分窗模式下 asset_rx 恒为 0，判断“正在写”只能问 asset_writing()。 */
            r[2] = (uint8_t)(asset_writing() ? 0x06u : 0x15u);
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x08u) {                    /* 送来一段数据：len_hi len_lo，收满回 ack */
            uint32_t wl = ((uint32_t)f[4] << 8) | (uint32_t)f[5];

            /* 长度为 0 或超限：不猜、不钳位，直接回错让主机重发 */
            if (wl == 0u || wl > 4096u) {
                r[0] = 0x0Au; r[1] = 0x08u; r[2] = 0x15u;
                r[3] = 0u; r[4] = 0u; r[5] = 0u;
                usb_write(r, 6);
                return;
            }
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
            r[0] = 0x0Au;
            r[1] = 0x09u;      /* 帧头必须与请求的 sub 一致，否则主机会一直等到超时 */
            r[2] = 0x06u;
            r[3] = secs;
            r[4] = 0u;
            r[5] = 0u;
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
        if (sub == 0x0Du) {                    /* 复位面包屑：上次停在哪一步 + 复位原因
                                        * [0A,0D,stage,boots_lo,boots_hi,reset_flags,last_stage,AA] */
            uint32_t boots = TRACE->boots;

            r[0] = 0x0Au; r[1] = 0x0Du;
            r[2] = (uint8_t)(TRACE->stage & 0xFu);
            r[3] = (uint8_t)(boots & 0xFFu);
            r[4] = (uint8_t)((boots >> 8) & 0xFFu);
            r[5] = TRACE->reset;
            r[6] = (uint8_t)(TRACE->last_stage & 0xFu);
            r[7] = 0xAAu;
            usb_write(r, 8);
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
                fade_run_sync();          /* 诊断路径：整段淡入跑完再回包 */
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
            uint8_t ok;

            lcd_raw_idle();                    /* 擦除期间 PA6 要留给 NOR */
            /* 与 0A 14 同一套保护：令牌 + 空闲态，避免流错位时凑出一次整片擦除 */
            if (f[2] != 0xA5u || f[3] != 0x5Au ||
                raw_left > 0u || up_rx_on != 0u || asset_rx != 0u || sig_rx != 0u) {
                r[0] = 0x0Au; r[1] = 0x0Au; r[2] = 0x15u;
                r[3] = 0xFEu; r[4] = 0u; r[5] = 0u;
                usb_write(r, 6);
                return;
            }
            TRACE_STAGE(8u);
            ok = (uint8_t)(flash_erase_chip_now() ? 0x06u : 0x15u);
            if (ok == 0x06u) {
                asset_invalidate();            /* 缓存作废，否则仍以为槽里有图 */
                settings_default(&g_settings); /* 只改内存；NOR 留空，下次上电用默认 */
            }
            r[0] = 0x0Au; r[1] = 0x0Au; r[2] = ok;
            r[3] = 0u; r[4] = 0u; r[5] = 0u;
            usb_write(r, 6);
            return;
        }
        if (sub == 0x06u) {                    /* 存设置：boot standby count dwell */
            lcd_raw_idle();                    /* 下面可能要画自检屏/待机图 */
            g_settings.boot_mode = f[2];
            g_settings.standby_mode = f[3];
            g_settings.standby_count = f[4];
            g_settings.dwell = (uint16_t)f[5];

            if (g_settings.boot_mode > 2u)     /* 0=不画 1=图片 2=自检 */
                g_settings.boot_mode = 0u;
            if (g_settings.standby_mode > 4u)
                g_settings.standby_mode = 1u;
            /* count 限定轮播只在前 N 个槽循环：钳到 1..STANDBY_SLOTS（0 → 1，>16 → 16） */
            if (g_settings.standby_count == 0u)
                g_settings.standby_count = 1u;
            else if (g_settings.standby_count > STANDBY_SLOTS)
                g_settings.standby_count = (uint8_t)STANDBY_SLOTS;
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
                boot_screen_row(0u, (uint8_t)((rep & 0x01u) ? 1u : 2u));
                boot_screen_row(1u, (uint8_t)((rep & 0x02u) ? 1u : 2u));
                boot_screen_row(2u, (uint8_t)((rep & 0x04u) ? 1u : 2u));
                boot_screen_row(3u, (uint8_t)((rep & 0x08u) ? 1u : 2u));
                boot_screen_row(4u, (uint8_t)((rep & 0x10u) ? 1u : 2u));
                boot_screen_done(passed, 5u);
                blink_first = 1u;
            } else if (g_settings.standby_mode) {
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
        uint16_t v = adc_filtered(f[1]);        /* 3 取中值 + 8 LSB 滞回，见上面说明 */
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
    uint32_t rx_last_tick;      /* 上次收到字节的 SysTick 时刻（判半帧残留） */
    uint8_t  rx_reset_seen;     /* 已经处理到的 USB 复位计数 */
    uint8_t rst = reset_cause_read();   /* 必须在 iwdg_start() 之前、且只读一次 */

    trace_boot(rst);
    TRACE_STAGE(1u);
    g_up_result = 0u;           /* .noinit 跨复位保留，上电先把它归零 */

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    /* 看门狗尽早起来：开机画面/资产/自检任何一步卡住都能自己复位，
     * 而不是停在原地变成"USB 能枚举、但永远不说话"的哑巴。 */
    iwdg_start();
    iwdg_feed();

    /* USB 先起、上电立刻打招呼：即使后面屏幕初始化或画面卡住，上位机也能
     * 连上并读到版本与面包屑，而不是整机"能枚举、一个字节都不说"。 */
    adc_init();
    usb_init();
    usb_write(hello, 6);
    usb_write(hello, 6);
    TRACE_STAGE(2u);

    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    iwdg_feed();

    {
        /* 开机自检：结果既画在屏上（boot_mode==2），也存进 g_boot_report 供上位机查 */
        uint8_t rep = 0u;
        uint8_t id[3];
        uint16_t touch;
        int cfg_ok;
        uint8_t passed;

        cfg_ok = settings_load(&g_settings);

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
            boot_screen_row(0u, (uint8_t)((rep & 0x01u) ? 1u : 2u));
            boot_screen_row(1u, (uint8_t)((rep & 0x02u) ? 1u : 2u));
            boot_screen_row(2u, (uint8_t)((rep & 0x04u) ? 1u : 2u));
            boot_screen_row(3u, (uint8_t)((rep & 0x08u) ? 1u : 2u));
            boot_screen_row(4u, (uint8_t)((rep & 0x10u) ? 1u : 2u));
            boot_screen_done(passed, 5u);
            while (ms > 0u) {
                iwdg_feed();
                if ((ms % 200u) == 0u)
                    usb_write(hello, 6);     /* 画自检画面期间也别让上位机干等 */
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
        rx_last_tick = systick_now();
        rx_reset_seen = (uint8_t)usb_reset_count;
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

    for (;;) {
        iwdg_feed();
        TRACE_STAGE(4u);
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;
        static uint32_t stall;

        /* 主机重新打开串口会触发 USB 复位：把上一个会话的残包清掉。
         * 关端口时可能有一半命令还留在设备里，不清掉的话 6 字节定界就永久
         * 错位，后续命令全部对不上。复位后主机必定重新握手，丢掉是安全的。 */
        if (usb_reset_count != rx_reset_seen) {
            rx_reset_seen = (uint8_t)usb_reset_count;
            rx_len = 0;
            rx_last_tick = systick_now();
        }

        while (rx_len < (int)sizeof(rx)) {
            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
            if (n <= 0)
                break;
            rx_len += n;
            rx_last_tick = systick_now();
            quiet = 0u;
            hello_tick = 0u;
            stall = 0u;
            idle_ms = 0u;
        }

        /* 原始像素流中途断掉时放弃本段：否则剩下的字会被当成像素，
         * 之后的帧头全部错位，表现为画面冻结且切插件也无法恢复。 */
        if (raw_left > 0u && rx_len == 0) {
            /* 400000 圈：主机按 2 KB 分块限速发送，块间可能有十几毫秒空档，
             * 阈值太小会把正常间隔误判成断流，一帧中途作废就缺块。
             * 真断流时多等几百毫秒也无妨。 */
            if (++stall > 400000u) {
                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;
                lcd_raw_idle();
                stall = 0u;
            }
        }

        /* 半帧残留自愈：所有收流状态都关掉之后，手上还剩 1..5 字节（6 字节
         * 定界永远补不齐）且 100 ms 没有新字节，就整段丢掉重新对齐。 */
        if (rx_len > 0 && rx_len < 6
            && raw_left == 0u && !asset_rx && !up_rx_on && !sig_rx) {
            uint32_t tt = systick_now();

            if ((tt - rx_last_tick) >= RX_IDLE_FLUSH_MS * 48000u) {
                rx_len = 0;
                rx_last_tick = tt;
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

        /* 待机动画：四角方块白/黄交替、图片轮播、弹球都在这里推进。
         * 用非阻塞状态机：主循环每转一圈给 standby_wait 加一拍，攒够
         * STANDBY_BLINK_TICKS（≈1 秒）才翻色/换图，这一圈收到字节就跳过，
         * 不会因为动画把 USB 收包和命令处理挡住。 */
        /* 非阻塞交叉淡入：这一圈只推一条 2 行带子（640 B）就回主循环，
         * 所以淡入期间 USB 每 3–10 ms 就被服务一次，不会再出现写超时。 */
        if (fade_active) {
            if (standby_mode == 0u) {
                /* 主机来帧/贴图把待机关了：静默放弃剩下的带子。这里不能动
                 * lcd CS（流式画面正开着窗口），band 收尾时它已经是高的。 */
                fade_active = 0u;
            } else {
                standby_fade_step();
            }
        }

        if (standby_mode) {
            if (blink_first) {
                blink_first = 0u;
                standby_wait = 0u;
                standby_draw();          /* 图片模式会在这里起一次淡入，立即返回 */
            } else if (!fade_active && rx_len == 0) {   /* 淡入没走完不计轮播拍数 */
                uint32_t need = STANDBY_BLINK_TICKS;

                /* 图片轮播按设置的“每张停留秒数”，方块轮播固定 1 秒，
                 * 弹球 20 拍一步（空闲时 ≈20 ms） */
                if (g_settings.standby_mode == 2u)
                    need = (uint32_t)g_settings.dwell * STANDBY_BLINK_TICKS;
                else if (g_settings.standby_mode == 3u)
                    need = 20u;

                standby_wait++;
                /* 待机时主循环几乎不转、打招呼的快计数器涨不起来，主机就会
                 * “找不到设备”。照旧每约 50 拍补一次 hello。 */
                if ((standby_wait % 50u) == 0u)
                    usb_write(hello, 6);
                if (standby_wait >= need) {
                    standby_wait = 0u;
                    standby_tick();
                }
            }
        }

        /* 资产写到一半没下文（主机崩了/线松了）：数空转圈数后作废，别把设备卡成哑巴。
         * 这里用主循环圈数而不是 SysTick 计时：空闲时一圈 ≈ 1 ms，计数单调，
         * 不受 SysTick 的分频/计数配置影响。 */
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

        /* OTA 上传窗口半开（0A 12 02 声明了 1 KB，字节却没来齐）：主循环空闲
         * 时一圈 ≈ 1 ms，UP_IDLE_LIMIT = 400 圈 ≈ 0.4 秒就作废，设备不会长时间
         * 停在“等数据”状态。作废时清状态并回一个
         * [0A,12,15,00,00,00]，主机能看到“这窗废了”，设备也不会把随后到的
         * 命令当成图像数据吃掉。 */
        if (up_rx_on) {
            if (rx_len == 0) {
                if (++up_idle > UP_IDLE_LIMIT) {
                    static const uint8_t up_abort[6] = {0x0Au, 0x12u, 0x15u, 0u, 0u, 0u};

                    up_rx_on = 0u;
                    up_rx_left = 0u;
                    up_idle = 0u;
                    usb_write(up_abort, 6);
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

                /* 填充字节：完全空闲时连续 ≥ RX_PAD_RUN 个 0x00 只可能是
                 * 上位机垫的对齐填充（6 字节帧最多 5 个前导 0，class 0 sub 0
                 * 本身就是空帧），直接跳过；收流中途不做这件事。
                 * 只按 6 的整数倍跳过：万一下一帧恰好也以 0x00 开头，多出来
                 * 的那个 0 必须留给它，否则反而把边界推错一位。 */
                if (raw_left == 0u && !asset_rx && !up_rx_on && !sig_rx
                    && rx[pos] == 0x00u) {
                    int z = 0;

                    while (pos + z < rx_len && rx[pos + z] == 0x00u)
                        z++;
                    if (z >= RX_PAD_RUN) {
                        pos += z - (z % 6);
                        continue;
                    }
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
        if (ver_pending && usb_write(&ver_val, 1u))
            ver_pending = 0u;

        if (hb_hold > 0u) {
            hb_hold--;
        } else if (++quiet >= 200u) {
            if (hello_tick == 0u)
                usb_write(hello, 6);
            if (++hello_tick >= 200u)
                hello_tick = 0u;
        }

        /* 空闲时按 1 ms 一拍走，给 idle_ms 一个真实的时间基准；
         * 有数据时保持全速，不拖慢收流。 */
        if (rx_len == 0 && raw_left == 0u)
            delay_ms(1);
    }
}
