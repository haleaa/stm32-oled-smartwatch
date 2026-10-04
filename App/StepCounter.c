#include "stm32f10x.h"                  // Device header
#include <math.h>
#include "MPU6050.h"
#include "MPU6050_Reg.h"
#include "OLED.h"
#include "Key.h"
#include "Menu.h"
#include "StepCounter.h"

/*========================= 可调参数（计步算法核心） =========================*/

/* 采样节拍：TIM4 每 20ms 中断一次 -> 50Hz。
   人走路步频最高约 3Hz，50Hz 远高于奈奎斯特频率，足以还原峰谷波形。 */
#define STEP_SAMPLE_PERIOD_MS   20

/* 滑动平均窗口：5 点(=100ms)，平滑波形、去高频抖动毛刺 */
#define STEP_FILTER_WIN         5

/* 动态基线更新系数：慢更新，稳定近似“重力分量”，自动适应佩戴角度与温漂。
   50Hz 下 0.01 对应约 2 秒时间常数；如需跟随更快可增大到 0.02(约 1 秒)。 */
   
//持续变化 2 秒以上的慢信号，基线才会明显跟着动；走路那种每秒好几下的快波动，基线根本来不及追
#define STEP_BASELINE_ALPHA     0.01f
//新的基线 = 旧基线 × 0.99 + 当前加速度采样值 × 0.01
//一阶低通滤波就是按百分比重新分布，滤除高速变化的，让变量缓慢变化。

/* 峰/谷阈值相对动态基线的偏移量(g)：不用固定阈值，自适应不同人/佩戴方式 */
#define STEP_PEAK_OFFSET        0.08f   // 峰阈值 = 基线 + 0.08g（脚蹬地向上加速）
#define STEP_VALLEY_OFFSET      0.05f   // 谷阈值 = 基线 - 0.05g（脚落地向下减速）

/* 步频校验：相邻两有效候选步的时间间隔(ms)。正常 30~180 步/分 -> 0.33~2s */
#define STEP_DT_MIN_MS          300     // < 300ms：抖手/敲击振动，丢弃
#define STEP_DT_MAX_MS          2000    // > 2s：视为新一轮步态的第一步（重新起算）

/* 幅值校验：走路合加速度峰值(g)的合理区间 */
#define STEP_PEAK_MIN           1.04f   // < 1.04g：振动太弱（轻微晃动），丢弃
#define STEP_PEAK_MAX           2.50f   // > 2.5g：磕碰/跳跃等强冲击，丢弃

/* 连续步状态机：候选态需在窗口内连续 N 步才确认，是最有效的防误计手段 */
#define STEP_CONFIRM_NUM        3       // 连续 >= 3 步确认后进入计步态，并补计这 3 步
#define STEP_CONFIRM_WIN_MS     3000    // 且这 3 步需在 3 秒内完成
#define STEP_WALK_TIMEOUT_MS    2000    // 计步态下连续 2 秒无新有效步 -> 退回静止态

/* 陀螺仪辅助去误判（六轴进阶）：走路时手臂自然摆动，合角速度有周期性波动。
   默认关闭以保证纯加速度方案在各种佩戴位置(手腕/口袋/推车)都稳健；
   手腕佩戴场景可置 1 启用，用合角速度峰值剔除“无转动”的伪步态。 */
#define STEP_USE_GYRO_ASSIST    0
#define STEP_GYRO_MIN_DPS       15.0f   // 候选步周期内合角速度峰值下限：过小=几乎无转动
#define STEP_GYRO_MAX_DPS       240.0f  // 上限：接近 ±250dps 量程，过大为剧烈甩动

/* MPU6050 计步最优量程对应的灵敏度换算系数 */
#define STEP_ACC_SENSITIVITY    16384.0f    // ±2g    -> 16384 LSB/g
#define STEP_GYRO_SENSITIVITY   131.0f      // ±250dps-> 131 LSB/(deg/s)

/* 菜单主页 ID（与 Menu.c 内部保持一致，用于返回菜单） */
#define MENU_HOME               0

/*========================= 类型与静态变量 =========================*/

/* 计步器运行状态 */
typedef struct
{
	Step_State_t state;         // 当前状态机状态
	uint32_t total_steps;       // 最终有效步数（对外输出）
	uint32_t candidate_steps;   // 候选态已累计的连续候选步数
	uint32_t last_step_time;    // 上一个有效候选步时间戳(ms)，0 表示尚无
	uint32_t cand_first_time;   // 候选态第一步时间戳(ms)，用于 3 秒窗口判定
	float    baseline;          // 动态基线(近似重力，静止时约 1.0g)
	uint8_t  peak_flag;         // 峰谷检测标志：1=已越过峰阈值，等待回落到谷阈值
	float    last_peak_val;     // 本步周期内合加速度峰值(g)
	float    gyro_cycle_max;    // 本步周期内合角速度峰值(dps)
} Step_Counter_t;

