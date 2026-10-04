#include <math.h>
#include "algorithm.h"

/*========================= 可调参数 =========================*/

/* 手指检测阈值：红外通道直流(DC)均值低于此值，判定为未贴合手指。
   该值与 LED 电流、ADC 量程有关，如硬件不同可在此微调。 */
#define ALGO_FINGER_THRESH      30000L

/* 心率搜索范围(bpm)，决定自相关的滞后搜索区间 */
#define ALGO_HR_MIN             30
#define ALGO_HR_MAX             220

/* 自相关强度阈值：主峰能量 / 零滞后能量 低于此值，认为周期性不足，心率无效 */
#define ALGO_AC_STRENGTH_MIN    0.30f

/* 血氧 R 比值的合理范围，超出则认为信号异常、结果无效 */
#define ALGO_RATIO_MIN          0.20f
#define ALGO_RATIO_MAX          1.50f

/* 红外交流分量的滑动平均半窗口(点)，用于抑制高频噪声 */
#define ALGO_MA_HALF            2

static int32_t s_ir_ac[ALGO_BUFFER_SIZE];    // 红外去直流后的交流分量
static int32_t s_red_ac[ALGO_BUFFER_SIZE];   // 红光去直流后的交流分量
static int32_t s_ir_ma[ALGO_BUFFER_SIZE];    // 红外交流分量滑动平均后的结果

/*========================= 心率血氧计算 =========================*/

void Algorithm_Calc(const uint32_t *ir_buffer, const uint32_t *red_buffer,
                    int32_t length, HR_SpO2_Result_t *result)
{
	int32_t i, k, idx;
	int64_t sum_ir = 0, sum_red = 0;        // 直流累加(用 64 位防溢出)
	int32_t mean_ir, mean_red;              // 直流均值
	int64_t ss_ir = 0, ss_red = 0;          // 交流分量平方和
	int64_t energy = 0, corr;               // 自相关能量与互相关累加
	int32_t lag, best_lag = 0;              // 滞后点数与最优滞后
	int32_t lag_min, lag_max;               // 滞后搜索区间
	int32_t n, hr;
	float r0, r_best = 0.0f, r_cur;         // 归一化自相关
	float strength;
	float rms_ir, rms_red, ratio, spo2f;

	/* 结果清零：默认全部无效，只有满足条件才逐项置位 */
	result->heart_rate = 0;
	result->spo2       = 0;
	result->hr_valid   = 0;
	result->spo2_valid = 0;
	result->finger     = 0;

	/* 入参合法性检查 */
	if (ir_buffer == 0 || red_buffer == 0 || result == 0) return;
	if (length <= 0) return;
	if (length > ALGO_BUFFER_SIZE) length = ALGO_BUFFER_SIZE;

	/*---------- 1. 计算直流分量(DC)均值 ----------*/
	for (i = 0; i < length; i++)
	{
		sum_ir  += (int64_t)ir_buffer[i];
		sum_red += (int64_t)red_buffer[i];
	}
	mean_ir  = (int32_t)(sum_ir  / length);
	mean_red = (int32_t)(sum_red / length);

	/*---------- 2. 手指检测 ----------*/
	/* 未贴合手指时红外反射光很弱，DC 均值极低，此时算出的心率血氧无意义 */
	if (mean_ir < ALGO_FINGER_THRESH)
	{
		return;   // finger 保持 0，其余全部无效
	}
	result->finger = 1;

	/*---------- 3. 去直流，提取交流分量(脉搏波) ----------*/
	for (i = 0; i < length; i++)
	{
		s_ir_ac[i]  = (int32_t)ir_buffer[i]  - mean_ir;
		s_red_ac[i] = (int32_t)red_buffer[i] - mean_red;
	}

	/*---------- 4. 对红外交流分量做滑动平均，抑制高频噪声 ----------*/
	for (i = 0; i < length; i++)
	{
		int32_t acc = 0, cnt = 0;
		for (k = -ALGO_MA_HALF; k <= ALGO_MA_HALF; k++)
		{
			idx = i + k;
			if (idx < 0) idx = 0;                 // 边界做钳位处理
			if (idx >= length) idx = length - 1;
			acc += s_ir_ac[idx];
			cnt++;
		}
		s_ir_ma[i] = acc / cnt;
	}

	/*---------- 5. 心率：自相关法寻找最强周期 ----------*/
	/* 心率(HR) 与 周期滞后(lag) 的关系：lag = 采样率 * 60 / HR */
	lag_min = (ALGO_SAMPLE_RATE * 60) / ALGO_HR_MAX;   // 高心率 -> 小滞后
	lag_max = (ALGO_SAMPLE_RATE * 60) / ALGO_HR_MIN;   // 低心率 -> 大滞后
	if (lag_min < 1) lag_min = 1;
	if (lag_max > length - 1) lag_max = length - 1;

	/* 零滞后处的自相关，即信号能量，用作归一化基准 */
	for (i = 0; i < length; i++)
	{
		energy += (int64_t)s_ir_ma[i] * s_ir_ma[i];
	}
	r0 = (float)energy / (float)length;

	/* 在搜索区间内逐点计算归一化自相关，取最大值对应的滞后 */
	for (lag = lag_min; lag <= lag_max; lag++)
	{
		corr = 0;
		n = length - lag;
		for (i = 0; i < n; i++)
		{
			corr += (int64_t)s_ir_ma[i] * s_ir_ma[i + lag];
		}
		r_cur = (float)corr / (float)n;      // 除以重叠点数，消除长度偏置
		if (r_cur > r_best)
		{
			r_best  = r_cur;
			best_lag = lag;
		}
	}

	/* 依据主峰强度与最优滞后反推心率 */
	if (r0 > 0.0f && best_lag > 0)
	{
		strength = r_best / r0;              // 周期性强弱(0~1)
		if (strength >= ALGO_AC_STRENGTH_MIN)
		{
			hr = (ALGO_SAMPLE_RATE * 60 + best_lag / 2) / best_lag;   // 四舍五入
			if (hr >= ALGO_HR_MIN && hr <= ALGO_HR_MAX)
			{
				result->heart_rate = hr;
				result->hr_valid   = 1;
			}
		}
	}

	/*---------- 6. 血氧：比值法(Ratio of Ratios) ----------*/
	/* 先求两路交流分量的 RMS（用平方和开方表征脉搏幅度） */
	for (i = 0; i < length; i++)
	{
		ss_ir  += (int64_t)s_ir_ac[i]  * s_ir_ac[i];
		ss_red += (int64_t)s_red_ac[i] * s_red_ac[i];
	}
	rms_ir  = sqrtf((float)ss_ir  / (float)length);
	rms_red = sqrtf((float)ss_red / (float)length);

	/* R = (AC_red / DC_red) / (AC_ir / DC_ir) */
	if (mean_red > 0 && rms_ir > 1.0f)
	{
		ratio = (rms_red / (float)mean_red) / (rms_ir / (float)mean_ir);
		if (ratio >= ALGO_RATIO_MIN && ratio <= ALGO_RATIO_MAX)
		{
			/* 经验二次多项式拟合（与 Maxim 参考算法同款）：
			   SpO2 = -45.060*R^2 + 30.354*R + 94.845 */
			spo2f = -45.060f * ratio * ratio + 30.354f * ratio + 94.845f;
			if (spo2f < 0.0f)   spo2f = 0.0f;
			if (spo2f > 100.0f) spo2f = 100.0f;
			result->spo2       = (int32_t)(spo2f + 0.5f);
			result->spo2_valid = 1;
		}
	}
}
