#ifndef __USB_UVC_H
#define __USB_UVC_H

#include <common.h>
#include <list.h>

enum uvc_buffer_state {
    UVC_BUF_STATE_IDLE    = 0,
    UVC_BUF_STATE_QUEUED    = 1,
    UVC_BUF_STATE_ACTIVE    = 2,
    UVC_BUF_STATE_READY    = 3,
    UVC_BUF_STATE_DONE    = 4,
    UVC_BUF_STATE_ERROR    = 5,
};

struct uvc_buffer {
    struct list_head queue;

    enum uvc_buffer_state state;
    unsigned int error;

    void *mem;
    uint32_t length;
    uint32_t bytesused;

    void (*complete)(struct uvc_buffer *buf);
    void *private_data;

    /* 帧的时间戳，单位微秒*/
    unsigned int timestamp;
};

struct uvc_video_format {
    /* Frame parameters */
    u32 fcc;
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
    unsigned int fps;
};

/* terminal type */
#define UVC_TT_VENDOR_SPECIFIC                0x0100
#define UVC_TT_STREAMING                    0x0101
#define UVC_ITT_VENDOR_SPECIFIC                0x0200
#define UVC_ITT_CAMERA                        0x0201
#define UVC_ITT_MEDIA_TRANSPORT_INPUT        0x0202
#define UVC_OTT_VENDOR_SPECIFIC                0x0300
#define UVC_OTT_DISPLAY                        0x0301
#define UVC_OTT_MEDIA_TRANSPORT_OUTPUT        0x0302
#define UVC_EXTERNAL_VENDOR_SPECIFIC        0x0400
#define UVC_COMPOSITE_CONNECTOR                0x0401
#define UVC_SVIDEO_CONNECTOR                0x0402
#define UVC_COMPONENT_CONNECTOR                0x0403

/*
UVC_KEY_FRAME_RATE： 关键帧的传输速率控制
UVC_P_FRAME_RATE： P帧的传输速率控制
UVC_COMP_QUALITY： 压缩质量的控制
UVC_COMP_WINDOW_SIZE： 压缩窗口大小控制（单位：KB）
UVC_GENERATE_KEY_FRAME： 生成关键帧控制
UVC_UPDATA_FRAME_SEGMENT： 更新帧信号控制
*/
#define UVC_KEY_FRAME_RATE            (1 << 0)
#define UVC_P_FRAME_RATE            (1 << 1)
#define UVC_COMP_QUALITY            (1 << 2)
#define UVC_COMP_WINDOW_SIZE        (1 << 3)
#define UVC_GENERATE_KEY_FRAME        (1 << 4)
#define UVC_UPDATA_FRAME_SEGMENT    (1 << 5)

/* ------------------------------------------------------------------------
 * GUIDs
 */