static Step_Counter_t sc;

//环形缓冲区
static float   filter_buf[STEP_FILTER_WIN];
static uint8_t filter_idx  = 0;
static uint8_t filter_init = 0;

/* TIM4 采样节拍：step_tick 在中断中自增(50Hz)，主循环据此判定是否到达新采样点。
   时间戳 time_ms = step_tick * 20ms，即使主循环偶有延迟也能保持时间基准准确。 */
static volatile uint32_t step_tick = 0;
static uint32_t last_processed_tick = 0;

static uint8_t KeyNum;
static uint8_t mpu_ok = 0;   // 0 = MPU6050 absent -> skip blocking I2C reads

/*========================= 底层：数据预处理 =========================*/

// 滑动平均滤波：5 点窗口平滑合加速度，去掉高频抖动毛刺
//就是五个数字加起来取平均
static float Step_AccSmooth(float val)
{ 
	uint8_t i;
	float sum = 0.0f;

	if (filter_init == 0)               // 首帧：用当前值填满窗口，避免从 0 爬升的启动瞬态
	{
		for (i = 0; i < STEP_FILTER_WIN; i++) filter_buf[i] = val;
		filter_init = 1;
		return val;
	}

	filter_buf[filter_idx] = val;
	filter_idx = (uint8_t)((filter_idx + 1) % STEP_FILTER_WIN);
	for (i = 0; i < STEP_FILTER_WIN; i++) sum += filter_buf[i];
	return sum / STEP_FILTER_WIN;
}

/*========================= 上层：核心计步处理 =========================*/

/* 每个采样点(50Hz)调用一次。
   acc：平滑后的合加速度(g)；gyro：合角速度(dps)；time_ms：当前时间戳(ms)。 */
static void Step_Process(float acc, float gyro, uint32_t time_ms)
{
	float high_th, low_th;
	uint32_t dt;

	/* 1. 动态基线慢更新，消除佩戴角度与温漂影响 */
	//一阶低通滤波
	sc.baseline = sc.baseline * (1.0f - STEP_BASELINE_ALPHA) + acc * STEP_BASELINE_ALPHA;
	//STEP_BASELINE_ALPHA平滑系数
	//就是按比例把旧基线和新采样加速度重新分配
	
	high_th = sc.baseline + STEP_PEAK_OFFSET;   // 峰阈值，就是最少抬起算作一步的值
	low_th  = sc.baseline - STEP_VALLEY_OFFSET; // 谷阈值，就是最少放下算作一步的值

	/* 2. 峰谷检测——上升沿：平滑加速度由下向上越过峰阈值 */
	if (sc.peak_flag == 0 && acc > high_th)
	{
		sc.peak_flag = 1;
		sc.last_peak_val  = acc;    // 先记录越阈瞬间值，随后在本周期内持续取真实峰值
		sc.gyro_cycle_max = gyro;
	}
	else if (sc.peak_flag == 1)     // 处于峰-谷周期内，跟踪真实峰值与角速度峰值
	{
		if (acc  > sc.last_peak_val)  sc.last_peak_val  = acc;
		if (gyro > sc.gyro_cycle_max) sc.gyro_cycle_max = gyro;
	}
	//如果当前加速度角速度比之前波峰还要大，就更新波峰

	/* 3. 峰谷检测——下降沿：回落到谷阈值以下，一个完整步周期结束，生成候选步 */
	if (sc.peak_flag == 1 && acc < low_th)
	{
		sc.peak_flag = 0;

		/* 4. 步频下限校验：间隔过短多为抖手/敲击，直接丢弃（不更新时间戳） */
		if (sc.last_step_time != 0 && (time_ms - sc.last_step_time) < STEP_DT_MIN_MS)
		{
			return;
		}

		/* 5. 幅值合法性校验：走路峰值通常在 1.1g ~ 2.5g */
		if (sc.last_peak_val < STEP_PEAK_MIN || sc.last_peak_val > STEP_PEAK_MAX)
		{
			return;
		}

#if STEP_USE_GYRO_ASSIST
		/* 5b. 陀螺仪辅助：候选步周期内应伴随合理的手臂转动角速度 */
		if (sc.gyro_cycle_max < STEP_GYRO_MIN_DPS || sc.gyro_cycle_max > STEP_GYRO_MAX_DPS)
		{
			return;
		}
#endif

		/* 到这里已经是一个“有效候选步”，交给连续步状态机裁决 */
		dt = (sc.last_step_time != 0) ? (time_ms - sc.last_step_time) : 0xFFFFFFFFu;
//		第一行：算距离上一步过了多久

//		如果之前有过上一步（last_step_time != 0），就算出时间差dt。
//		如果是开机以来第一步，就给一个超大的数0xFFFFFFFF，保证肯定超过最大间隔。

		/* 6a. 首步 / 长间隔(>2s)后的第一步：作为新一轮步态起点，进入候选态重新累计。
		       本步暂不计入 total，等待连续确认，避免偶发单步误计。 */
		if (dt > STEP_DT_MAX_MS)
		{
			sc.state = Step_State_Candidate;
			sc.candidate_steps = 1;
			sc.cand_first_time = time_ms;
			sc.last_step_time  = time_ms;
			return;
		}

		/* 6b. 间隔在 [MIN, MAX] 内的连续步：按状态机处理 */
		switch (sc.state)
		{
			//静止态
			case Step_State_Still:      
				sc.candidate_steps = 1;
				sc.cand_first_time = time_ms;
				sc.state = Step_State_Candidate;
				break;

			case Step_State_Candidate:  // 候选态：3 秒内连续 >= 3 步则确认并补计
				if (time_ms - sc.cand_first_time > STEP_CONFIRM_WIN_MS)
				{
					// 超出 3 秒窗口，以本步为新起点重新累计
					sc.candidate_steps = 1;
					sc.cand_first_time = time_ms;
				}
				else
				{
					sc.candidate_steps++;
					if (sc.candidate_steps >= STEP_CONFIRM_NUM)
					{
						sc.state = Step_State_Walking;
						sc.total_steps += STEP_CONFIRM_NUM;   // 补计前面确认的候选步
					}
				}
				break;

			case Step_State_Walking:    // 计步态：每来一个有效步 +1
				sc.total_steps++;
				break;

			default:
				sc.state = Step_State_Still;
				break;
		}
		sc.last_step_time = time_ms;
	}

	/* 7. 超时退回静止态：计步/候选态下连续 2 秒无新有效步 */
	if (sc.state != Step_State_Still && (time_ms - sc.last_step_time) > STEP_WALK_TIMEOUT_MS)
	{
		sc.state = Step_State_Still;
		sc.candidate_steps = 0;
	}
}

