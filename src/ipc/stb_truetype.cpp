#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// 全局stb_truetype资源
static unsigned char *font_data = NULL;
static stbtt_fontinfo font_info;
static int font_initialized = 0;
static float scale = 0.0f;
//#define COLOR_FULL

/**
 * 从UTF-8字符串中解析下一个Unicode码点
 * （复用原有实现，保持兼容性）
 */
static int get_utf8_char(const char *text, int *index) {
    unsigned char c = text[*index];
    if (c < 0x80) { // 单字节字符（0-127）
        (*index)++;
        return c;
    } else if (c < 0xE0) { // 双字节（128-223）
        if (!text[*index + 1]) return -1;
        int code = ((c & 0x1F) << 6) | (text[*index + 1] & 0x3F);
        *index += 2;
        return code;
    } else if (c < 0xF0) { // 三字节（224-239，中文常用）
        if (!text[*index + 1] || !text[*index + 2]) return -1;
        int code = ((c & 0x0F) << 12) | ((text[*index + 1] & 0x3F) << 6) | (text[*index + 2] & 0x3F);
        *index += 3;
        return code;
    } else if (c < 0xF8) { // 四字节（240-247）
        if (!text[*index + 1] || !text[*index + 2] || !text[*index + 3]) return -1;
        int code = ((c & 0x07) << 18) | ((text[*index + 1] & 0x3F) << 12) | 
                   ((text[*index + 2] & 0x3F) << 6) | (text[*index + 3] & 0x3F);
        *index += 4;
        return code;
    }
    // 无效的UTF-8序列
    (*index)++;
    return -1;
}

/**
 * 初始化stb_truetype库和字体
 * @param font_path 字体文件路径
 * @param font_size 字体大小（像素）
 * @return 0成功，非0失败
 */
#ifdef __cplusplus
extern "C" {
#endif
 int init_stb_truetype(const char *font_path, int font_size) {
    // 已初始化且字号不变则直接返回
    if (font_initialized) {
        int new_scale = stbtt_ScaleForPixelHeight(&font_info, font_size);
        if (new_scale == scale) return 0;
        
        scale = new_scale;
        return 0;
    }

    // 读取字体文件
    FILE *fp = fopen(font_path, "rb");
    if (!fp) {
        fprintf(stderr, "无法打开字体文件: %s\n", font_path);
        return -1;
    }
    
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long font_size_bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // 分配内存并读取字体数据
    font_data = (unsigned char*)malloc(font_size_bytes);
    if (!font_data) {
        fprintf(stderr, "内存分配失败\n");
        fclose(fp);
        return -1;
    }
    
    if (fread(font_data, 1, font_size_bytes, fp) != font_size_bytes) {
        fprintf(stderr, "读取字体文件失败\n");
        free(font_data);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    // 初始化字体信息
    if (!stbtt_InitFont(&font_info, font_data, stbtt_GetFontOffsetForIndex(font_data, 0))) {
        fprintf(stderr, "初始化字体失败\n");
        free(font_data);
        font_data = NULL;
        return -1;
    }
    
    // 设置字体大小
    scale = stbtt_ScaleForPixelHeight(&font_info, font_size);
    font_initialized = 1;
    return 0;
}

/**
 * 释放stb_truetype资源
 */
void cleanup_stb_truetype(void) {
    if (font_initialized) {
        free(font_data);
        font_data = NULL;
        font_initialized = 0;
        scale = 0.0f;
    }
}

/**
 * 在YUYV图像上叠加文本（使用stb_truetype）
 */
int overlay_text_on_yuyv(uint8_t *yuyv_data, int width, int height, 
                         const char *text, int x, int y,
                         uint8_t *yuv_color, int interlace, int is_yuyv) {
    if (!font_initialized || !font_data) {
        fprintf(stderr, "stb_truetype未初始化，请先调用init_stb_truetype\n");
        return -1;
    }

    int pen_x = x;
    int index = 0;
    int ascent, descent, line_gap;
    //line_gap：行间距（两行基线之间的额外距离）
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    ascent = (int)(ascent * scale);
    descent = (int)(descent * scale);

    while (text[index]) {
        int char_code = get_utf8_char(text, &index);
        if (char_code == -1) continue;

        // 获取字符信息
        int advance, lsb, x0, y0, x1, y1;
        //lsb：左移量（字符位图相对基线起点的偏移）
        stbtt_GetCodepointHMetrics(&font_info, char_code, &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&font_info, char_code, scale, scale, &x0, &y0, &x1, &y1);

        // 计算字符位置
        int x_pos = pen_x + x0 + (int)(lsb * scale);
        int y_pos = y + y0;  // y是基线位置

        // 渲染字符到临时缓冲区
        int w = x1 - x0;
        int h = y1 - y0;
        unsigned char *bitmap = (unsigned char*)malloc(w * h);
        if (!bitmap) continue;
        
        stbtt_MakeCodepointBitmap(&font_info, bitmap, w, h, w, scale, scale, char_code);
#ifdef COLOR_FULL
        // 支持更多颜色
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int img_x = x_pos + col;
                int img_y = y_pos + row;

                if (img_x < 0 || img_x >= width || img_y < 0 || img_y >= height) continue;

                // 计算YUYV索引（每个像素2字节，Y0 U0 Y1 V0交替）
                int yuyv_index = img_y * width * 2 + img_x * 2;
                unsigned char alpha = bitmap[row * w + col];
                if (alpha == 0) continue;

                // 混合系数（使用整数运算避免浮点误差）
                int alpha_ratio = alpha;
                int inv_alpha = 255 - alpha_ratio;

                // 1. 处理Y分量（亮度）- 增强红色对比度
                uint8_t orig_y = yuyv_data[yuyv_index];
                uint8_t new_y = (orig_y * inv_alpha + yuv_color[0] * alpha_ratio) / 255;
                yuyv_data[yuyv_index] = new_y;


                // 2. 处理U分量（共享分量，每两个像素一组）
                int u_index;
                if (img_x % 2 == 0) {
                    u_index = yuyv_index + 1;  // 偶数像素：U在Y后1字节
                } else {
                    u_index = yuyv_index - 1;  // 奇数像素：共享前一个像素的U（Y前1字节）
                }
                uint8_t orig_u = yuyv_data[u_index];
                uint8_t new_u = (orig_u * inv_alpha + yuv_color[1] * alpha_ratio) / 255;
                yuyv_data[u_index] = new_u;

                // 3. 处理V分量（共享分量）
                int v_index;
                if (img_x % 2 == 0) {
                    v_index = yuyv_index + 3;  // 偶数像素：V在Y后3字节
                } else {
                    v_index = yuyv_index + 1;  // 奇数像素：共享前一个像素的V（Y后1字节）
                }
                // 2. 处理U分量（共享分量，每两个像素一组）
                //int u_index = yuyv_index + 1;  // 当前像素对的U分量索引


                // 3. 处理V分量（共享分量，位于当前像素对的第四个字节）
                //int v_index = u_index + 2;  // V分量在U分量后2字节
                if (v_index < width * height * 2) {  // 边界检查
                    uint8_t orig_v = yuyv_data[v_index];
                    uint8_t new_v = (orig_v * inv_alpha + yuv_color[2] * alpha_ratio) / 255;
                    yuyv_data[v_index] = new_v;
                }
            }
        }
#else
        // 绘制到位图
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int img_x = x_pos + col;
                int img_y = y_pos + row;

                if (img_x < 0 || img_x >= width || img_y < 0 || img_y >= height) continue;
                int yuyv_index;
                if (is_yuyv) {
                    yuyv_index = img_y * width * 2 + img_x * 2;
                    if (interlace) {
                        if (img_y % 2 == 0) {
                            yuyv_index = img_y/2*width*2 + img_x * 2; 
                        } else {
                            yuyv_index = height/2 * width * 2 + img_y/2*width*2 + img_x * 2; 
                        } 
                    } 
                } else {
                    yuyv_index = img_y * width * 2 + img_x * 2 + 1;
                    if (interlace) {
                        if (img_y % 2 == 0) {
                            yuyv_index = img_y/2*width*2 + img_x * 2 + 1; 
                        } else {
                            yuyv_index = height/2 * width * 2 + img_y/2*width*2 + img_x * 2 + 1; 
                        } 
                    }                    
                }

                unsigned char alpha = bitmap[row * w + col];
                if (alpha == 0) continue;

                // 简单Alpha混合
                uint8_t orig_y = yuyv_data[yuyv_index];
                uint8_t new_y = (orig_y * (255 - alpha) + yuv_color[0] * alpha) / 255;
                yuyv_data[yuyv_index] = new_y;
            }
        }

    
