#include "touch.h"
#include "stdlib.h"
#include "math.h"
#include "spi.h" /* Required for the hspi2 handle */
#include "string.h"

/*
 * This driver is now based on the STM32 HAL library, using hardware SPI.
 * Ensure the SPI peripheral (hspi2), CS pin, and PEN pin are configured in CubeMX.
 */

_m_tp_dev tp_dev =
{
    .init = TP_Init,
    .scan = TP_Scan,
    .adjust = TP_Adjust,
    .x = {0},
    .y = {0},
    .sta = 0,
    .xfac = 0.0f,
    .yfac = 0.0f,
    .xoff = 0,
    .yoff = 0,
    .touchtype = 0,
};

/*
 * Default commands for XPT2046.
 * These will be dynamically adjusted in TP_Init() based on the LCD's orientation
 * to ensure the logical X/Y axes match the physical touch axes.
 */
uint8_t CMD_RDX = 0xD0;
uint8_t CMD_RDY = 0x90;

/**
 * @brief  通过SPI向触摸控制器写入一个字节
 * @param  num: 要写入的字节数据
 * @retval 无
 * @note   使用HAL库的SPI传输函数，通过hspi2外设发送数据
 *           - 使用硬件SPI接口，确保高效可靠
 *           - 超时时间设置为1000ms
 *           - 该函数主要用于XPT2046触摸控制器的命令发送
 */
void TP_Write_Byte(uint8_t num)
{
    HAL_SPI_Transmit(&hspi2, &num, 1, 1000);
}

/**
 * @brief  从触摸控制器读取ADC值
 * @param  CMD: 要发送的命令（CMD_RDX或CMD_RDY）
 * @retval 12位的ADC转换结果
 * @note   该函数通过SPI接口与XPT2046触摸控制器通信，完成以下操作：
 *          1. 拉低CS片选信号启动通信
 *          2. 发送命令字节并接收16位响应数据
 *          3. 拉高CS片选信号结束通信
 *          4. 将接收到的数据组合并转换为12位有效值
 */
uint16_t TP_Read_AD(uint8_t CMD)
{
    uint8_t tx_data = CMD;       // 要发送的命令字节
    uint8_t rx_buf[2] = {0};     // 接收缓冲区，用于存储2字节响应
    uint16_t result = 0;         // 最终返回的ADC结果

    /* 1. 激活片选信号（CS置低）启动SPI通信 */
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);

    /*
     * 2. 通过SPI发送命令并接收响应
     *    - 先发送1字节命令（tx_data）
     *    - 再接收2字节响应数据（rx_buf）
     *    - 超时时间设置为1000ms
     */
    HAL_SPI_Transmit(&hspi2, &tx_data, 1, 1000);
    HAL_SPI_Receive(&hspi2, rx_buf, 2, 1000);

    /* 3. 释放片选信号（CS置高）结束SPI通信 */
    HAL_GPIO_WritePin(CS_GPIO_Port,CS_Pin, GPIO_PIN_SET);

    /*
     * 4. 数据处理：
     *    - 将接收到的2字节数据合并为16位值
     *    - 右移4位得到有效的12位ADC值
     *    - XPT2046返回的数据格式为高位在前，有效位在bit15~bit4
     */
    result = (rx_buf[0] << 8) | rx_buf[1];
    result >>= 4;

    return result;
}

#define READ_TIMES  5   // Number of reads for filtering
#define LOST_VAL    1   // Number of outlier values to discard from each end

/**
 * @brief  读取触摸坐标(X或Y)并进行中值滤波处理
 * @param  xy: 坐标读取命令(CMD_RDX或CMD_RDY)
 * @retval 经过滤波处理后的坐标值
 * @note   该函数实现以下功能：
 *         1. 多次采样原始坐标数据(READ_TIMES次)
 *         2. 使用冒泡排序对采样值进行排序
 *         3. 去除最大和最小的LOST_VAL个离群值
 *         4. 计算剩余数据的平均值作为最终结果
 *         5. 有效抑制触摸屏采样噪声，提高坐标稳定性
 */
