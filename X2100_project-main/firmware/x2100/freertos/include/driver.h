#ifndef _DRIVER_H_
#define _DRIVER_H_


/*
 * 目的：
 *     建立起应用与驱动之间的唯一的联系。
 * 使用：
 *     1.应用发起请求，驱动返回句柄
 *     2.应用通过句柄访问驱动设备
 */
struct handle_desc {
    const void *ptr;
};


#endif /* _DRIVER_H_ */