#define UVC_GUID_FORMAT_MJPEG \
    { 'M',  'J',  'P',  'G', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2 \
    { 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2_ISIGHT \
    { 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0x00, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_NV12 \
    { 'N',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YV12 \
    { 'Y',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_I420 \
    { 'I',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_UYVY \
    { 'U',  'Y',  'V',  'Y', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y800 \
    { 'Y',  '8',  '0',  '0', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y8 \
    { 'Y',  '8',  ' ',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y10 \
    { 'Y',  '1',  '0',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y12 \
    { 'Y',  '1',  '2',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y16 \
    { 'Y',  '1',  '6',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BY8 \
    { 'B',  'Y',  '8',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BA81 \
    { 'B',  'A',  '8',  '1', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GBRG \
    { 'G',  'B',  'R',  'G', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GRBG \
    { 'G',  'R',  'B',  'G', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RGGB \
    { 'R',  'G',  'G',  'B', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BG16 \
    { 'B',  'G',  '1',  '6', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GB16 \
    { 'G',  'B',  '1',  '6', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RG16 \
    { 'R',  'G',  '1',  '6', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GR16 \
    { 'G',  'R',  '1',  '6', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RGBP \
    { 'R',  'G',  'B',  'P', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BGR3 \
    { 0x7d, 0xeb, 0x36, 0xe4, 0x4f, 0x52, 0xce, 0x11, \
     0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
#define UVC_GUID_FORMAT_BGR4 \
    { 0x7e, 0xeb, 0x36, 0xe4, 0x4f, 0x52, 0xce, 0x11, \
     0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
#define UVC_GUID_FORMAT_M420 \
    { 'M',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

#define UVC_GUID_FORMAT_H264 \
    { 'H',  '2',  '6',  '4', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_H265 \
    { 'H',  '2',  '6',  '5', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y8I \
    { 'Y',  '8',  'I',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y12I \
    { 'Y',  '1',  '2',  'I', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Z16 \
    { 'Z',  '1',  '6',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RW10 \
    { 'R',  'W',  '1',  '0', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_INVZ \
    { 'I',  'N',  'V',  'Z', 0x90, 0x2d, 0x58, 0x4a, \
     0x92, 0x0b, 0x77, 0x3f, 0x1f, 0x2c, 0x55, 0x6b}
#define UVC_GUID_FORMAT_INZI \
    { 'I',  'N',  'Z',  'I', 0x66, 0x1a, 0x42, 0xa2, \
     0x90, 0x65, 0xd0, 0x18, 0x14, 0xa8, 0xef, 0x8a}
#define UVC_GUID_FORMAT_INVI \
    { 'I',  'N',  'V',  'I', 0xdb, 0x57, 0x49, 0x5e, \
     0x8e, 0x3f, 0xf4, 0x79, 0x53, 0x2b, 0x94, 0x6f}
#define UVC_GUID_FORMAT_CNF4 \
    { 'C',  ' ',  ' ',  ' ', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

#define UVC_GUID_FORMAT_D3DFMT_L8 \
    {0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_KSMEDIA_L8_IR \
    {0x32, 0x00, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

#define UVC_GUID_FORMAT_HEVC \
    { 'H',  'E',  'V',  'C', 0x00, 0x00, 0x10, 0x00, \
     0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

#define UVC_GUID_EXTENSION_UNIT_H264 \
    { 0x41, 0x76, 0x9e, 0xa2, 0x04, 0xde, 0xe3, 0x47, \
     0x8b, 0x2b, 0xf4, 0x34, 0x1a, 0xff, 0x00, 0x3b}

struct uvc_format_desc {
    u8 guid[16];
    u32 fcc;
};

extern const struct uvc_format_desc *uvc_format_by_guid(const u8 guid[16]);

/*  Four-character-code (FOURCC) */
#define v4l2_fourcc(a, b, c, d)\
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define v4l2_fourcc_be(a, b, c, d)    (v4l2_fourcc(a, b, c, d) | (1 << 31))

/* 10bit raw bayer packed, 5 bytes for every 4 pixels */
#define V4L2_PIX_FMT_SBGGR10P v4l2_fourcc('p', 'B', 'A', 'A') /* bpp: 10  raw10 */

#define V4L2_PIX_FMT_RGB565  v4l2_fourcc('R', 'G', 'B', 'P') /* 16  RGB-5-6-5     */
#define V4L2_PIX_FMT_BGR24   v4l2_fourcc('B', 'G', 'R', '3') /* bpp:24  BGR-8-8-8     */
#define V4L2_PIX_FMT_XBGR32  v4l2_fourcc('X', 'R', '2', '4') /* 32  BGRX-8-8-8-8  */

#define V4L2_PIX_FMT_GREY    v4l2_fourcc('G', 'R', 'E', 'Y') /* bpp:8  Greyscale     */
#define V4L2_PIX_FMT_Y10     v4l2_fourcc('Y', '1', '0', ' ') /* 10  Greyscale     */
#define V4L2_PIX_FMT_Y12     v4l2_fourcc('Y', '1', '2', ' ') /* 12  Greyscale     */
#define V4L2_PIX_FMT_Y16     v4l2_fourcc('Y', '1', '6', ' ') /* 16  Greyscale     */
#define V4L2_PIX_FMT_YUYV    v4l2_fourcc('Y', 'U', 'Y', 'V') /* bpp:16  YUV 4:2:2     */
#define V4L2_PIX_FMT_UYVY    v4l2_fourcc('U', 'Y', 'V', 'Y') /* 16  YUV 4:2:2     */
#define V4L2_PIX_FMT_M420    v4l2_fourcc('M', '4', '2', '0') /* 12  YUV 4:2:0 2 lines y, 1 line uv interleaved */
#define V4L2_PIX_FMT_NV12    v4l2_fourcc('N', 'V', '1', '2') /* bpp:12  Y/CbCr 4:2:0  */
#define V4L2_PIX_FMT_YUV420  v4l2_fourcc('Y', 'U', '1', '2') /* 12  YUV 4:2:0     */
#define V4L2_PIX_FMT_YVU420  v4l2_fourcc('Y', 'V', '1', '2') /* 12  YVU 4:2:0     */
#define V4L2_PIX_FMT_SBGGR8  v4l2_fourcc('B', 'A', '8', '1') /*  8  BGBG.. GRGR.. */
#define V4L2_PIX_FMT_SGBRG8  v4l2_fourcc('G', 'B', 'R', 'G') /*  8  GBGB.. RGRG.. */
#define V4L2_PIX_FMT_SGRBG8  v4l2_fourcc('G', 'R', 'B', 'G') /*  8  GRGR.. BGBG.. */
#define V4L2_PIX_FMT_SRGGB8  v4l2_fourcc('R', 'G', 'G', 'B') /*  8  RGRG.. GBGB.. */
#define V4L2_PIX_FMT_SRGGB10P v4l2_fourcc('p', 'R', 'A', 'A')
#define V4L2_PIX_FMT_SBGGR16 v4l2_fourcc('B', 'Y', 'R', '2') /* 16  BGBG.. GRGR.. */
#define V4L2_PIX_FMT_SGBRG16 v4l2_fourcc('G', 'B', '1', '6') /* 16  GBGB.. RGRG.. */
#define V4L2_PIX_FMT_SGRBG16 v4l2_fourcc('G', 'R', '1', '6') /* 16  GRGR.. BGBG.. */
#define V4L2_PIX_FMT_SRGGB16 v4l2_fourcc('R', 'G', '1', '6') /* 16  RGRG.. GBGB.. */
#define V4L2_PIX_FMT_MJPEG    v4l2_fourcc('M', 'J', 'P', 'G') /* bpp:8  Motion-JPEG   */
#define V4L2_PIX_FMT_DV       v4l2_fourcc('d', 'v', 's', 'd') /* 1394          */
#define V4L2_PIX_FMT_H264     v4l2_fourcc('H', '2', '6', '4') /* H264 with start codes */
#define V4L2_PIX_FMT_HEVC     v4l2_fourcc('H', 'E', 'V', 'C') /* HEVC aka H.265 */
#define V4L2_PIX_FMT_Y8I      v4l2_fourcc('Y', '8', 'I', ' ') /* Greyscale 8-bit L/R interleaved */
#define V4L2_PIX_FMT_Y12I     v4l2_fourcc('Y', '1', '2', 'I') /* Greyscale 12-bit L/R interleaved */
#define V4L2_PIX_FMT_Z16      v4l2_fourcc('Z', '1', '6', ' ') /* Depth data 16-bit */
#define V4L2_PIX_FMT_INZI     v4l2_fourcc('I', 'N', 'Z', 'I') /* Intel Planar Greyscale 10-bit and Depth 16-bit */
#define V4L2_PIX_FMT_CNF4     v4l2_fourcc('C', 'N', 'F', '4') /* Intel 4-bit packed depth confidence information */

/* --------------------------------------------------------------------------
 * UVC constants
 */

/* A.2. Video Interface Subclass Codes */
#define UVC_SC_UNDEFINED                0x00
#define UVC_SC_VIDEOCONTROL                0x01
#define UVC_SC_VIDEOSTREAMING                0x02
#define UVC_SC_VIDEO_INTERFACE_COLLECTION        0x03

/* A.3. Video Interface Protocol Codes */
#define UVC_PC_PROTOCOL_UNDEFINED            0x00
#define UVC_PC_PROTOCOL_15                0x01

/* A.5. Video Class-Specific VC Interface Descriptor Subtypes */
#define UVC_VC_DESCRIPTOR_UNDEFINED            0x00
#define UVC_VC_HEADER                    0x01
#define UVC_VC_INPUT_TERMINAL                0x02
#define UVC_VC_OUTPUT_TERMINAL                0x03
#define UVC_VC_SELECTOR_UNIT                0x04
#define UVC_VC_PROCESSING_UNIT                0x05
#define UVC_VC_EXTENSION_UNIT                0x06

/* A.6. Video Class-Specific VS Interface Descriptor Subtypes */
#define UVC_VS_UNDEFINED                0x00
#define UVC_VS_INPUT_HEADER                0x01
#define UVC_VS_OUTPUT_HEADER                0x02
#define UVC_VS_STILL_IMAGE_FRAME            0x03
#define UVC_VS_FORMAT_UNCOMPRESSED            0x04
#define UVC_VS_FRAME_UNCOMPRESSED            0x05
#define UVC_VS_FORMAT_MJPEG                0x06
#define UVC_VS_FRAME_MJPEG                0x07
#define UVC_VS_FORMAT_MPEG2TS                0x0a
#define UVC_VS_FORMAT_DV                0x0c
#define UVC_VS_COLORFORMAT                0x0d
#define UVC_VS_FORMAT_FRAME_BASED            0x10
#define UVC_VS_FRAME_FRAME_BASED            0x11
#define UVC_VS_FORMAT_STREAM_BASED            0x12
#define UVC_VS_FORMAT_H264                0x13
#define UVC_VS_FRAME_H264                0x14
#define UVC_VS_FORMAT_H264_SIMULCAST        0x15

/* A.7. Video Class-Specific Endpoint Descriptor Subtypes */
#define UVC_EP_UNDEFINED                0x00
#define UVC_EP_GENERAL                    0x01
#define UVC_EP_ENDPOINT                    0x02
#define UVC_EP_INTERRUPT                0x03


/* A.9.1. VideoControl Interface Control Selectors */
#define UVC_VC_CONTROL_UNDEFINED            0x00
#define UVC_VC_VIDEO_POWER_MODE_CONTROL            0x01
#define UVC_VC_REQUEST_ERROR_CODE_CONTROL        0x02

/* A.9.2. Terminal Control Selectors */
#define UVC_TE_CONTROL_UNDEFINED            0x00

/* A.9.3. Selector Unit Control Selectors */
#define UVC_SU_CONTROL_UNDEFINED            0x00
#define UVC_SU_INPUT_SELECT_CONTROL            0x01

/* A.9.7. VideoStreaming Interface Control Selectors */
#define UVC_VS_CONTROL_UNDEFINED            0x00
#define UVC_VS_PROBE_CONTROL                0x01
#define UVC_VS_COMMIT_CONTROL                0x02
#define UVC_VS_STILL_PROBE_CONTROL            0x03
#define UVC_VS_STILL_COMMIT_CONTROL            0x04
#define UVC_VS_STILL_IMAGE_TRIGGER_CONTROL        0x05
#define UVC_VS_STREAM_ERROR_CODE_CONTROL        0x06
#define UVC_VS_GENERATE_KEY_FRAME_CONTROL        0x07
#define UVC_VS_UPDATE_FRAME_SEGMENT_CONTROL        0x08
#define UVC_VS_SYNC_DELAY_CONTROL            0x09

/* B.1. USB Terminal Types */
#define UVC_TT_VENDOR_SPECIFIC                0x0100
#define UVC_TT_STREAMING                0x0101

/* B.2. Input Terminal Types */
#define UVC_ITT_VENDOR_SPECIFIC                0x0200
#define UVC_ITT_CAMERA                    0x0201
#define UVC_ITT_MEDIA_TRANSPORT_INPUT            0x0202

/* B.3. Output Terminal Types */
#define UVC_OTT_VENDOR_SPECIFIC                0x0300
#define UVC_OTT_DISPLAY                    0x0301
#define UVC_OTT_MEDIA_TRANSPORT_OUTPUT            0x0302

/* B.4. External Terminal Types */
#define UVC_EXTERNAL_VENDOR_SPECIFIC            0x0400
#define UVC_COMPOSITE_CONNECTOR                0x0401
#define UVC_SVIDEO_CONNECTOR                0x0402
#define UVC_COMPONENT_CONNECTOR                0x0403

/* 2.4.2.2. Status Packet Type */
#define UVC_STATUS_TYPE_CONTROL                1
#define UVC_STATUS_TYPE_STREAMING            2

/* 2.4.3.3. Payload Header Information */
#define UVC_STREAM_EOH                    (1 << 7)
#define UVC_STREAM_ERR                    (1 << 6)
#define UVC_STREAM_STI                    (1 << 5)
#define UVC_STREAM_RES                    (1 << 4)
#define UVC_STREAM_SCR                    (1 << 3)
#define UVC_STREAM_PTS                    (1 << 2)
#define UVC_STREAM_EOF                    (1 << 1)
#define UVC_STREAM_FID                    (1 << 0)

/* 4.1.2. Control Capabilities */
#define UVC_CONTROL_CAP_GET                (1 << 0)
#define UVC_CONTROL_CAP_SET                (1 << 1)
#define UVC_CONTROL_CAP_DISABLED        (1 << 2)
#define UVC_CONTROL_CAP_AUTOUPDATE        (1 << 3)
#define UVC_CONTROL_CAP_ASYNCHRONOUS    (1 << 4)

/* Video Class-Specific Request Codes */
#define UVC_RC_UNDEFINED            0x00
#define UVC_SET_CUR                    0x01
#define UVC_GET_CUR                    0x81
#define UVC_GET_MIN                    0x82
#define UVC_GET_MAX                    0x83
#define UVC_GET_RES                    0x84
#define UVC_GET_LEN                    0x85
#define UVC_GET_INFO                0x86
#define UVC_GET_DEF                    0x87

/*  Camera Terminal Control Selectors */
#define UVC_CT_CONTROL_UNDEFINED                0x00
#define UVC_CT_SCANNING_MODE_CONTROL            0x01
#define UVC_CT_AE_MODE_CONTROL                    0x02
#define UVC_CT_AE_PRIORITY_CONTROL                0x03
#define UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL    0x04
#define UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL    0x05
#define UVC_CT_FOCUS_ABSOLUTE_CONTROL            0x06
#define UVC_CT_FOCUS_RELATIVE_CONTROL            0x07
#define UVC_CT_FOCUS_AUTO_CONTROL                0x08
#define UVC_CT_IRIS_ABSOLUTE_CONTROL            0x09
#define UVC_CT_IRIS_RELATIVE_CONTROL            0x0a
#define UVC_CT_ZOOM_ABSOLUTE_CONTROL            0x0b
#define UVC_CT_ZOOM_RELATIVE_CONTROL            0x0c
#define UVC_CT_PANTILT_ABSOLUTE_CONTROL            0x0d
#define UVC_CT_PANTILT_RELATIVE_CONTROL            0x0e
#define UVC_CT_ROLL_ABSOLUTE_CONTROL            0x0f
#define UVC_CT_ROLL_RELATIVE_CONTROL            0x10
#define UVC_CT_PRIVACY_CONTROL                    0x11
#define UVC_CT_FOCUS_SIMPLE_CONTROL                0x12
#define UVC_CT_WINDOW_CONTROL                    0x13
#define UVC_CT_REGION_OF_INTEREST_CONTROL        0x14

/* Processing Unit Control Selectors */
#define UVC_PU_CONTROL_UNDEFINED            0x00
#define UVC_PU_BACKLIGHT_COMPENSATION_CONTROL        0x01
#define UVC_PU_BRIGHTNESS_CONTROL            0x02
#define UVC_PU_CONTRAST_CONTROL                0x03
#define UVC_PU_GAIN_CONTROL                0x04
#define UVC_PU_POWER_LINE_FREQUENCY_CONTROL        0x05
#define UVC_PU_HUE_CONTROL                0x06
#define UVC_PU_SATURATION_CONTROL            0x07
#define UVC_PU_SHARPNESS_CONTROL            0x08
#define UVC_PU_GAMMA_CONTROL                0x09
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL    0x0a
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL    0x0b
#define UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL        0x0c
#define UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL    0x0d
#define UVC_PU_DIGITAL_MULTIPLIER_CONTROL        0x0e
#define UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL        0x0f
#define UVC_PU_HUE_AUTO_CONTROL                0x10
#define UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL        0x11
#define UVC_PU_ANALOG_LOCK_STATUS_CONTROL        0x12
#define UVC_PU_CONTRAST_AUTO_CONTROL            0x13

/* H.264 Extensions Unit Control Selectors */
#define UVC_EUH_VIDEO_CONFIG_PROBE            0x01
#define UVC_EUH_VIDEO_CONFIG_COMMIT            0x02
#define UVC_EUH_RATE_CONTROL_MODE            0x03
#define UVC_EUH_TEMPORAL_SCALE_MODE            0x04
#define UVC_EUH_SPATIAL_SCALE_MODE            0x05
#define UVC_EUH_SNR_SCALE_MODE                0x06
#define UVC_EUH_LTR_BUFFER_SIZE_CONTROL        0x07
#define UVC_EUH_LTR_PICTURE_CONTROL            0x08
#define UVC_EUH_PICTURE_TYPE_CONTROL        0x09
#define UVC_EUH_VERSION                        0x0a
#define UVC_EUH_ENCODER_RESET                0x0b
#define UVC_EUH_FRAMERATE_CONFIG            0x0c
#define UVC_EUH_VIDEO_ADVANCE_CONFIG        0x0d
#define UVC_EUH_BITRATE_LAYERS                0x0e
#define UVC_EUH_QP_STEPS_LAYERS                0x0f

/* the mentioned Control is supported for the video stream */
#define UVC_FEATURE_SCANNING_MODE            (1 << 0)
#define UVC_FEATURE_AUTO_EXPOSURE_MODE        (1 << 1)
#define UVC_FEATURE_AUTO_EXPOSURE_PRIORITY    (1 << 2)
#define UVC_FEATURE_EXPOSURE_TIME_ABSOLUTE    (1 << 3)
#define UVC_FEATURE_EXPOSURE_TIME_RELATIVE    (1 << 4)
#define UVC_FEATURE_FOCUS_ABSOLUTE            (1 << 5)
#define UVC_FEATURE_FOCUS_RELATIVE            (1 << 6)
#define UVC_FEATURE_IRIS_ABSOLUTE            (1 << 7)
#define UVC_FEATURE_IRIS_RELATIVE            (1 << 8)
#define UVC_FEATURE_ZOOM_ABSOLUTE            (1 << 9)
#define UVC_FEATURE_ZOOM_RELATIVE            (1 << 10)
#define UVC_FEATURE_PAN_TILT_ABSOLUTE        (1 << 11)
#define UVC_FEATURE_PAN_TILT_RELATIVE        (1 << 12)
#define UVC_FEATURE_ROLL_ABSOLUTE            (1 << 13)
#define UVC_FEATURE_ROLL_RELATIVE            (1 << 14)
#define UVC_FEATURE_FOUCES_AUTO                (1 << 17)
#define UVC_FEATURE_PRIVACY                    (1 << 18)
#define UVC_FEATURE_FOCUS_SIMPLE            (1 << 19)
#define UVC_FEATURE_WINDOW                    (1 << 20)
#define UVC_FEATURE_REGION_OF_INTEREST        (1 << 21)

#define UVC_SCANNING_MODE_DATA_LEN                    1
#define UVC_AUTO_EXPOSURE_MODE_DATA_LEN                1
#define UVC_AUTO_EXPOSURE_PRIORITY_DATA_LEN            1
#define UVC_EXPOSURE_TIME_ABSOLUTE_DATA_LEN            4
#define UVC_EXPOSURE_TIME_RELATIVE_DATA_LEN            1
#define UVC_FOCUS_ABSOLUTE_DATA_LEN                    2
#define UVC_FOCUS_RELATIVE_DATA_LEN                    2
#define UVC_IRIS_ABSOLUTE_DATA_LEN                    2
#define UVC_IRIS_RELATIVE_DATA_LEN                    1
#define UVC_ZOOM_ABSOLUTE_DATA_LEN                    2
#define UVC_ZOOM_RELATIVE_DATA_LEN                    3
#define UVC_PAN_TILT_ABSOLUTE_DATA_LEN                8
#define UVC_PAN_TILT_RELATIVE_DATA_LEN                4
#define UVC_ROLL_ABSOLUTE_DATA_LEN                    2
#define UVC_ROLL_RELATIVE_DATA_LEN                    2
#define UVC_FOUCES_AUTO_DATA_LEN                    1
#define UVC_PRIVACY_DATA_LEN                        1
#define UVC_FOCUS_SIMPLE_DATA_LEN                    1
#define UVC_WINDOW_DATA_LEN                            12
#define UVC_REGION_OF_INTEREST_DATA_LEN                10

/* the mentioned Control is supported for the video stream */
#define UVC_PARAM_BRIGHTNESS                (1 << 0)
#define UVC_PARAM_CONTRAST                    (1 << 1)
#define UVC_PARAM_HUE                        (1 << 2)
#define UVC_PARAM_SATURATION                (1 << 3)
#define UVC_PARAM_SHARPNESS                    (1 << 4)
#define UVC_PARAM_GAMMA                        (1 << 5)
#define UVC_PARAM_WHITE_BALANCE_TEMPERATURE    (1 << 6)
#define UVC_PARAM_WHITE_BALANCE_COMPONENT    (1 << 7)
#define UVC_PARAM_WHITE_BACKLIGHT_COMPENSATION        (1 << 8)
#define UVC_PARAM_GAIN                        (1 << 9)
#define UVC_PARAM_POWER_LINE_FREQUEBCY        (1 << 10)
#define UVC_PARAM_HUE_AUTO                    (1 << 11)
#define UVC_PARAM_WHITE_BALANCE_TEMPERATURE_AUTO    (1 << 12)
#define UVC_PARAM_WHITE_BALANCE_COMPONENT_AUTO        (1 << 13)
#define UVC_PARAM_DIGITAL_MULTIPLIER        (1 << 14)
#define UVC_PARAM_DIGITAL_MULTIPLIER_LIMIT    (1 << 15)
#define UVC_PARAM_ANALOG_VIDEO_STANDARD        (1 << 16)
#define UVC_PARAM_ANALOG_VIDEO_LOCK_STATUS    (1 << 17)
#define UVC_PARAM_CONTRAST_AUTO                (1 << 18)

#define UVC_BRIGHTNESS_DATA_LEN                        2
#define UVC_CONTRAST_DATA_LEN                        2
#define UVC_HUE_DATA_LEN                            2
#define UVC_SATURATION_DATA_LEN                        2
#define UVC_SHARPNESS_DATA_LEN                        2
#define UVC_GAMMA_DATA_LEN                            2
#define UVC_WHITE_BALANCE_TEMPERATURE_DATA_LEN        2
#define UVC_WHITE_BALANCE_COMPONENT_DATA_LEN        4
#define UVC_WHITE_BACKLIGHT_COMPENSATION_DATA_LEN    2
#define UVC_GAIN_DATA_LEN                            2
#define UVC_POWER_LINE_FREQUEBCY_DATA_LEN            1
#define UVC_HUE_AUTO_DATA_LEN                        1
#define UVC_WHITE_BALANCE_TEMPERATURE_AUTO_DATA_LEN    1
#define UVC_WHITE_BALANCE_COMPONENT_AUTO_DATA_LEN    1
#define UVC_DIGITAL_MULTIPLIER_DATA_LEN                2
#define UVC_DIGITAL_MULTIPLIER_LIMIT_DATA_LEN        2
#define UVC_ANALOG_VIDEO_STANDARD_DATA_LEN            1
#define UVC_ANALOG_VIDEO_LOCK_STATUS_DATA_LEN        1
#define UVC_CONTRAST_AUTO_DATA_LEN                    1

/* the mentioned Control is supported for the h264 video stream */
#define UVC_H264_VIDEO_CONFIG_PROBE            (1 << 0)
#define UVC_H264_VIDEO_CONFIG_COMMIT        (1 << 1)
#define UVC_H264_RATE_CONTROL_MODE            (1 << 2)
#define UVC_H264_TEMPORAL_SCALE_MODE        (1 << 3)
#define UVC_H264_SPATIAL_SCALE_MODE            (1 << 4)
#define UVC_H264_SNR_SCALE_MODE                (1 << 5)
#define UVC_H264_LTR_BUFFER_SIZE_CONTROL    (1 << 6)
#define UVC_H264_LTR_PICTURE_CONTROL        (1 << 7)
#define UVC_H264_PICTURE_TYPE_CONTROL        (1 << 8)
#define UVC_H264_VERSION                    (1 << 9)
#define UVC_H264_ENCODER_RESET                (1 << 10)
#define UVC_H264_FRAMERATE_CONFIG            (1 << 11)
#define UVC_H264_VIDEO_ADVANCE_CONFIG        (1 << 12)
#define UVC_H264_BITRATE_LAYERS                (1 << 13)
#define UVC_H264_QP_STEPS_LAYERS            (1 << 14)

#define UVC_VIDEO_CONFIG_PROBE_LEN            46
#define UVC_VIDEO_CONFIG_COMMIT_LEN            46
#define UVC_RATE_CONTROL_MODE_LEN            3
#define UVC_TEMPORAL_SCALE_MODE_LEN            3
#define UVC_SPATIAL_SCALE_MODE_LEN            3
#define UVC_SNR_SCALE_MODE_LEN                4
#define UVC_LTR_BUFFER_SIZE_CONTROL_LEN        4
#define UVC_LTR_PICTURE_CONTROL_LEN            4
#define UVC_PICTURE_TYPE_CONTROL_LEN        4
#define UVC_VERSION_LEN                        2
#define UVC_ENCODER_RESET_LEN                2
#define UVC_FRAMERATE_CONFIG_LEN            6
#define UVC_VIDEO_ADVANCE_CONFIG_LEN        8
#define UVC_BITRATE_LAYERS_LEN                10
#define UVC_QP_STEPS_LAYERS_LEN                5

/* ------------------------------------------------------------------------
 * UVC structures
 */

/* All UVC descriptors have these 3 fields at the beginning */
struct uvc_descriptor_header {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
} __attribute__((packed));

/* 3.7.2. Video Control Interface Header Descriptor */
struct uvc_header_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u16 bcdUVC;
    u16 wTotalLength;
    u32 dwClockFrequency;
    u8  bInCollection;
    u8  baInterfaceNr[];
} __attribute__((__packed__));

#define UVC_DT_HEADER_SIZE(n)                (12+(n))

#define UVC_HEADER_DESCRIPTOR(n) \
    uvc_header_descriptor_##n

#define DECLARE_UVC_HEADER_DESCRIPTOR(n)        \
struct UVC_HEADER_DESCRIPTOR(n) {            \
    u8  bLength;                    \
    u8  bDescriptorType;                \
    u8  bDescriptorSubType;            \
    u16 bcdUVC;                    \
    u16 wTotalLength;                \
    u32 dwClockFrequency;                \
    u8  bInCollection;                \
    u8  baInterfaceNr[n];                \
} __attribute__ ((packed))

/* 3.7.2.1. Input Terminal Descriptor */
struct uvc_input_terminal_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bTerminalID;
    u16 wTerminalType;
    u8  bAssocTerminal;
    u8  iTerminal;
} __attribute__((__packed__));

#define UVC_DT_INPUT_TERMINAL_SIZE            8

/* 3.7.2.2. Output Terminal Descriptor */
struct uvc_output_terminal_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bTerminalID;
    u16 wTerminalType;
    u8  bAssocTerminal;
    u8  bSourceID;
    u8  iTerminal;
} __attribute__((__packed__));

#define UVC_DT_OUTPUT_TERMINAL_SIZE            9

/* 3.7.2.3. Camera Terminal Descriptor */
struct uvc_camera_terminal_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bTerminalID;
    u16 wTerminalType;
    u8  bAssocTerminal;
    u8  iTerminal;
    u16 wObjectiveFocalLengthMin;
    u16 wObjectiveFocalLengthMax;
    u16 wOcularFocalLength;
    u8  bControlSize;
    u8  bmControls[3];
} __attribute__((__packed__));

#define UVC_DT_CAMERA_TERMINAL_SIZE                18

/* 3.7.2.4. Selector Unit Descriptor */
struct uvc_selector_unit_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bUnitID;
    u8  bNrInPins;
    u8  baSourceID[0];
    u8  iSelector;
} __attribute__((__packed__));

#define UVC_DT_SELECTOR_UNIT_SIZE(n)            (6+(n))

#define UVC_SELECTOR_UNIT_DESCRIPTOR(n)    \
    uvc_selector_unit_descriptor_##n

#define DECLARE_UVC_SELECTOR_UNIT_DESCRIPTOR(n)    \
struct UVC_SELECTOR_UNIT_DESCRIPTOR(n) {        \
    u8  bLength;                    \
    u8  bDescriptorType;                \
    u8  bDescriptorSubType;            \
    u8  bUnitID;                    \
    u8  bNrInPins;                \
    u8  baSourceID[n];                \
    u8  iSelector;                \
} __attribute__ ((packed))

/* 3.7.2.5. Processing Unit Descriptor */
struct uvc_processing_unit_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bUnitID;
    u8  bSourceID;
    u16 wMaxMultiplier;
    u8  bControlSize;
    u8  bmControls[3];
    u8  iProcessing;
    u8  bmVideoStandards;
} __attribute__((__packed__));

#define UVC_DT_PROCESSING_UNIT_SIZE            13

/* 3.7.2.6. Extension Unit Descriptor */
struct uvc_extension_unit_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bUnitID;
    u8  guidExtensionCode[16];
    u8  bNumControls;
    u8  bNrInPins;
    u8  baSourceID[0];
    u8  bControlSize;
    u8  bmControls[0];
    u8  iExtension;
} __attribute__((__packed__));

#define UVC_DT_EXTENSION_UNIT_SIZE(p, n)        (24+(p)+(n))

#define UVC_EXTENSION_UNIT_DESCRIPTOR(p, n) \
    uvc_extension_unit_descriptor_##p_##n

#define DECLARE_UVC_EXTENSION_UNIT_DESCRIPTOR(p, n)    \
struct UVC_EXTENSION_UNIT_DESCRIPTOR(p, n) {        \
    u8  bLength;                    \
    u8  bDescriptorType;                \
    u8  bDescriptorSubType;            \
    u8  bUnitID;                    \
    u8  guidExtensionCode[16];            \
    u8  bNumControls;                \
    u8  bNrInPins;                \
    u8  baSourceID[p];                \
    u8  bControlSize;                \
    u8  bmControls[n];                \
    u8  iExtension;                \
} __attribute__ ((packed))

/* 3.8.2.2. Video Control Interrupt Endpoint Descriptor */
struct uvc_control_endpoint_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u16 wMaxTransferSize;
} __attribute__((__packed__));

#define UVC_DT_CONTROL_ENDPOINT_SIZE            5

/* 3.9.2.1. Input Header Descriptor */
struct uvc_input_header_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bNumFormats;
    u16 wTotalLength;
    u8  bEndpointAddress;
    u8  bmInfo;
    u8  bTerminalLink;
    u8  bStillCaptureMethod;
    u8  bTriggerSupport;
    u8  bTriggerUsage;
    u8  bControlSize;
    u8  bmaControls[];
} __attribute__((__packed__));

#define UVC_DT_INPUT_HEADER_SIZE(p)            (13+(p))

/* 3.9.2.2. Output Header Descriptor */
struct uvc_output_header_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bNumFormats;
    u16 wTotalLength;
    u8  bEndpointAddress;
    u8  bTerminalLink;
    u8  bControlSize;
    u8  bmaControls[];
} __attribute__((__packed__));

#define UVC_DT_OUTPUT_HEADER_SIZE(p)            (9+(p))

/* 3.9.2.6. Color matching descriptor */
struct uvc_color_matching_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bColorPrimaries;
    u8  bTransferCharacteristics;
    u8  bMatrixCoefficients;
} __attribute__((__packed__));

#define UVC_DT_COLOR_MATCHING_SIZE            6

/* 4.3.1.1. Video Probe and Commit Controls */
struct uvc_streaming_control {
    u16 bmHint;
    u8  bFormatIndex;
    u8  bFrameIndex;
    u32 dwFrameInterval;
    u16 wKeyFrameRate;
    u16 wPFrameRate;
    u16 wCompQuality;
    u16 wCompWindowSize;
    u16 wDelay;
    u32 dwMaxVideoFrameSize;
    u32 dwMaxPayloadTransferSize;
    u32 dwClockFrequency;
    u8  bmFramingInfo;
    u8  bPreferedVersion;
    u8  bMinVersion;
    u8  bMaxVersion;
    u8  bUsage;
    u8  bBitDepthLuma;
    u8  bmSettings;
    u8  bMaxNumberOfRefFramesPlus1;
    u16 bmRateControlModes;
    u16 bmLayoutPerStream[4];

} __attribute__((__packed__));

/* Uncompressed Payload - 3.1.1. Uncompressed Video Format Descriptor */
struct uvc_format_uncompressed {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFormatIndex;
    u8  bNumFrameDescriptors;
    u8  guidFormat[16];
    u8  bBitsPerPixel;
    u8  bDefaultFrameIndex;
    u8  bAspectRatioX;
    u8  bAspectRatioY;
    u8  bmInterfaceFlags;
    u8  bCopyProtect;
} __attribute__((__packed__));

#define UVC_DT_FORMAT_UNCOMPRESSED_SIZE            27

/* Uncompressed Payload - 3.1.2. Uncompressed Video Frame Descriptor */
struct uvc_frame_uncompressed {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFrameIndex;
    u8  bmCapabilities;
    u16 wWidth;
    u16 wHeight;
    u32 dwMinBitRate;
    u32 dwMaxBitRate;
    u32 dwMaxVideoFrameBufferSize;
    u32 dwDefaultFrameInterval;
    u8  bFrameIntervalType;
    u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_DT_FRAME_UNCOMPRESSED_SIZE(n)        (26+4*(n))

/* Based Video Format Descriptor */
struct uvc_format_based {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFormatIndex;
    u8  bNumFrameDescriptors;
    u8  guidFormat[16];
    u8  bBitsPerPixel;
    u8  bDefaultFrameIndex;
    u8  bAspectRatioX;
    u8  bAspectRatioY;
    u8  bmInterfaceFlags;
    u8  bCopyProtect;
    u8  bVariableSize;
} __attribute__((__packed__));

#define UVC_DT_FORMAT_BASED_SIZE            28

/* Based Video Frame Descriptor */
struct uvc_frame_based {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFrameIndex;
    u8  bmCapabilities;
    u16 wWidth;
    u16 wHeight;
    u32 dwMinBitRate;
    u32 dwMaxBitRate;
    u32 dwDefaultFrameInterval;
    u8  bFrameIntervalType;
    u32 dwBytesPerLine;
    u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_DT_FRAME_BASED_SIZE(n)        (26+4*(n))

/* MJPEG Payload - 3.1.1. MJPEG Video Format Descriptor */
struct uvc_format_mjpeg {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFormatIndex;
    u8  bNumFrameDescriptors;
    u8  bmFlags;
    u8  bDefaultFrameIndex;
    u8  bAspectRatioX;
    u8  bAspectRatioY;
    u8  bmInterfaceFlags;
    u8  bCopyProtect;
} __attribute__((__packed__));

#define UVC_DT_FORMAT_MJPEG_SIZE            11

/* MJPEG Payload - 3.1.2. MJPEG Video Frame Descriptor */
struct uvc_frame_mjpeg {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFrameIndex;
    u8  bmCapabilities;
    u16 wWidth;
    u16 wHeight;
    u32 dwMinBitRate;
    u32 dwMaxBitRate;
    u32 dwMaxVideoFrameBufferSize;
    u32 dwDefaultFrameInterval;
    u8  bFrameIntervalType;
    u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_DT_FRAME_MJPEG_SIZE(n)            (26+4*(n))

/* H264 Video Format Descriptor */
struct uvc_format_h264 {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFormatIndex;
    u8  bNumFrameDescriptors;
    u8  bDefaultFrameIndex;
    u8  bMaxCodecConfigDelay;
    u8  bmSupportedSliceModes;
    u8  bmSupportedSyncFrameTypes;
    u8  bResolutionScaling;
    u8  Reserved1;
    u8  bmSupportedRateControlModes;

    u16 wMaxMBperSecOneResolutionNoScalability;
    u16 wMaxMBperSecTwoResolutionsNoScalability;
    u16 wMaxMBperSecThreeResolutionsNoScalability;
    u16 wMaxMBperSecFourResolutionsNoScalability;

    u16 wMaxMBperSecOneResolutionTemporalScalability;
    u16 wMaxMBperSecTwoResolutionsTemporalScalability;
    u16 wMaxMBperSecThreeResolutionsTemporalScalability;
    u16 wMaxMBperSecFourResolutionsTemporalScalability;

    u16 wMaxMBperSecOneResolutionTemporalQualityScalability;
    u16 wMaxMBperSecTwoResolutionsTemporalQualityScalability;
    u16 wMaxMBperSecThreeResolutionsTemporalQualityScalability;
    u16 wMaxMBperSecFourResolutionsTemporalQualityScalability;

    u16 wMaxMBperSecOneResolutionTemporalSpatialScalability;
    u16 wMaxMBperSecTwoResolutionsTemporalSpatialScalability;
    u16 wMaxMBperSecThreeResolutionsTemporalSpatialScalability;
    u16 wMaxMBperSecFourResolutionsTemporalSpatialScalability;

    u16 wMaxMBperSecOneResolutionFullScalability;
    u16 wMaxMBperSecTwoResolutionsFullScalability;
    u16 wMaxMBperSecThreeResolutionsFullScalability;
    u16 wMaxMBperSecFourResolutionsFullScalability;
} __attribute__((__packed__));

#define UVC_DT_FORMAT_H264_SIZE            52

/* H264 Video Frame Descriptor */
struct uvc_frame_h264 {
    u8  bLength;
    u8  bDescriptorType;
    u8  bDescriptorSubType;
    u8  bFrameIndex;
    u16 wWidth;
    u16 wHeight;
    u16 wSARwidth;
    u16 wSARheight;
    u16 wProfile;
    u8  bLevelIDC;
    u16 wConstrainedToolset;
    u32 bmSupportedUsages;
    u16 bmCapabilities;
    u32 bmSVCCapabilities;
    u32 bmMVCCapabilities;
    u32 dwMinBitRate;
    u32 dwMaxBitRate;
    u32 dwDefaultFrameInterval;
    u8  bNumFrameIntervals;
    u32 dwFrameInterval[];
} __attribute__((__packed__));

#define UVC_DT_FRAME_H264_SIZE(n)            (44+4*(n))

/* H264 Video Config Probe and Commit */
struct uvc_h264_video_config {
    u32 dwFrameInterval;
    u32 dwBitRate;
    u16 bmHints;
    u16 wConfigurationIndex;
    u16 wWidth;
    u16 wHeight;
    u16 wSliceUnits;
    u16 wSliceMode;
    u16 wProfile;
    u16 wIFramePeriod;
    u16 wEstimatedVideoDelay;
    u16 wEstimatedMaxConfigDelay;
    u8  bUsageType;
    u8  bRateControlMode;
    u8  bTemporalScaleMode;
    u8  bSpatialScaleMode;
    u8  bSNRScaleMode;
    u8  bStreamMuxOption;
    u8  bStreamFormat;
    u8  bEntropyCABAC;
    u8  bTimestamp;
    u8  bNumOfReorderFrames;
    u8  bPreviewFlipped;
    u8  bView;
    u8  bReserved1;
    u8  bReserved2;
    u8  bStreamID;
    u8  bSpatialLayerRatio;
    u16 wLeakyBucketSize;
} __attribute__((__packed__));

#define UVC_H264_VIDEO_CONFIG_SIZE    46

#endif /* __USB_UVC_H */