uint16_t TP_Read_XOY(uint8_t xy)
{
    uint16_t i, j;
    uint16_t buf[READ_TIMES];  // 采样数据缓冲区
    uint16_t sum = 0;          // 中间值求和
    uint16_t temp;             // 临时变量

    /* 第一步：多次采样原始坐标数据 */
    for (i = 0; i < READ_TIMES; i++)
    {
        buf[i] = TP_Read_AD(xy);  // 通过SPI读取原始ADC值
    }

    /* 第二步：使用冒泡排序对采样数据进行升序排列 */
    for (i = 0; i < READ_TIMES - 1; i++)
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (buf[i] > buf[j])  // 如果前值大于后值
            {
                temp = buf[i];    // 交换数据位置
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }

    /* 第三步：计算中间有效数据的平均值 */
    // 跳过前LOST_VAL个最小值和后LOST_VAL个最大值
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++)
    {
        sum += buf[i];  // 累加中间有效数据
    }
    // 计算平均值(总和/有效数据个数)
    temp = sum / (READ_TIMES - 2 * LOST_VAL);

    return temp;  // 返回滤波后的坐标值
}

/**
 * @brief  读取触摸屏原始X/Y坐标值
 * @param  x: 指向存储X坐标值的指针
 * @param  y: 指向存储Y坐标值的指针
 * @retval 1表示读取成功，0表示读取失败
 * @note   该函数实现以下功能：
 *          1. 调用TP_Read_XOY()分别读取X轴和Y轴的ADC原始值
 *          2. 通过中值滤波算法获取稳定的坐标值
 *          3. 将结果通过指针参数返回给调用者
 *          4. 当前版本固定返回1(成功)，后续可扩展错误检测
 */
uint8_t TP_Read_XY(uint16_t *x, uint16_t *y)
{
    uint16_t xtemp, ytemp;

    // 读取X轴坐标值(使用CMD_RDX命令)
    xtemp = TP_Read_XOY(CMD_RDX);

    // 读取Y轴坐标值(使用CMD_RDY命令)
    ytemp = TP_Read_XOY(CMD_RDY);

    // 通过指针参数返回坐标值
    *x = xtemp;
    *y = ytemp;

    // 固定返回成功状态
    return 1;
}

#define ERR_RANGE 50 // Tolerance for consecutive reads

/**
 * @brief  双重读取触摸坐标并进行一致性校验
 * @param  x: 指向存储最终X坐标的指针
 * @param  y: 指向存储最终Y坐标的指针
 * @retval 1表示读取成功且数据一致，0表示读取失败或数据不一致
 * @note   该函数实现以下功能：
 *          1. 连续两次调用TP_Read_XY()获取坐标数据
 *          2. 检查两次读取的坐标差值是否在允许范围内(ERR_RANGE)
 *          3. 若数据一致则计算平均值作为最终坐标
 *          4. 提供更可靠的触摸坐标数据，防止单次读取的偶然误差
 */
uint8_t TP_Read_XY2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;  // 第一次读取的X/Y坐标
    uint16_t x2, y2;  // 第二次读取的X/Y坐标

    /* 连续两次读取触摸坐标 */
    if (TP_Read_XY(&x1, &y1) && TP_Read_XY(&x2, &y2))
    {
        /* 检查两次读取的坐标差值是否在允许范围内 */
        if (abs(x1 - x2) < ERR_RANGE && abs(y1 - y2) < ERR_RANGE)
        {
            /* 计算平均值作为最终坐标 */
            *x = (x1 + x2) / 2;
            *y = (y1 + y2) / 2;
            return 1;  // 返回成功状态
        }
    }
    return 0;  // 返回失败状态
}

