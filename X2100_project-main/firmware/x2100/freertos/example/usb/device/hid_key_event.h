#ifndef  _HID_KEY_EVENT_H_
#define _HID_KEY_EVENT_H_

#define USB_A 0x4
#define USB_B 0x5
#define USB_C 0x6
#define USB_D 0x7
#define USB_E 0x8
#define USB_F 0x9
#define USB_G 0xa
#define USB_H 0xb
#define USB_I 0xc
#define USB_J 0xd
#define USB_K 0xe
#define USB_L 0xf
#define USB_M 0x10
#define USB_N 0x11
#define USB_O 0x12
#define USB_P 0x13
#define USB_Q 0x14
#define USB_R 0x15
#define USB_S 0x16
#define USB_T 0x17
#define USB_U 0x18
#define USB_V 0x19
#define USB_W 0x1a
#define USB_X 0x1b
#define USB_Y 0x1c
#define USB_Z 0x1d

#define USB_1 0x1e                                          /* shift下：[!] */
#define USB_2 0x1f                                          /* shift下：[@] */
#define USB_3 0x20                                          /* shift下：[#] */
#define USB_4 0x21                                          /* shift下：[$] */
#define USB_5 0x22                                          /* shift下：[%] */
#define USB_6 0x23                                          /* shift下：[^] */
#define USB_7 0x24                                          /* shift下：[&] */
#define USB_8 0x25                                          /* shift下：[*] */
#define USB_9 0x26                                          /* shift下：[(] */
#define USB_0 0x27                                          /* shift下：[)] */

#define USB_ENTER 0x28
#define USB_ESC 0x29
#define USB_BACKSPACE 0x2a
#define USB_TAB 0x2b
#define USB_SPACEBAR 0x2c
#define USB_CAPSLOCK 0x39

#define USB_F1 0x3a
#define USB_F2 0x3b
#define USB_F3 0x3c
#define USB_F4 0x3d
#define USB_F5 0x3e
#define USB_F6 0x3f
#define USB_F7 0x40
#define USB_F8 0x41
#define USB_F9 0x42
#define USB_F10 0x43
#define USB_F11 0x44
#define USB_F12 0x45

#define USB_INSERT 0x49
#define USB_HOME 0x4a
#define USB_PAGEUP 0x4b
#define USB_DELETE 0x4c
#define USB_END 0x4d
#define USB_PAGEDOWN 0x4e
#define USB_RIGHT 0x4f
#define USB_LEFT 0x50
#define USB_DOWN 0x51
#define USB_UP 0x52
#define USB_NUMLOCK 0x53
#define USB_KPENTER 0x58

#define USB_MINUS 0x2d                                      /* [-]              shift下：[_] */
#define USB_EQUAL 0x2e                                      /* [=]              shift下：[+] */
#define USB_LEFTBRACKET 0x2f                                /* [[]              shift下：[{] */
#define USB_RIGHTBRACKET 0x30                               /* []]              shift下：[}] */
#define USB_BACKSLASH 0x31                                  /* [\]              shift下：[|] */
#define USB_SEMICOLON 0x33                                  /* [;]              shift下：[:] */
#define USB_APOSTROPHE 0x34                                 /* [']              shift下：["] */
#define USB_POINT 0x35                                      /* [`]              shift下：[~] */
#define USB_COMMA 0x36                                      /* [,]              shift下：[<] */
#define USB_PERIOD 0x37                                     /* [.]              shift下：[>] */
#define USB_SLASH 0x38                                      /* [/]              shift下：[?] */

#define USB_LEFTCTRL    0x80000000
#define USB_LEFTSHIFT 0x80000001
#define USB_LEFTALT 0x80000002
#define USB_LEFTWINDOW 0x80000003
#define USB_RIGHTCTRL 0x80000004
#define USB_RIGHTSHIFT 0x80000005
#define USB_RIGHTALT 0x80000006
#define USB_RIGTHWINDOW 0x80000007


#define REPORT_LENGTH 8
struct hid_key_event {
    u8 hid_data[REPORT_LENGTH];
    u8 key_cnt;
};

/**
 * @method  hid_key_event_init
 * 初始化记录按键的结构体
 *
 * @param   {struct hid_key_event *event}    记录按键的结构体
 */
extern void hid_key_event_init(struct hid_key_event *event);

/**
 * @method  hid_key_event_add
 * 记录按下按键的键值
 *
 * @param   {struct hid_key_event *event}    记录按键的结构体
 * @param   {int code}  上报的按键的键值
 * @param   {int value} 上报的按键的状态
 *
 * @return  {int}   返回1表示记录成功，返回0表示按键缓存区6个字节已被填满，返回-1表示释放的按键不在记录之内
 */
extern int hid_key_event_add(struct hid_key_event *event, int code, int value);

/**
 * @method  gadget_keyboard_report_event
 * 发送USB键盘数据到主机
 *
 * @param   {struct hid_key_event *event}    记录按键的结构体
 */
extern void gadget_keyboard_report_event(struct hid_key_event *event);

/**
 * @method  hid_key_query_event
 * 判断按键是否被记录
 *
 * @param   {struct hid_key_event *event}    记录按键的结构体
 * @param   {int code}  需要被查询的按键值
 *
 * @return  {int}   返回1表示已被摁下，返回0表示未被摁下
 */
extern int hid_key_query_event(struct hid_key_event *event, int code);

#endif