/*========================= 底层：采样与传感器读取 =========================*/

/* 读取一次 MPU6050，换算物理量并跑一遍两级计步算法 */
static void Step_SampleTask(void)
{
	int16_t ax, ay, az, gx, gy, gz;
	float fx, fy, fz, gxd, gyd, gzd;
	float acc_total, acc_smooth, gyro_total;

	if (mpu_ok == 0) return;   //没检测到MPU6050直接退出
	if (MPU6050_GetData(&ax, &ay, &az, &gx, &gy, &gz) == 0) return;   // 读取失败直接退出
	
	fx = (float)ax / STEP_ACC_SENSITIVITY;
	fy = (float)ay / STEP_ACC_SENSITIVITY;
	fz = (float)az / STEP_ACC_SENSITIVITY;
	//用对应量程的灵敏度系数，处理一下得到的原始传感器数据，换算成多少g

	/* 合加速度 a_total = sqrt(ax^2+ay^2+az^2)，消除佩戴角度影响，只看整体运动强度 */
	acc_total  = sqrtf(fx * fx + fy * fy + fz * fz);
	//sqrtf是求平方根函数
	//它在这里的作用，就是用三维勾股定理，把三个垂直方向的加速度分量，合成一个总的合加速度大小
	
	acc_smooth = Step_AccSmooth(acc_total);   // 滑动平均滤波（五点取平均）

	/* 合角速度 gyro_total = sqrt(gx^2+gy^2+gz^2)（陀螺仪辅助用） */
	gxd = (float)gx / STEP_GYRO_SENSITIVITY;
	gyd = (float)gy / STEP_GYRO_SENSITIVITY;
	gzd = (float)gz / STEP_GYRO_SENSITIVITY;
	gyro_total = sqrtf(gxd * gxd + gyd * gyd + gzd * gzd);

	/* 时间戳由 50Hz 节拍换算，保证与真实时间对齐 */
	Step_Process(acc_smooth, gyro_total, step_tick * STEP_SAMPLE_PERIOD_MS);
}