/**
 * @brief  扫描触摸屏按压状态并处理坐标数据
 * @param  tp: 坐标模式选择
 *            - 0: 返回经过校准的屏幕坐标
 *            - 1: 返回原始物理坐标
 * @retval 触摸状态
 *            - 1: 触摸屏被按压
 *            - 0: 触摸屏未被按压
 * @note   该函数实现以下功能：
 *          1. 检测触摸笔按压状态（通过PEN引脚）
 *          2. 根据模式读取原始或校准后的坐标
 *          3. 管理触摸状态标志位（首次按压/持续按压/释放）
 *          4. 维护坐标数据缓存（当前坐标和初始坐标）
 */
uint8_t TP_Scan(uint8_t tp)
{
    // 检测触摸笔按压状态（低电平表示按压）
    if (HAL_GPIO_ReadPin(T_pen_GPIO_Port, T_pen_Pin) == GPIO_PIN_RESET)
    {
        /* 按压状态下的坐标处理 */
        if (tp) // 原始坐标模式直接读取
        {
            TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0]);
        }
        else if (TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0])) // 屏幕坐标模式需转换
        {
            // 使用校准参数转换坐标：x = xfac * raw_x + xoff
            tp_dev.x[0] = tp_dev.xfac * tp_dev.x[0] + tp_dev.xoff;
            tp_dev.y[0] = tp_dev.yfac * tp_dev.y[0] + tp_dev.yoff;
        }

        /* 触摸状态机管理 */
        // 检测是否为新的按压动作（之前未被按压）
        if ((tp_dev.sta & TP_PRES_DOWN) == 0)
        {
            // 设置按压标志和捕获标志（TP_CATH_PRES用于标记新按压事件）
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;
            // 保存初始按压坐标到x[4]/y[4]（用于拖动检测等）
            tp_dev.x[4] = tp_dev.x[0];
            tp_dev.y[4] = tp_dev.y[0];
        }
    }
    else // 触摸笔释放状态处理
    {
        // 如果之前是按压状态，清除所有标志位
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            tp_dev.sta = 0; // 清除TP_PRES_DOWN和TP_CATH_PRES
        }
        // 重置当前坐标为无效值（0xFFFF）
        tp_dev.x[0] = 0xFFFF;
        tp_dev.y[0] = 0xFFFF;
    }
    // 返回当前按压状态（只检查TP_PRES_DOWN标志）
    return (tp_dev.sta & TP_PRES_DOWN);
}


/******************************************************************************************/
/* LCD Drawing and Calibration Functions                                                  */
/******************************************************************************************/

/**
 * @brief  绘制触摸校准用的十字准星标记
 * @param  x: 十字中心点的X坐标
 * @param  y: 十字中心点的Y坐标
 * @param  color: 十字准星的颜色
 * @retval 无
 * @note   该函数用于触摸屏校准时绘制参考标记，包含以下元素：
 *           - 水平线（长度25像素，中心对称）
 *           - 垂直线（长度25像素，中心对称）
 *           - 中心点周围的4个对角像素点
 *           - 中心6像素半径的圆环
 *           - 设置全局绘图颜色变量g_point_color
 */
void TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color)
{
    g_point_color = color;  // 设置全局绘图颜色
    /* 绘制水平线（从x-12到x+13，共25像素长度） */
    lcd_draw_line(x - 12, y, x + 13, y, color);
    /* 绘制垂直线（从y-12到y+13，共25像素长度） */
    lcd_draw_line(x, y - 12, x, y + 13, color);

    /* 绘制中心点周围的4个对角像素点，增强中心可见性 */
    lcd_draw_point(x + 1, y + 1, color);  // 右下
    lcd_draw_point(x - 1, y + 1, color);  // 左下
    lcd_draw_point(x + 1, y - 1, color);  // 右上
    lcd_draw_point(x - 1, y - 1, color);  // 左上

    /* 绘制中心6像素半径的圆环，作为更明显的视觉参考 */
    lcd_draw_circle(x, y, 6, color);
}

