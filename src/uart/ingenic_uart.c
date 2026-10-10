#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <pthread.h>
#include "ingenic_uart.h"
// 串口名最大长度
#define UART_NAME_MAX_SIZE 15

// 串口最大数量
#define UART_MAX_NUM 10

// 记录串口信息
struct uart_info {
    char uart_name[UART_NAME_MAX_SIZE];   // 串口名
    int uart_fd;                         // 串口文件描述符
    pthread_mutex_t uart_mutex;          // 串口互斥锁
};

// 记录串口信息索引
static uint8_t get_uart_info_index = 0;
// 记录串口信息
static struct uart_info get_uart_info[UART_MAX_NUM];

/**
 * @brief  查找指定串口的信息
 * @param  info       : 输出参数, 查找到的串口信息
 * @param  uart_name: 输入参数, 待查找的串口名
 * @return 成功: 0
 *         失败: -1
 */
static int find_uart_info(struct uart_info *info, const char *uart_name)
{
    int ret = -1;
    for (uint8_t i = 0; i < get_uart_info_index; i++) {
        ret = memcmp(get_uart_info[i].uart_name,
                     uart_name, strlen(uart_name));
        if (0 == ret) {
            memcpy(info, &get_uart_info[i], sizeof(struct uart_info));
            return 0;
        }
    }
    return -1;
}

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
                int parity_bit, int stop_bit)
{
    int fd = -1;
    int ret = -1;
    struct uart_info info;     // 串口信息
    struct termios option = {0}; // 串口属性结构体

    // 超过支持的串口数量, 直接返回错误
    if (get_uart_info_index > UART_MAX_NUM)
        return -1;

    // 串口已打开, 先关闭串口
    ret = find_uart_info(&info, uart_name);
    if (0 == ret)
        close_uart(info.uart_name);

    fd = open(uart_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
        return -1;

    // 获取终端属性
    tcgetattr(fd, &option);
    // 设置输入波特率
    cfsetspeed(&option, baud_rate);
    // 设置输出波特率
    cfsetospeed(&option, baud_rate);
    
    option.c_cflag &= ~CSIZE;
    switch (data_bit) {
	case 5:
		option.c_cflag |= CS5;
		break;
	case 6:
		option.c_cflag |= CS6;
		break;
	case 7:
		option.c_cflag |= CS7;
		break;
	case 8:
		option.c_cflag |= CS8;
		break;
	default:
		option.c_cflag |= CS8;
		break;
	}

    switch (parity_bit) {
    case 'n':
        option.c_cflag &= ~PARENB;
        option.c_iflag &= ~INPCK;
        break;
    case 'o':
        option.c_cflag |= (PARODD | PARENB);
        option.c_iflag |= INPCK;
        break;
    case 'e':
        option.c_cflag |= PARENB;
        option.c_cflag &= ~PARODD;
        option.c_iflag |= INPCK;
        break;
    default:
        option.c_cflag &= ~PARENB;
        option.c_iflag &= ~INPCK;
        break;
    }

    switch (stop_bit) {
    case 1:
        option.c_cflag &= ~CSTOPB;
        break;
    case 2:
        option.c_cflag |= CSTOPB;
        break;
    default:
        option.c_cflag &= ~CSTOPB;
        break;
    }
    
    // 一般必设置的标志
    option.c_cflag |= (CLOCAL | CREAD);
    option.c_oflag &= ~(OPOST);
    option.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    option.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF | IXANY);

    // 清空输入输出缓冲区
    tcflush(fd, TCIOFLUSH);

    // 设置最小接收字符数和超时时间
    // 当MIN=0, TIME=0时, 如果有数据可用, 则read最多返回所要求的字节数,
    // 如果无数据可用, 则read立即返回0
    option.c_cc[VMIN] = 0;
    option.c_cc[VTIME] = 0;

    // 设置终端属性
    ret = tcsetattr(fd, TCSANOW, &option);
    if (ret < 0) {
        close(fd);
        return -1;
    }

    // 记录串口信息
    memcpy(get_uart_info[get_uart_info_index].uart_name,
           uart_name, strlen(uart_name));
    get_uart_info[get_uart_info_index].uart_fd = fd;

    // 初始化互斥锁
    pthread_mutex_init(&get_uart_info[get_uart_info_index].uart_mutex, NULL);

    get_uart_info_index++;

    return 0;
}

