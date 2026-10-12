#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>                      // 下面四个头文件是linux系统编程特有的
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/videodev2.h>                // 操作摄像头设备
#include <semaphore.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include "ipc.h"
#include "ingenic_uart.h"
#include "kv_sqlite3.h"
#include "rtsp_server_handle.h"

#define   WIDTH   720                     // 图片的宽度
#define   HEIGHT  576                       // 图片的高度
#define   FMT     V4L2_PIX_FMT_UYVY         // 图片格式
#define   COUNT   3                         // 缓冲区个数
#define FONT_FILE_PATH "/oem/font/MSYH.TTC"

extern struct ipc_config ipc_config;
int running = 1;
int interlaced = 0;
int is_h265 = 1;
sem_t empty;
sem_t full;
sem_t sem_snap;


extern void cleanup_stb_truetype(void);
extern int init_stb_truetype(const char *font_path, int font_size);

                         
//extern sqlite3 *db;
// 信号处理函数，用于捕获Ctrl+C退出信号
void set_threadExit(void);
void sigint_handler(int signo) {
    printf("\n准备退出程序...\n");
    running = 0;
    set_threadExit();
    rtspStopServer();
    return;
}
extern int is_record;
static void sigtimer_handler(int signum) {
    //printf("定时器触发！\n");
	is_record = 0;
   	return;
}

extern struct video_buffer *h264enc_video_buffers;
extern struct video_buffer *jpegenc_video_buffers;
int parse_args(int argc, char **argv)
{ 
    int opt;
    while ((opt = getopt(argc, argv, "hi:")) != -1) {
        switch (opt) {
            case 'h': 
                printf("-h: 显示帮助信息\n");
                printf("-i <0/1>: 是否隔行扫描\n");
                exit(0);
            case 'i': 
                interlaced = atoi(optarg); 
                break;
            case '?':
                printf("invalid args!\n");
                exit(1);
            default: 
                printf("invalid args!\n");
                exit(1);    
        }
    }
    return 0;
}
extern int is_snap;
int extract_nv12_from_yuyv(unsigned char* psrc, int interlaced, unsigned char* pdst)
{
    unsigned char* yptr;
	unsigned char* uvptr;

    yptr = pdst;
	uvptr = pdst+(WIDTH*HEIGHT);        
    

    int lpitch = (WIDTH<< 1);
	unsigned char* psrc2;
	if (interlaced) psrc2 = psrc + lpitch * (HEIGHT>>1);
    else psrc2 = psrc + lpitch;
	int i, j;

    for (j = 0; j < HEIGHT; j+=2) {
        for (i = 0; i < WIDTH; i+=2) {
            uvptr[i] = psrc[(i<<1)+1];
            uvptr[i+1] = psrc[(i<<1)+3];

            yptr[i] = psrc[i<<1];
            yptr[i+1] = psrc[(i<<1)+2];
        }
        uvptr += WIDTH;
        yptr += WIDTH;
        if (interlaced) psrc += lpitch;
        else psrc += (lpitch<<1);  

        for (i = 0; i < WIDTH; i+=2) {
            yptr[i] = psrc2[i<<1];
            yptr[i+1] = psrc2[(i<<1)+2];
        }
        yptr += WIDTH;
        if (interlaced) psrc2 += lpitch;
        else psrc2 += (lpitch<<1);
    }
    return 0;
}


int fd;
unsigned char *datas[COUNT];            // 缓冲区数据地址
struct v4l2_buffer buff;