/**
 * @brief  绘制一个2x2像素的大点（方形点）
 * @param  x: 点左上角的X坐标
 * @param  y: 点左上角的Y坐标
 * @param  color: 点的颜色值（16位RGB565格式）
 * @retval 无
 * @note   该函数用于在触摸屏测试或绘图时绘制更显眼的点标记，特点包括：
 *           - 设置全局绘图颜色变量g_point_color
 *           - 在指定坐标(x,y)处绘制4个相邻像素组成2x2方块
 *           - 比单像素点更醒目，适合作为触摸反馈或标记点
 */
void TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color)
{
    g_point_color = color;  // 设置全局绘图颜色变量
    lcd_draw_point(x, y, color);         // 绘制左上角像素
    lcd_draw_point(x + 1, y, color);     // 绘制右上角像素
    lcd_draw_point(x, y + 1, color);     // 绘制左下角像素
    lcd_draw_point(x + 1, y + 1, color); // 绘制右下角像素
}

static const char* TP_REMIND_MSG_TBL = "Please use the stylus to click the cross on the screen.";

/**
 * @brief  执行触摸屏四点校准
 * @note   该函数已重构，移除了递归调用以提高稳定性
 * @retval 无
 *
 * @details 该函数实现触摸屏四点校准流程，包含以下步骤：
 *          1. 在屏幕四个角显示红色十字标记
 *          2. 采集用户点击四个标记点的原始坐标
 *          3. 计算并验证坐标数据的合理性
 *          4. 计算校准参数(xfac/yfac缩放因子和xoff/yoff偏移量)
 *          5. 若校准失败自动重试，成功则保存参数并返回
 */
