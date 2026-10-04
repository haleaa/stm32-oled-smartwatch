#include "stm32f10x.h"                  // Device header
#include "MAX30102.h"
#include "HeartRate.h"
#include "OLED.h"
#include "Key.h"
#include "Menu.h"
#include "algorithm.h"

/*========================= 页面配置参数 =========================*/

#define MENU_HOME            0        // 菜单主页 ID（与 Menu.c 内部保持一致）

#define HR_REFRESH_MS        3000     // 心率血氧刷新周期：每 3 秒计算并刷新一次
#define HR_REENTRY_GAP_MS    800      // 两帧调用间隔超过此值，判定为“重新进入本页”

/* 界面布局坐标 */
#define HR_VAL_Y             0        // 心率大数字所在行
#define HR_SPO2_Y            32       // 血氧大数字所在行
#define HR_HINT_Y            56       // 底部按键提示行

/*========================= 静态状态量 =========================*/

/* 1ms 毫秒计时，由 SysTick 中断累加，为刷新周期与重入检测提供时间基准 */
static volatile uint32_t hr_ms = 0;

/* 采样环形缓冲：滚动保存最近 ALGO_BUFFER_SIZE 个样本 */
static uint32_t hr_ir_buf[ALGO_BUFFER_SIZE];
static uint32_t hr_red_buf[ALGO_BUFFER_SIZE];
static uint16_t hr_wr_idx = 0;        // 环形缓冲写指针
static uint16_t hr_count  = 0;        // 已填充样本数（满后保持 BUFFER_SIZE）

/* 计算用的线性快照：把环形缓冲按“从旧到新”展开，供算法使用 */
static uint32_t hr_snap_ir[ALGO_BUFFER_SIZE];
static uint32_t hr_snap_red[ALGO_BUFFER_SIZE];

/* 当前对外展示的结果（只在本页内使用） */
static HR_SpO2_Result_t hr_result;
static uint8_t hr_dev_present = 0;    // MAX30102 是否在线（进入页面时缓存）
static uint8_t hr_need_redraw = 1;    // 界面是否需要重绘（避免整屏闪烁）

/* 计时基准与初始化标志 */
static uint32_t hr_last_frame_ms = 0; // 上一帧调用时刻，用于重入检测
static uint32_t hr_last_calc_ms  = 0; // 上一次计算时刻，用于 3 秒周期
static uint8_t  hr_setup_done    = 0; // 是否已完成进入页面时的初始化

/*========================= 计时心跳 =========================*/

/* 由 SysTick_Handler 每 1ms 调用一次 */
void HeartRate_Tick(void)
{
	hr_ms++;
}

/*========================= 传感器配置 =========================*/

/* 重新配置 MAX30102：打开 4 倍硬件平均，使 FIFO 输出速率 = 100Hz / 4 = 25Hz，
   与算法设定的采样率一致；同时复位 FIFO 指针、清中断标志，丢弃旧数据。 */
static void HeartRate_SensorConfig(void)
{
	uint8_t dummy = 0;

	/* SMP_AVE=4(bits[7:5]=010) | 不 rollover | almost full=15 -> 0x4F */
	MAX30102_SendByte(0x4F, REG_FIFO_CONFIG);

	/* 复位 FIFO 读写指针与溢出计数 */
	MAX30102_SendByte(0x00, REG_FIFO_WR_PTR);
	MAX30102_SendByte(0x00, REG_OVF_COUNTER);
	MAX30102_SendByte(0x00, REG_FIFO_RD_PTR);

	/* 读一次中断状态寄存器，清除可能挂起的标志 */
	MAX30102_SpecAddrRead(&dummy, REG_INTR_STATUS_1);
	MAX30102_SpecAddrRead(&dummy, REG_INTR_STATUS_2);
}

/*========================= 复位（进入页面） =========================*/

/* 每次进入本页时调用：清空缓冲与结果、重配传感器、标记需要重绘（显示 --） */
static void HeartRate_Reset(void)
{
	uint16_t i;

	for (i = 0; i < ALGO_BUFFER_SIZE; i++)
	{
		hr_ir_buf[i]  = 0;
		hr_red_buf[i] = 0;
	}
	hr_wr_idx = 0;
	hr_count  = 0;

	hr_result.heart_rate = 0;
	hr_result.spo2       = 0;
	hr_result.hr_valid   = 0;
	hr_result.spo2_valid = 0;
	hr_result.finger     = 0;

	hr_dev_present = Is_MAX30102_GetID() ? 1 : 0;   // 缓存设备在线状态
	HeartRate_SensorConfig();

	hr_last_calc_ms = hr_ms;    // 以当前时刻作为刷新计时起点
	hr_need_redraw  = 1;        // 进入即重绘，未出结果时显示 --
}

/*========================= 采样 =========================*/