extern int rk_mpi_venc_test(void); 
extern void* rtsp_server_thread(void *arg);
void *get_frame_thread(void *arg)
{

    int ret, i;


    fd = open("/dev/video13", O_RDWR);       // 注意查看摄像头设备名
    if (-1 == fd)
    {
        perror("open device");
        return NULL;
    }

    /* 第二步：设置捕捉图片帧格式 */
    struct v4l2_format format;
#if 1    
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;   // 关键：使用 MPLANE
    format.fmt.pix_mp.width = WIDTH;
    format.fmt.pix_mp.height = HEIGHT;
    format.fmt.pix_mp.pixelformat = FMT;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;           // 通常设为 NONE
    format.fmt.pix_mp.num_planes = 1;  
#else
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 操作类型为获取图片
    format.fmt.pix.width = WIDTH;               // 图片的宽度
    format.fmt.pix.height = HEIGHT;             // 图片的高度
    format.fmt.pix.pixelformat = FMT;// 图片格式
#endif
    ret = ioctl(fd, VIDIOC_S_FMT, &format);     // 进行设置(Set)
    if (-1 == ret)
    {
        perror("ioctl VIDIOC_S_FMT");
        close(fd);
        return NULL;
    }
    
    /* 第三步：检查是否设置成功 */
    ret = ioctl(fd, VIDIOC_G_FMT, &format);     // Get
    if (-1 == ret)
    {
        perror("ioctl VIDIOC_G_FMT");
        close(fd);
        return NULL;
    }
    if (format.fmt.pix.pixelformat == FMT)
    {
        printf("ioctl VIDIOC_S_FMT sucessful\n");
        printf("实际分辨率: %dx%d\n", format.fmt.pix.width, format.fmt.pix.height);
    }
    else
    {
        printf("ioctl VIDIOC_S_FMT failed\n");
        close(fd);
        return NULL;
    }

    /* 第四步：让摄像头驱动申请存放图像数据的缓冲区 */
    struct v4l2_requestbuffers reqbuf;
#if 1
    reqbuf.count = COUNT;                       // 缓冲区个数
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;  // 缓冲区类型
    reqbuf.memory = V4L2_MEMORY_MMAP;           // 缓冲区的用途：用于内存映射
#else
    reqbuf.count = COUNT;                       // 缓冲区个数
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 缓冲区类型
    reqbuf.memory = V4L2_MEMORY_MMAP;           // 缓冲区的用途：用于内存映射
#endif
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (-1 == ret)
    {
        perror("ioctl VIDIOC_REQBUFS");
        close(fd);
        return NULL;
    }

    /* 第五步：查询每个缓冲区的信息，同时进行内存映射 */
#if 1
    struct v4l2_plane planes[1];   // 因为 num_planes = 1

    buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buff.memory = V4L2_MEMORY_MMAP;
    buff.length = 1;             // planes 数组元素个数
    buff.m.planes = planes;      // 指向 planes 数组
#else
    struct v4l2_buffer buff;
    buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buff.memory = V4L2_MEMORY_MMAP;
#endif
    for (i = 0; i < COUNT; i++)
    {
        buff.index = i;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buff);
        if (-1 == ret)                          // 操作失败
        {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return NULL;
        }
        
        /* 打印缓冲区的长度和偏移量 */
        //printf("buf[%d]: len = %d offset: %d\n", i, buff.length, buff.m.offset);
        
        /* 把每块缓冲区映射到当前进程来 */
#if 1        
        datas[i] = (unsigned char*)mmap(NULL,
                                planes[0].length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                planes[0].m.mem_offset);
#else
         datas[i] = (unsigned char*)mmap(NULL, 
                                buff.length, 
                                PROT_READ | PROT_WRITE, 
                                MAP_SHARED, 
                                fd, 
                                buff.m.offset);
#endif                                
        if (MAP_FAILED == datas[i])             // 映射失败
        {
            perror("mmap failed");
            close(fd);
            return NULL;
        }

        /* 把映射成功的缓冲区加入到摄像头驱动的图像数据采集队列里 */
        ret = ioctl(fd, VIDIOC_QBUF, &buff);    // Queue
        if (-1 == ret)
        {
            perror("VIDIOC_QBUF");
            close(fd);
            return NULL;
        }
    }
    printf("stream start\n");
    /* 第六步：启动采集 */
#if 1
    int on = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;       // 设置启动标志位
#else
    int on = V4L2_BUF_TYPE_VIDEO_CAPTURE;       // 设置启动标志位
#endif
    ret = ioctl(fd, VIDIOC_STREAMON, &on);      // 开启摄像头流
    if (-1 == ret)
    {
        perror("ioctl VIDIOC_STREAMON");
        close(fd);
        return NULL;
    }
    rk_mpi_venc_test();
    /* 第七步：循环获取帧数据 */