void TP_Adjust(void)
{
    uint16_t pos_temp[4][2];  // 临时存储四个校准点的原始坐标
    uint8_t  cnt = 0;         // 已采集的校准点计数器
    uint16_t d1, d2;          // 用于计算两点间距离
    uint32_t tem1, tem2;      // 临时计算变量
    double   fac;             // 距离比例因子

    // 主校准循环，校准失败会自动重试
    for (;;)
    {
        // 初始化校准界面
        lcd_clear(WHITE);
        g_point_color = BLACK;
        // 显示操作提示信息
        lcd_show_string(40, 40, lcddev.width - 80, 100, 16, 1,(char*)TP_REMIND_MSG_TBL, BLACK);

        cnt = 0;
        // 在左上角(20,20)显示第一个红色十字标记
        TP_Drow_Touch_Point(20, 20, RED);
        tp_dev.sta = 0;    // 清除触摸状态
        tp_dev.xfac = 0;   // 标记为未校准状态

        // 采集四个校准点的循环
        while(cnt < 4)
        {
            TP_Scan(1);  // 扫描原始坐标模式
            // 检测到新的按压事件
            if ((tp_dev.sta & TP_CATH_PRES))
            {
                tp_dev.sta &= ~TP_CATH_PRES;  // 清除按压标志
                // 保存当前点的原始坐标
                pos_temp[cnt][0] = tp_dev.x[0];
                pos_temp[cnt][1] = tp_dev.y[0];
                cnt++;

                // 根据当前采集的点数切换校准点位置
                switch (cnt)
                {
                    case 1:  // 第一个点采集完成，切换到右上角
                        TP_Drow_Touch_Point(20, 20, WHITE);
                        TP_Drow_Touch_Point(lcddev.width - 20, 20, RED);
                        break;
                    case 2:  // 第二个点采集完成，切换到左下角
                        TP_Drow_Touch_Point(lcddev.width - 20, 20, WHITE);
                        TP_Drow_Touch_Point(20, lcddev.height - 20, RED);
                        break;
                    case 3:  // 第三个点采集完成，切换到右下角
                        TP_Drow_Touch_Point(20, lcddev.height - 20, WHITE);
                        TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, RED);
                        break;
                    case 4:  // 四个点采集完成，退出循环
                        break;
                }
            }
            HAL_Delay(10);  // 短延时降低CPU负载
        }

        /******************************************************************
         * 数据验证阶段：检查采集的四个点是否形成合理的矩形
         * 1. 检查上边(点1-2)和下边(点3-4)的长度比例
         * 2. 检查左边(点1-3)和右边(点2-4)的长度比例
         * 3. 比例超出0.95-1.05范围或距离为0则认为校准失败
         ******************************************************************/

        // 计算上边两点(点1和点2)的距离
        tem1 = abs(pos_temp[0][0] - pos_temp[1][0]);
        tem2 = abs(pos_temp[0][1] - pos_temp[1][1]);
        d1 = sqrt(tem1 * tem1 + tem2 * tem2);

        // 计算下边两点(点3和点4)的距离
        tem1 = abs(pos_temp[2][0] - pos_temp[3][0]);
        tem2 = abs(pos_temp[2][1] - pos_temp[3][1]);
        d2 = sqrt(tem1 * tem1 + tem2 * tem2);
        fac = (float)d1 / d2;
        // 验证上下边长度比例
        if (fac < 0.95 || fac > 1.05 || d1 == 0 || d2 == 0)
        {
            lcd_show_string(40, 80, lcddev.width - 80, 16, 16,1, "Error: Inconsistent distance. Retrying...", RED);
            HAL_Delay(2000);
            continue;  // 比例异常，重新校准
        }

        // 计算左边两点(点1和点3)的距离
        tem1 = abs(pos_temp[0][0] - pos_temp[2][0]);
        tem2 = abs(pos_temp[0][1] - pos_temp[2][1]);
        d1 = sqrt(tem1 * tem1 + tem2 * tem2);

        // 计算右边两点(点2和点4)的距离
        tem1 = abs(pos_temp[1][0] - pos_temp[3][0]);
        tem2 = abs(pos_temp[1][1] - pos_temp[3][1]);
        d2 = sqrt(tem1 * tem1 + tem2 * tem2);
        fac = (float)d1 / d2;
        // 验证左右边长度比例
        if (fac < 0.95 || fac > 1.05)
        {
            lcd_show_string(40, 80, lcddev.width - 80, 16, 16,1, "Error: Skewed points. Retrying...", RED);
            HAL_Delay(2000);
            continue;  // 比例异常，重新校准
        }

        /******************************************************************
         * 校准参数计算阶段：
         * 1. xfac: X轴缩放因子 = 屏幕宽度 / (点2X - 点1X)
         * 2. xoff: X轴偏移量 = (屏幕宽度 - xfac*(点2X + 点1X))/2
         * 3. yfac: Y轴缩放因子 = 屏幕高度 / (点3Y - 点1Y)
         * 4. yoff: Y轴偏移量 = (屏幕高度 - yfac*(点3Y + 点1Y))/2
         ******************************************************************/
        tp_dev.xfac = (float)(lcddev.width - 40) / (pos_temp[1][0] - pos_temp[0][0]);
        tp_dev.xoff = (lcddev.width - tp_dev.xfac * (pos_temp[1][0] + pos_temp[0][0])) / 2;

        tp_dev.yfac = (float)(lcddev.height - 40) / (pos_temp[2][1] - pos_temp[0][1]);
        tp_dev.yoff = (lcddev.height - tp_dev.yfac * (pos_temp[2][1] + pos_temp[0][1])) / 2;

        // 检查方向是否正确(缩放因子绝对值不应大于2)
        if (fabs(tp_dev.xfac) > 2 || fabs(tp_dev.yfac) > 2)
        {
            lcd_show_string(40, 80, lcddev.width - 80, 16, 16, 1,"Error: Wrong orientation? Retrying...", RED);
            HAL_Delay(2000);
            continue;  // 方向异常，重新校准
        }

        // 校准成功，显示提示信息
        lcd_clear(WHITE);
        lcd_show_string(35, 110, lcddev.width - 70, 30, 16, 1,"Touch Screen Adjust OK!", BLUE);
        HAL_Delay(100);
        return;  // 校准完成，退出函数
    }
}

