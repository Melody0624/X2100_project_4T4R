
#include <shell.h>
#include <driver/camera.h>
#include <printf.h>
#include <dump_mem.h>

static struct camera_device *camera;
static struct camera_info *info;

static void cmd_camera_dump(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    if (!camera) {
        printf("camera not found\n");
        return;
    }

    camera_skip_frames(camera, 0xffffffff);

    char *buf = camera_wait_frame(camera);

    printf_disable_time_stamp();

    printf("frm buffer %p\n", buf);
    if (buf)
        dump_mem32_c_style(buf, info->frame_size, 8);

    camera_put_frame(camera, buf);

    printf_enable_time_stamp();
}

static void cmd_camera_start(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    if (argc != 3
            || !(argv[1][0] == '0' || argv[1][0] == '1')
            || !(argv[2][0] == '0' || argv[2][0] == '1')) {
        printf("usage: camera_start <disable/enable> <index>\n");
        printf("usage: camera_start [0/1] [0/1]\n");
        return;
    }

    int index = argv[2][0] - '0';

    if (!camera) {
        camera = camera_detect(index);
        if (!camera) {
            printf("camera not found\n");
            return;
        }
        info = camera_get_info(camera);
    }

    if (argv[1][0] == '1') {
        camera_power_on(camera);
        camera_stream_on(camera);
    } else {
        camera_stream_off(camera);
        camera_power_off(camera);
    }
}

void cmd_camera_init(void)
{
    shell_cmd_register(cmd_camera_dump, "camera_dump", NULL, NULL);

    shell_cmd_register(cmd_camera_start, "camera_start", NULL, NULL);
}