/**
 * @brief  关闭串口
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int close_uart(const char *uart_name)
{
    int ret = -1;

    for (uint8_t i = 0; i < get_uart_info_index; i++) {
        ret = memcmp(get_uart_info[i].uart_name,
                     uart_name, strlen(uart_name));
        // 当前串口已打开
        if (0 == ret) {
            // 关闭串口
            ret = close(get_uart_info[i].uart_fd);
            if (ret < 0)
                return -1;

            // 清空串口信息
            get_uart_info[i].uart_fd = -1;
            memset(get_uart_info[i].uart_name, 0, UART_NAME_MAX_SIZE);

            // 销毁互斥锁
            pthread_mutex_destroy(&get_uart_info[i].uart_mutex);

            // 将串口信息放到数组最前面
            memcpy(&get_uart_info[i], &get_uart_info[i + 1],
                   (sizeof(struct uart_info) * (UART_MAX_NUM - i - 1)));

            (get_uart_info_index > 0) ? get_uart_info_index-- : 0;

            return 0;
        }
    }
    return 0;
}

/**
 * @brief  清空串口输入缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_input_data(const char *uart_name)
{
    int ret = -1;
    struct uart_info info; // 串口信息

    // 串口未打开, 直接返回成功
    ret = find_uart_info(&info, uart_name);
    if (0 != ret) {
        return 0;
    }

    ret = tcflush(info.uart_fd, TCIFLUSH);
    return ret;
}

/**
 * @brief  清空串口输出缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_output_data(const char *uart_name)
{
    int ret = -1;
    struct uart_info info; // 串口信息

    // 串口未打开, 直接返回成功
    ret = find_uart_info(&info, uart_name);
    if (0 != ret)
        return 0;

    ret = tcflush(info.uart_fd, TCOFLUSH);
    return ret;
}

/**
 * @brief  清空串口输入和输出缓存
 * @param  uart_name: 输入参数, 串口名(例: /dev/ttyS1)
 * @return 成功: 0
 *         失败: -1
 */
int uart_clear_data(const char *uart_name)
{
    int ret = -1;
    struct uart_info info; // 串口信息

    // 串口未打开, 直接返回成功
    ret = find_uart_info(&info, uart_name);
    if (0 != ret)
        return 0;

    ret = tcflush(info.uart_fd, TCIOFLUSH);
    return ret;
}

/**
 * @brief  串口发送数据
 * @param  uart_name  : 输入参数, 串口名(例: /dev/ttyS1)
 * @param  send_data    : 输入参数, 发送数据
 * @param  send_data_size: 输入参数, 发送数据长度
 * @return 成功: 0
 *         失败: -1
 */
int uart_write_data(const char *uart_name, unsigned char *send_data,
                      int send_data_size)
{
    int ret = -1;
    struct uart_info info; // 串口信息

    // 串口未打开, 直接返回失败
    ret = find_uart_info(&info, uart_name);
    if (0 != ret)
        return -1;

    pthread_mutex_lock(&info.uart_mutex);

    ret = write(info.uart_fd, send_data, send_data_size);
    if (send_data_size != ret) {
        pthread_mutex_unlock(&info.uart_mutex);
        return -1;
    }

    // 解锁
    pthread_mutex_unlock(&info.uart_mutex);
    return 0;
}

/**
 * @brief  串口接收数据
 * @param  recv_data    : 输出参数, 接收数据
 * @param  uart_name  : 输入参数, 串口名(例: /dev/ttyS1)
 * @param  recv_data_size: 输入参数, 接收数据长度
 * @param  timeout:     : 输入参数, 接收超时(单位：ms)
 * @return 成功: 实际接收数据长度
 *         失败: -1
 */
int uart_read_data(const char *uart_name, unsigned char *recv_data, int recv_data_size, int timeout)
{
    int ret = -1;
    nfds_t nfds = 1;         // 指定fds数组中的项目数
    struct pollfd fds[1];    // 指定要监视的文件描述符集
    int total_data_len = 0;  // 已读取数据长度
    int remain_data_len = 0; // 未读取数据长度
    struct uart_info info; // 串口信息

    // 串口未打开, 直接返回失败
    ret = find_uart_info(&info, uart_name);
    if (0 != ret) {
        printf("uart is close\n");
        return -1;
    }
        

    //memset(recv_data, 0, recv_data_size);

    remain_data_len = recv_data_size;

    // 加锁
    pthread_mutex_lock(&info.uart_mutex);

    while (1)
    {
        // 设置需要监听的文件描述符
        memset(fds, 0, sizeof(fds));
        fds[0].fd = info.uart_fd;
        fds[0].events = POLLIN;

        ret = poll(fds, nfds, timeout);
        // 返回负值, 发生错误
        if (ret < 0) {
            printf("poll err\n");
            pthread_mutex_unlock(&info.uart_mutex);
            return -1;
        } else if (0 == ret) {// 返回0, 超时
            // 如果超时后, 已读取数据长度大于0, 返回实际接收数据长度
            //printf("uart recv time out\n");
            //if (total_data_len > 0) {
            //    pthread_mutex_unlock(&info.uart_mutex);
            //    return total_data_len;
            //}
            pthread_mutex_unlock(&info.uart_mutex);
            return 0;
        } else {  // 返回值大于0, 成功
            // 判断是否是期望的返回
            if (fds[0].revents & POLLIN) {
                // 从文件起始位置开始读数据
                lseek(fds[0].fd, 0, SEEK_SET);
                ret = read(fds[0].fd, &recv_data[total_data_len],
                           remain_data_len);
                if (ret < 0) {
                    printf("read err\n");
                    pthread_mutex_unlock(&info.uart_mutex);
                    return -1;
                }
                // 计算已读取数据长度
                total_data_len += ret;
                // 计算剩余需要读取长度
                //remain_data_len = (recv_data_size - total_data_len);
                // 读取完毕
                //if (total_data_len == recv_data_size)
                    break;
            }
        }
    }
    pthread_mutex_unlock(&info.uart_mutex);

    return total_data_len;
}
