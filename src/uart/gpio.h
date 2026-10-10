#ifndef __GPIO_H
#define __GPIO_H

#include <stdint.h>
#include <stdbool.h>

// gpio方向
enum gpio_direction
{
    GPIO_IN = 0,  // GPIO输入
    GPIO_OUT = 1, // GPIO输出
};

// gpio电平值
enum gpio_value
{
    GPIO_LOW = 0,  // GPIO低电平
    GPIO_HIGH = 1, // GPIO高电平
};

// gpio中断输入, 边沿值
enum gpio_edge
{
    GPIO_NONE = 0,    // 不是中断
    GPIO_RISING = 1,  // 中断输入, 上升沿
    GPIO_FALLING = 2, // 中断输入, 下降沿
    GPIO_BOTH = 3,    // 中断输入, 上升沿和下降沿
};

/**
 * @brief  导出GPIO到用户空间
 * @param  gpio_num: 输入参数, 待导出的GPIO编号
 * @return 成功: 0
 *         失败: -1
 */
int gpio_export(const uint16_t gpio_num);

/**
 * @brief  取消导出到用户空间的GPIO
 * @param  gpio_num: 输入参数, 待取消导出的GPIO编号
 * @return 成功: 0
 *         失败: -1
 */
int gpio_unexport(const uint16_t gpio_num);

/**
 * @brief  设置GPIO方向
 * @param  gpio_num : 输入参数, 待设置的GPIO编号
 * @param  direction: 输入参数, 待设置的GPIO方向
 * @return 成功: 0
 *         失败: -1
 */
int gpio_set_direction(const uint16_t gpio_num,
                       const enum gpio_direction direction);

/**
 * @brief  设置GPIO输出电平值
 * @param  gpio_num: 输入参数, 待设置的GPIO编号
 * @param  value   : 输入参数, 待设置的GPIO电平值
 * @return 成功: 0
 *         失败: -1
 */
int gpio_set_value(const uint16_t gpio_num, const enum gpio_value value);

/**
 * @brief  获取GPIO的电平值
 * @param  value   : 输出参数, GPIO电平值
 * @param  gpio_num: 输入参数, GPIO编号
 * @return 成功: 0
 *         失败: -1
 */
int gpio_get_value(enum gpio_value *value, const uint16_t gpio_num);

/**
 * @brief  设置GPIO触发边沿
 * @param  gpio_num: 输入参数, 待设置的GPIO编号
 * @param  edge    : 输入参数, 待设置的GPIO边沿
 * @return 成功: 0
 *         失败: -1
 */
int gpio_set_edge(const uint16_t gpio_num, const enum gpio_edge edge);

/**
 * @brief  打开GPIO设备
 * @param  gpio_num: 输入参数, 待打开的GPIO编号
 * @return 成功: 设备文件描述符
 *         失败: -1
 */
int gpio_open(const uint16_t gpio_num);

/**
 * @brief  关闭GPIO设备
 * @param  fd: 输入参数, 待关闭的GPIO设备文件描述符
 * @return 成功: 0
 *         失败: -1
 */
int gpio_close(const int fd);

int gpio_simple_init(int gpio_num, enum gpio_direction dir);
#endif // __GPIO_H