/*    
    while (running)
    {  

        fd_set read_fds;
        int r;
        struct timeval timeout = { 0, 500000 };
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        r = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (r == -1) {
            if (EINTR == errno)
                continue;
            perror("select error");
            return NULL;
        }

        if (r == 0) {
            fprintf(stderr, "select timeout\n");
            continue;
        }

        if (FD_ISSET(fd, &read_fds)) {
            // 取出一个已准备好的缓冲区
            ret = ioctl(fd, VIDIOC_DQBUF, &buff);       // Dequeue
            if (-1 == ret)
            {
                perror("ioctl VIDIOC_DQBUF");
                break;
            }
            sem_wait(&empty);
            yuyv_blacken_all_region(datas[buff.index], WIDTH, HEIGHT, interlaced);
            //motion_detection(datas[buff.index], WIDTH, HEIGHT, interlaced);
            if (ipc_config.osd_state) {
                unsigned char color[3] = {255, 128, 128};
                char date[32];
                get_date(date, 1);
                overlay_text_on_yuyv(datas[buff.index], WIDTH, HEIGHT, date, 540, 25, color, interlaced);
                if (ipc_config.osd_text[0] != 0) {
                    overlay_text_on_yuyv(datas[buff.index], WIDTH, HEIGHT, ipc_config.osd_text, 10, 550, color, interlaced);
                }
	        }
            extract_nv12_from_yuyv(datas[buff.index], interlaced);
            sem_post(&full);

            // 将缓冲区重新放入队列，以便再次使用
            ret = ioctl(fd, VIDIOC_QBUF, &buff);
            if (-1 == ret)
            {
                perror("VIDIOC_QBUF");
                break;
            }
        }
             
    }
*/ 
    /* 第九步：停止采集 */
#if 1    
    on = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#else
    on = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#endif
    ioctl(fd, VIDIOC_STREAMOFF, &on);
    
    /* 第十步：释放资源 */
    for (i = 0; i < COUNT; i++)
    {
        munmap(datas[i], buff.length);  // 解除内存映射
    }
    close(fd);  // 关闭设备
    printf("get_frame_thread exit\n");
    return NULL;
}

#ifdef __cplusplus
extern "C" {
#endif
extern void* uart_init_pthread(void *arg);
#ifdef __cplusplus
}
#endif


int main(int argc, char **argv)
{	
    //datebase_init();
    init_ipc_config();
    if (init_stb_truetype(FONT_FILE_PATH, 24) != 0) {
	 	printf("init_stb_truetype failed\n");
    }
    parse_args(argc, argv);
    // 注册信号处理函数
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
	signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, sigtimer_handler);
    /* 第一步：打开摄像头设备文件 */

    sem_init(&empty, 0, 1);  // 初始为空
    sem_init(&full, 0, 0);   // 初始无数据
    sem_init(&sem_snap, 0, 0);   // 初始无数据

    //h264enc_init();
    //jpegenc_init();

    //init_rtsp_server();
    pthread_t id1, id2, id3, id4, id5;

    
    pthread_create(&id1, NULL, rtsp_server_thread, NULL);
    pthread_create(&id4, NULL, uart_init_pthread, NULL);
    pthread_create(&id3, NULL, get_frame_thread, NULL);
    //pthread_create(&id1, NULL, rtsp_server_thread, NULL);
    // pthread_create(&id2, NULL, h264enc_thread, NULL);
    // pthread_create(&id5, NULL, jpegenc_thread, NULL);

    printf("main thread ID = %d\n", (pid_t)syscall(SYS_gettid));

    pthread_join(id3, NULL);
    pthread_join(id1, NULL);
    //pthread_join(id2, NULL);
    //pthread_join(id5, NULL);
    pthread_join(id4, NULL);
    //if (ipc_config.db != NULL) kvdb_close(ipc_config.db);
    kv_close(&ipc_config.kv);
	//cleanup_stb_truetype();
	//close_uart(PANTILT_UART);
    //if(db) sqlite3_close(db);
    sem_destroy(&empty);
    sem_destroy(&full);
    sem_destroy(&sem_snap);
    system("killall wsdd");
    system("killall onvif_srvd");
    return 0;
}