/* 复位计步状态机与滤波器 */
static void Step_Counter_Reset(void)
{
	sc.state           = Step_State_Still;
	sc.total_steps     = 0;
	sc.candidate_steps = 0;
	sc.last_step_time  = 0;
	sc.cand_first_time = 0;
	sc.baseline        = 1.0f;   // 重力约 1g，作为基线初值
	sc.peak_flag       = 0;
	sc.last_peak_val   = 0.0f;
	sc.gyro_cycle_max  = 0.0f;

	filter_idx  = 0;
	filter_init = 0;
}

/* 配置 TIM4 为 50Hz 采样节拍中断（仅自增 step_tick，读取运算放在主循环，避免阻塞中断） */
static void Step_Timer_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);   // RCC 时钟使能
	TIM_InternalClockConfig(TIM4);                         // 设置内部时钟

	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;      // 72MHz/72 = 1MHz
	TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1;      // 1MHz/20000 = 50Hz(20ms)
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);

	TIM_ClearFlag(TIM4, TIM_FLAG_Update);                  // 清除初始化导致的中断标志位
	TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);             // 使能更新中断

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);        // 与工程其余定时器一致
	NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;  // 抢占优先级低于 TIM2/TIM3(2)
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM4, ENABLE);                                 // 启动定时器
}

/* TIM4 中断服务：50Hz 节拍，仅自增计数，耗时的 I2C 读取放在主循环完成 */
void TIM4_IRQHandler(void)
{
	if (TIM_GetFlagStatus(TIM4, TIM_FLAG_Update) == SET)
	{
		step_tick++;
		TIM_ClearITPendingBit(TIM4, TIM_FLAG_Update);
	}
}

/*========================= 对外接口 =========================*/

void StepCounter_Init(void)
{
	/* 1. 传感器初始化：硬件 I2C + 唤醒 + 基础配置 */
	MPU6050_Init();

	//配置MPU6050寄存器，调整一下量程让计步更精确
	MPU6050_WriteReg(MPU6050_SMPLRT_DIV,   0x13);   // 1kHz/(1+19) = 50Hz
	MPU6050_WriteReg(MPU6050_CONFIG,       0x03);   // DLPF：加速度 44Hz / 陀螺仪 42Hz
	MPU6050_WriteReg(MPU6050_GYRO_CONFIG,  0x00);   // ±250dps -> 131 LSB/(deg/s)
	MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);   // ±2g     -> 16384 LSB/g

	/* 3. 复位计步状态机 */
	Step_Counter_Reset();
	last_processed_tick = 0;
	step_tick = 0;

	/* 4. 启动 50Hz 采样节拍 */
	Step_Timer_Init();

	/* presence gate: if MPU6050 is absent, Step_SampleTask skips all I2C reads */
	mpu_ok = MPU6050_IsConnected();
}

//这个在主循环不断调用检测步数
void StepCounter_Update(void)
{
	//未到达新的采样点则直接返回；到达则处理最新一拍（主循环慢比中断时不积压，快时不处理省资源）
	if (step_tick == last_processed_tick) return;
	last_processed_tick = step_tick;

	Step_SampleTask();
}

uint32_t StepCounter_GetSteps(void)
{
	return sc.total_steps;
}

Step_State_t StepCounter_GetState(void)
{
	return sc.state;
}

void StepCounter_Reset(void)
{
	Step_Counter_Reset();
}

/*========================= 步数页面 UI =========================*/

void StepCounter_Show_UI(void)
{
	OLED_Clear();

	/* 左上角返回图标 + 右上角步数图标（复用工程已有取模图片，无需中文字库） */
	OLED_ShowImage(0, 0, 16, 16, Back);
	OLED_ShowImage(96, 0, 32, 32, Steps);

	/* 大号步数 */
	OLED_Printf(8, 24, OLED_12X24, "%lu", (unsigned long)sc.total_steps);
	//处理纯正整数可以用%u

	/* 左下角状态机状态（便于观察两级算法工作情况） */
	switch (sc.state)
	{
		case Step_State_Walking:   OLED_Printf(8, 52, OLED_6X8, "WALKING"); break;
		case Step_State_Candidate: OLED_Printf(8, 52, OLED_6X8, "READY");   break;
		default:                   OLED_Printf(8, 52, OLED_6X8, "IDLE");    break;
	}

	/* 右下角提示：Key1 清零步数 */
	OLED_Printf(90, 52, OLED_6X8, "K1=CLR");
}

void StepCounter_Switch(void)
{
	KeyNum = Key_GetNum();

	if (KeyNum == 3)        // 确认键：返回菜单
	{
		Menu_SetFeatPage(MENU_HOME);
	}
	else if (KeyNum == 1)   // 清零步数
	{
		StepCounter_Reset();
	}
}