/**
 * @brief  初始化触摸屏控制器
 * @retval 返回值说明
 *            - 0: 不需要校准(电容屏)
 *            - 1: 已执行校准(电阻屏)
 * @note   该函数实现以下功能：
 *          1. 根据LCD方向调整触摸坐标读取命令(解决横竖屏物理坐标与逻辑坐标匹配问题)
 *          2. 区分电容屏和电阻屏进行不同初始化处理
 *          3. 电阻屏自动执行四点校准流程
 *          4. 设置触摸屏类型标志位(touchtype)
 */
uint8_t TP_Init(void)
{
    /********************************************************************************
     *                                IMPORTANT FIX
     * 重要修正：根据LCD显示方向调整触摸坐标读取命令
     * 当LCD处于横屏模式时，触摸面板的物理X/Y轴与屏幕逻辑轴需要交换
     * 通过交换读取X/Y坐标的SPI命令来补偿这种差异
     ********************************************************************************/
    if (lcddev.dir == 1) // 横屏模式
    {
        CMD_RDX = 0x90; // 读取物理Y轴作为逻辑X坐标
        CMD_RDY = 0xD0; // 读取物理X轴作为逻辑Y坐标
    }
    else // 竖屏模式
    {
        CMD_RDX = 0xD0; // 默认：读取物理X轴作为逻辑X坐标
        CMD_RDY = 0x90; // 默认：读取物理Y轴作为逻辑Y坐标
    }

    /* 电容屏处理分支 */
    // 检查是否为电容式触摸屏(根据LCD控制器ID判断)
    if (lcddev.id == 0x5510 || lcddev.id == 0x9806)
    {
        // 注：实际实现中这里会初始化具体电容触摸控制器
        // if (GT9147_Init() == 0) {
        //     tp_dev.scan = GT9147_Scan;
        // } else {
        //     OTT2001A_Init();
        //     tp_dev.scan = OTT2001A_Scan;
        // }

        tp_dev.touchtype |= 0x80; // 设置触摸类型为电容式(bit7)
        tp_dev.touchtype |= (lcddev.dir & 0x01); // 保存当前方向(bit0)
        return 0; // 电容屏不需要校准
    }
    /* 电阻屏处理分支 */
    else
    {
        // 注：实际实现中这里会从EEPROM加载校准数据
        // if (TP_Get_Adjdata()) return 0;

        // 如果没有校准数据，执行四点校准流程
        TP_Adjust();
        // 注：实际实现中校准数据会保存到EEPROM
        // TP_Save_Adjdata();
        return 1; // 返回1表示已执行校准
    }
}

/**
 * @brief  加载绘图提示界面并绘制退出按钮
 * @retval 无
 * @note   该函数实现触摸屏绘图测试的初始化界面，包含以下功能：
 *          1. 清屏并设置默认绘图颜色
 *          2. 在屏幕左上角显示操作提示文本
 *          3. 在屏幕右上角绘制红色退出按钮（带白色"EXIT"文字）
 *          4. 按钮设计规范：
 *              - 按钮宽度60像素，高度25像素
 *              - 右侧保留5像素边距
 *              - 文字水平居中（通过x坐标偏移15像素实现）
 *          5. 该界面用于rtp_test()函数的触摸绘图测试
 */
// 为按钮UI元素定义宏，便于维护和修改
#define EXIT_BTN_WIDTH   60
#define EXIT_BTN_HEIGHT  25
#define EXIT_BTN_MARGIN  5

