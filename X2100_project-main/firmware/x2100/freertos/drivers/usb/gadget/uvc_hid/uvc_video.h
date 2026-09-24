#ifndef __UVC_VIDEO_H__
#define __UVC_VIDEO_H__

struct uvc_video;

void uvcg_video_pump(struct uvc_video *video);

int uvcg_video_enable(struct uvc_video *video, int enable);

#endif /* __UVC_VIDEO_H__ */
