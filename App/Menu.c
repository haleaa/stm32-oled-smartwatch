#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "Page.h"
#include "FlashLight.h"
#include "StopWatch.h"
#include "StepCounter.h"
#include "HeartRate.h"
#include "Weather.h"

#define MENU_START_X 1
#define MENU_START_Y 11
#define SCREEN_W 128
#define CELL_SIZE 42
#define ICON_SIZE 32
#define SHOW_CELL_NUM 3
#define ITEM_TOTAL 6
#define ICON_MARGIN         ((CELL_SIZE - ICON_SIZE) / 2)  // 图标在单元内的边距=5
#define ICON_BASE_Y         (MENU_START_Y + ICON_MARGIN)
#define BOX_START 			MENU_START_X + CELL_SIZE

#define ANIM_IDLE           0           // 动画空闲
#define ANIM_RUNNING        1           // 动画运行中

#define MENU_HOME   		0
#define FEATURE_BACK		1
#define FEATURE_STEPS		2
#define FEATURE_STOPWATCH	3
#define FEATURE_FLASHLIGHT	4
#define FEATURE_HEARTRATE	5
#define FEATURE_WEATHER		6

static int8_t Start_Index = 0;            // 最左侧单元对应的菜单索引
static int16_t Anim_x_Offset = 0;          // 当前水平偏移量
static int16_t Target_Offset = 0;		   // 目标偏移量
static uint8_t anim_state = ANIM_IDLE;     // 动画状态机

static uint8_t FeaturePage = 0;

static uint8_t KeyNum; 
	
typedef struct
{
	const uint8_t *Icon;
	uint8_t FeatureId; //功能id
}MenuItem_t;

static MenuItem_t menu_list[ITEM_TOTAL];   // 菜单列表

void Menu_Init(void)
{
	menu_list[0].Icon = Menu_Back;	menu_list[0].FeatureId = FEATURE_BACK;
	menu_list[1].Icon = Steps;		menu_list[1].FeatureId = FEATURE_STEPS;
	menu_list[2].Icon = StopWatch;	menu_list[2].FeatureId = FEATURE_STOPWATCH;
	menu_list[3].Icon = FlashLight;	menu_list[3].FeatureId = FEATURE_FLASHLIGHT;
	menu_list[4].Icon = HeartRate;	menu_list[4].FeatureId = FEATURE_HEARTRATE;
	menu_list[5].Icon = Weather;	menu_list[5].FeatureId = FEATURE_WEATHER;
	
	FlashLight_Init();
}

void Menu_Show_UI(void)
{
	int16_t Cell_x, Icon_x;
	int8_t Menu_Idx;
	
	OLED_Clear();

	OLED_ShowImage(BOX_START, MENU_START_Y, CELL_SIZE, CELL_SIZE, Box);
	
	for (int8_t i = -1; i < SHOW_CELL_NUM + 1; i++)
	{
		Menu_Idx = Start_Index + i;
//		如果想让菜单循环显示就加上Menu_Idx的边界
//		Menu_Idx %= ITEM_TOTAL;
		
		if (Menu_Idx < 0 || Menu_Idx >= ITEM_TOTAL) continue; //Menu_Idx < 0是非法的数组下标，所以要跳过
		
		Cell_x = MENU_START_X + i * CELL_SIZE + Anim_x_Offset;
		// 完全移出屏幕的跳过，先不处理在屏幕外面提前预设好的图标，移进屏幕里面再处理
        if(Cell_x + CELL_SIZE < 0 || Cell_x >= SCREEN_W) continue;
		
		Icon_x = Cell_x + ICON_MARGIN;
		OLED_ShowImage(Icon_x, ICON_BASE_Y, ICON_SIZE, ICON_SIZE, menu_list[Menu_Idx].Icon);
	}
}

void Menu_AnimUpdate(void)
{
	int16_t remaining_dist; //剩余距离
	
	if (anim_state == ANIM_IDLE) return;
	
	remaining_dist = Target_Offset - Anim_x_Offset;
	
	//ease-out缓动：每次移动剩余距离的1/4
	if (remaining_dist > 3 || remaining_dist < -3) //不能是if (remaining_dist != 0)因为remaining_dist这样计算永远不可能为0，会导致动画卡死在仅差一个像素的地方。
	{											   //代码这个条件remaining_dist为1,0，-1都会进入这个if，也就是距离最后只差1个像素了进入else手动对齐到终点
		Anim_x_Offset += remaining_dist / 4;
	}
	else
	{
		Anim_x_Offset = Target_Offset;
		
		//选中图标往右移一个是图标整体向左偏移，因此Target_Offset为负数时是图标右移，正数是图标左移
		if (Target_Offset < 0)
		{
			Start_Index ++;
		}
		else if (Target_Offset > 0)
		{
			Start_Index --;
		}
		
		Anim_x_Offset = 0; Target_Offset = 0;
		anim_state = ANIM_IDLE;
	}
}