/* 非阻塞采集：驱动无新样本时返回 false，立即退出，不拖慢主循环 */
static void HeartRate_Sample(void)
{
	uint32_t red = 0, ir = 0;

	if (MAX30102_Read_FIFO(&red, &ir) == false) return;

	hr_ir_buf[hr_wr_idx]  = ir;
	hr_red_buf[hr_wr_idx] = red;
	hr_wr_idx = (uint16_t)((hr_wr_idx + 1) % ALGO_BUFFER_SIZE);
	if (hr_count < ALGO_BUFFER_SIZE) hr_count++;
}

/*========================= 计算一次 =========================*/

/* 取最近一整窗样本跑算法，并更新展示结果 */
static void HeartRate_Measure(void)
{
	int32_t i;
	uint16_t idx;
	HR_SpO2_Result_t r;

	/* 缓冲已满时，写指针处即为最旧样本，从此处顺序展开为“旧->新” */
	idx = hr_wr_idx;
	for (i = 0; i < ALGO_BUFFER_SIZE; i++)
	{
		hr_snap_ir[i]  = hr_ir_buf[idx];
		hr_snap_red[i] = hr_red_buf[idx];
		idx = (uint16_t)((idx + 1) % ALGO_BUFFER_SIZE);
	}

	Algorithm_Calc(hr_snap_ir, hr_snap_red, ALGO_BUFFER_SIZE, &r);

	/* 手指离开：清空为无效，界面回到 -- */
	if (r.finger == 0)
	{
		hr_result.hr_valid   = 0;
		hr_result.spo2_valid = 0;
		hr_result.finger     = 0;
		hr_need_redraw = 1;
		return;
	}
	hr_result.finger = 1;

	/* 本次有效才更新，无效则保留上一次的值，避免数字来回闪烁 */
	if (r.hr_valid)
	{
		hr_result.heart_rate = r.heart_rate;
		hr_result.hr_valid   = 1;
	}
	if (r.spo2_valid)
	{
		hr_result.spo2       = r.spo2;
		hr_result.spo2_valid = 1;
	}

	hr_need_redraw = 1;
}

/*========================= 对外接口：采集与计算 =========================*/

/* 主循环每帧调用：负责重入检测、非阻塞采样、按 3 秒周期触发计算 */
void HeartRate_GetResult(void)
{
	uint32_t now = hr_ms;

	/* 重入检测：与上一帧间隔过大，说明刚从其它页面切回（或首次进入），需复位 */
	if (hr_setup_done == 0 || (uint32_t)(now - hr_last_frame_ms) > HR_REENTRY_GAP_MS)
	{
		HeartRate_Reset();
		hr_setup_done = 1;
	}
	hr_last_frame_ms = now;

	/* 设备不在线则不采样，界面由 Show_UI 显示提示 */
	if (hr_dev_present == 0) return;

	/* 非阻塞采集一个样本（若有） */
	HeartRate_Sample();

	/* 缓冲填满后，每满 3 秒计算并刷新一次；未填满时持续对齐计时基准，
	   避免刚填满的瞬间立即触发计算。 */
	if (hr_count >= ALGO_BUFFER_SIZE)
	{
		if ((uint32_t)(now - hr_last_calc_ms) >= HR_REFRESH_MS)
		{
			HeartRate_Measure();
			hr_last_calc_ms = now;
		}
	}
	else
	{
		hr_last_calc_ms = now;
	}
}

/*========================= 对外接口：界面显示 =========================*/

void HeartRate_Show_UI(void)
{
	uint8_t key;

	/* 仅在内容变化（进入页面 / 出新结果）时整屏重绘，避免每帧 Clear 造成闪烁 */
	if (hr_need_redraw)
	{
		hr_need_redraw = 0;
		OLED_Clear();

		if (hr_dev_present == 0)
		{
			OLED_Printf(0, 24, OLED_8X16, "No Device");
		}
		else
		{
			/* 心率行：标签 + 大数字（未测到显示 --） + 单位 */
			OLED_Printf(0, 4, OLED_8X16, "HR");
			if (hr_result.hr_valid)
				OLED_Printf(32, HR_VAL_Y, OLED_12X24, "%d", (int)hr_result.heart_rate);
			else
				OLED_Printf(32, HR_VAL_Y, OLED_12X24, "--");
			OLED_Printf(80, 8, OLED_8X16, "bpm");

			/* 血氧行 */
			OLED_Printf(0, 36, OLED_8X16, "SpO2");
			if (hr_result.spo2_valid)
				OLED_Printf(48, HR_SPO2_Y, OLED_12X24, "%d", (int)hr_result.spo2);
			else
				OLED_Printf(48, HR_SPO2_Y, OLED_12X24, "--");
			OLED_Printf(88, 36, OLED_8X16, "%");

			/* 底部按键提示 */
			OLED_Printf(0, HR_HINT_Y, OLED_6X8, "K3=BACK");
		}
	}

	/* 返回键（Key3）：回到菜单主页 */
	key = Key_GetNum();
	if (key == 3)
	{
		Menu_SetFeatPage(MENU_HOME);
	}
}