#endif
        free(bitmap);
        pen_x += (int)(advance * scale);  // 移动到下一个字符
    }

    return 0;
}

#ifdef __cplusplus
}
#endif 
/*
计算依据：从 RGB 按 BT.601 标准转换到 YUV ：
Y = 0.299R + 0.587G + 0.114*B
U = 128 - 0.168736R - 0.331264G + 0.5*B
V = 128 + 0.5R - 0.418688G - 0.081312*B
*/

// 示例main函数保持不变
int __main() {
    int width = 640;
    int height = 480;
    uint8_t *yuyv_image = (uint8_t *)malloc(width * height * 2);
    if (!yuyv_image) {
        fprintf(stderr, "内存分配失败\n");
        return 1;
    }
    
    // 填充蓝色背景
    for (int i = 0; i < width * height * 2; i += 4) {
        yuyv_image[i] = 255;       // Y0
        yuyv_image[i + 1] = 128;  // U0
        yuyv_image[i + 2] = 255;   // Y1
        yuyv_image[i + 3] = 128;  // V0
    }
    
    // 文本颜色（白色）
    uint8_t white[] = {255, 128, 128};
    uint8_t black[] = {0, 128, 128};
    uint8_t red[] = {30, 44, 255};
    uint8_t green[] = {149, 43, 51};
    uint8_t blue[] = {84, 255, 106};
    // 初始化字体
    const char *font_path = "./MSYH.TTC";
    if (init_stb_truetype(font_path, 24) != 0) {
        free(yuyv_image);
        return 1;
    }
    
    // 叠加文本
    overlay_text_on_yuyv(yuyv_image, width, height, "Hello, World!", 50, 50, blue, 0, 1);
    overlay_text_on_yuyv(yuyv_image, width, height, "中文测试：stb版", 50, 100, red, 0, 1);
    overlay_text_on_yuyv(yuyv_image, width, height, "轻量级字体渲染", 50, 150, green, 0, 1);
    
    // 保存图像
    FILE *fp = fopen("output_stb.yuv", "wb");
    if (fp) {
        fwrite(yuyv_image, 1, width * height * 2, fp);
        fclose(fp);
        printf("图像已保存到 output_stb.yuv\n");
        printf("可使用命令查看：ffplay -f rawvideo -pix_fmt yuyv422 -video_size 640x480 output_stb.yuv\n");
    } else {
        fprintf(stderr, "无法打开文件 output_stb.yuv\n");
    }
    
    // 清理资源
    free(yuyv_image);
    cleanup_stb_truetype();
    
    return 0;
}