static void Menu_Next(void)
{
    //当最后一个图标位于正中央时菜单不能再右移
    if(Start_Index >= ITEM_TOTAL - 2) return;
//    // 动画运行中可打断，直接更新目标
//    if(anim_state == ANIM_RUNNING) return; // 如需动画可打断，删掉这行

    Target_Offset = -CELL_SIZE;  // 整体左移一个单元
    anim_state = ANIM_RUNNING;
}

static void Menu_Prev(void)
{
    //当第一个图标位于正中央时菜单不能再左移
    if(Start_Index <= -1) return;
//    if(anim_state == ANIM_RUNNING) return; // 如需动画可打断，删掉这行

    Target_Offset = CELL_SIZE;   // 整体右移一个单元
    anim_state = ANIM_RUNNING;
}

static uint8_t Menu_GetFeatID(void)
{
    // 选中项 = 中间单元 = start_index + 1
    return menu_list[Start_Index + 1].FeatureId;
}

void Menu_SetFeatPage(uint8_t FeatPage)
{
	FeaturePage = FeatPage;
}

void Menu_Switch(void)
{
	KeyNum = Key_GetNum();
	if (KeyNum == 1)
	{
		Menu_Next();
	}
	else if (KeyNum == 2)
	{
		Menu_Prev();
	}
	else if (KeyNum == 3)
	{
		FeaturePage = Menu_GetFeatID();
	}
}

void Menu_FeatPageSwitch(void)
{
	switch (FeaturePage)
	{
		case MENU_HOME:
			Menu_Show_UI();
			Menu_Switch();
			break;
		
		case FEATURE_BACK:
			Page_Mode = Page_Time;
			FeaturePage = MENU_HOME;
			break;
		
		case FEATURE_STEPS:
			StepCounter_Show_UI();
			StepCounter_Switch();
			break;
		
		case FEATURE_STOPWATCH:
			StopWatch_Time_Calculate();
			StopWatch_Show_UI();
			StopWatch_Switch();
			break;
		
		case FEATURE_FLASHLIGHT:
			FlashLight_Render();
			FlashLight_Switch();
			break;
		
		case FEATURE_HEARTRATE:
			HeartRate_GetResult();
			HeartRate_Show_UI();
			break;
		
		case FEATURE_WEATHER:
			Weather_Show_UI();
			Weather_Switch();
			break;
		
		default:
			FeaturePage = MENU_HOME;
			break;
	}		
}

//初始化函数放在主函数里面，不要放进循环了。

//遇到问题：用取模软件画选中边框的时候没有调整图像大小，用的图标的图像大小导致显示出错
//解决：取模软件调成正确的42x42大小就能正常显示了

//bug:菜单向左滑动一格时，第一个单元格对应的图标在动画结束时才被刷新
//解决：让for循环里的i从-1开始，让屏幕左右两边都多提前渲染一格

//bug：将第一个单元格移到最左边时，因为Start_Index = -1 因此 Menu_Idx会变成负数，而数组下标负数是非法的导致花屏乱码
//解决：用卫语句把非法情况排除		if (Menu_Idx < 0 || Menu_Idx >= ITEM_TOTAL) continue; //Menu_Idx < 0是非法的数组下标，所以要跳过

// MARGIN 外边距 animation 动画 offset偏移量 target目标 IDLE空闲 Cell单元格 Previous 上一个

//if (anim_state == ANIM_IDLE) return;这是一个卫语句,这里比正向条件包裹if (anim_state == ANIM_RUNNING){功能}更好
//场景								推荐写法			原因
//参数校验、边界检查、无效状态过滤	A 卫语句			快速排除噪音，主逻辑保持平整
//有锁/内存/文件句柄等成对资源		B 正向包裹			防止提前return导致泄漏
//3个及以上平等互斥状态				Switch / if-else if	保持对称性和可读性
//排除后还需要做日志/清理/统计		B 正向包裹			卫语句无法承载“排除后的动作”
//核心业务逻辑超过5行且有嵌套		A 卫语句			减少缩进地狱
//函数体≤3行的简单判断				B 或直接表达式		没必要为这么短的代码用卫语句
//这里属于无效状态过滤，而且可以避免缩进地狱