void load_draw_hint(void)
{

    lcd_set_window(0, 0, lcddev.width, lcddev.height);

    /* 1. 初始化界面 */
    lcd_clear(WHITE);      // 清屏为白色背景
    g_point_color = BLACK; // 设置默认绘图颜色为黑色

    /* 2. 在左上角显示操作提示 */
    lcd_show_string(10, 5, 200, 16, 16, 1, "Touch to draw...", BLACK);

    /* 3. 在右上角绘制退出按钮 */
    // 计算按钮的左上角坐标
    uint16_t btn_x = lcddev.width - EXIT_BTN_WIDTH - EXIT_BTN_MARGIN;
    uint16_t btn_y = EXIT_BTN_MARGIN;

    // 绘制红色按钮背景
    lcd_fill(btn_x, btn_y, btn_x + EXIT_BTN_WIDTH, btn_y + EXIT_BTN_HEIGHT, RED);

    // 计算"EXIT"文字的坐标，使其在按钮内水平和垂直居中
    const char* exit_text = "EXIT";
    uint8_t font_size = 16;
    uint16_t text_width = strlen(exit_text) * (font_size / 2); // 字体宽度 = 字符数 * (字号/2)
    uint16_t text_height = font_size;                          // 字体高度 = 字号

    uint16_t text_x = btn_x + (EXIT_BTN_WIDTH - text_width) / 2;
    uint16_t text_y = btn_y + (EXIT_BTN_HEIGHT - text_height) / 2;

    // 在按钮中央绘制白色"EXIT"文字
    // 使用叠加模式(mode=0)效率更高，因为背景已由lcd_fill填充好
    lcd_show_string(text_x, text_y, EXIT_BTN_WIDTH, EXIT_BTN_HEIGHT, font_size, 1, (char*)exit_text, WHITE);
}
/**
 * @brief  电阻触摸屏测试函数（带退出机制）
 * @retval 无
 * @note   该函数实现触摸屏绘图测试功能，包含以下特性：
 *          1. 初始化绘制操作提示界面和退出按钮
 *          2. 实时检测触摸状态并处理两种事件：
 *             - 初始按压事件（TP_CATH_PRES标志）：用于检测退出按钮点击
 *             - 持续按压状态：用于连续绘制触摸轨迹
 *          3. 智能区域处理：
 *             - 退出按钮区域(右上角60x25像素)不响应绘图
 *             - 屏幕边界检查确保坐标有效
 *          4. 使用2x2大像素点绘制触摸轨迹，增强视觉效果
 */
void rtp_test(void)
{
    /* 定义退出按钮区域坐标（基于load_draw_hint()中的绘制位置） */
    uint16_t exit_btn_x1 = lcddev.width - 60 - 5;  // 按钮左上角X坐标（屏幕宽度-60-5边距）
    uint16_t exit_btn_y1 = 5;                      // 按钮左上角Y坐标（顶部5像素）
    uint16_t exit_btn_x2 = exit_btn_x1 + 60;       // 按钮右下角X坐标
    uint16_t exit_btn_y2 = exit_btn_y1 + 25;       // 按钮右下角Y坐标

    /* 初始化界面：绘制操作提示和退出按钮 */
    load_draw_hint();

    /* 主测试循环 */
    while (1)
    {
        /* 检测当前触摸状态（参数0表示使用校准后的屏幕坐标） */
        if (TP_Scan(0))
        {
            /*--- 事件处理：检测初始按压事件（仅触发一次）---*/
            if (tp_dev.sta & TP_CATH_PRES)
            {
                tp_dev.sta &= ~TP_CATH_PRES;  // 清除按压捕获标志

                /* 检查是否点击了退出按钮 */
                if (tp_dev.x[0] > exit_btn_x1 && tp_dev.x[0] < exit_btn_x2 &&
                    tp_dev.y[0] > exit_btn_y1 && tp_dev.y[0] < exit_btn_y2)
                {
                    return;  // 退出测试函数
                }
            }

            /*--- 状态处理：持续按压时绘制轨迹 ---*/
            /* 确保坐标在屏幕有效范围内 */
            if (tp_dev.x[0] < lcddev.width && tp_dev.y[0] < lcddev.height)
            {
                /* 避开按钮区域绘制 */
                if (!(tp_dev.x[0] > exit_btn_x1 && tp_dev.x[0] < exit_btn_x2 &&
                      tp_dev.y[0] > exit_btn_y1 && tp_dev.y[0] < exit_btn_y2))
                {
                    TP_Draw_Big_Point(tp_dev.x[0], tp_dev.y[0], BLUE);  // 绘制2x2蓝色点
                }
            }
        }
        HAL_Delay(1);  // 短延时降低CPU占用
    }
}