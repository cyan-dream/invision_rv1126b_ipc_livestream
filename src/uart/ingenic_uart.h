#ifndef __UART_H
#define __UART_H

#include <stdint.h>
#include <termios.h>
#ifdef __cplusplus
extern "C" {
#endif
enum uart_baud_rate
{
    BAUD_0 = B0,
    BAUD_50 = B50,
    BAUD_75 = B75,
    BAUD_110 = B110,
    BAUD_134 = B134,
    BAUD_150 = B150,
    BAUD_200 = B200,
    BAUD_300 = B300,
    BAUD_600 = B600,
    BAUD_1200 = B1200,
    BAUD_1800 = B1800,
    BAUD_2400 = B2400,
    BAUD_4800 = B4800,
    BAUD_9600 = B9600,
    BAUD_19200 = B19200,
    BAUD_38400 = B38400,
    BAUD_57600 = B57600,
    BAUD_115200 = B115200,
    BAUD_230400 = B230400,
    BAUD_460800 = B460800,
    BAUD_500000 = B500000,
    BAUD_576000 = B576000,
    BAUD_921600 = B921600,
    BAUD_1000000 = B1000000,
    BAUD_1152000 = B1152000,
    BAUD_1500000 = B1500000,
    BAUD_2000000 = B2000000,
    BAUD_2500000 = B2500000,
    BAUD_3000000 = B3000000,
    BAUD_3500000 = B3500000,
    BAUD_4000000 = B4000000,
};

/**
 * @brief  打开串口
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @param  baud_rate  : 输入参数, 波特率
 * @param  data_bit   : 输入参数, 数据位
 * @param  parity_bit : 输入参数, 奇偶检验位, 默认为无校验'n'或'N'
 * @param  stop_bit   : 输入参数, 停止位, 默认为1位停止位
 * @return 成功: 0
 *         失败: -1
 */
int open_uart(const char *uart_name, const enum uart_baud_rate baud_rate, int data_bit, 
                int parity_bit, int stop_bit);
/**
 * @brief  关闭串口
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int close_uart(const char *uart_name);

/**
 * @brief  清空串口输入缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_input_data(const char *uart_name);

/**
 * @brief  清空串口输出缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_output_data(const char *uart_name);

/**
 * @brief  清空串口输入和输出缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_data(const char *uart_name);

/**
 * @brief  串口发送数据
 * @param  uart_name  : 输入参数, 串口名(例: /dev/ttyS1)
 * @param  send_data    : 输入参数, 发送数据
 * @param  send_data_size: 输入参数, 发送数据长度
 * @return 成功: 0
 *         失败: -1
 */
int uart_write_data(const char *uart_name, unsigned char *send_data,
                        int send_data_size);

/**
 * @brief  串口接收数据
 * @param  recv_data    : 输出参数, 接收数据
 * @param  uart_name  : 输入参数, 串口名(例: /dev/ttyS1)
 * @param  recv_data_len: 输入参数, 接收数据长度
 * @param  timeout:     : 输入参数, 接收超时(单位：ms)
 * @return 成功: 实际接收数据长度
 *         失败: -1
 */
int uart_read_data(const char *uart_name, unsigned char *recv_data,
                        int recv_data_size, int timeout);

#ifdef __cplusplus
}
#endif                        
#endif // __UART_H
