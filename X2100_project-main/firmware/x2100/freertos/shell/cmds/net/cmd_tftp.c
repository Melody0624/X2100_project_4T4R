#include <stdio.h>
#include <shell.h>

#include "lwip/apps/tftp_client.h"
#include "lwip/apps/tftp_server.h"

#include <string.h>


static void *tftp_open_file(const char* fname, u8_t file_is_write)
{
  if (file_is_write) {
    return (void*)fopen(fname, "wb");
  } else {
    return (void*)fopen(fname, "rb");
  }
}

static void *tftp_open(const char* fname, const char* mode, u8_t file_is_write)
{
  LWIP_UNUSED_ARG(mode);
  return tftp_open_file(fname, file_is_write);
}

static void tftp_close(void* handle)
{
  fclose((FILE*)handle);
}

static int tftp_read(void* handle, void* buf, int bytes)
{
  int ret = fread(buf, 1, bytes, (FILE*)handle);
  if (ret <= 0) {
    return -1;
  }
  return ret;
}

static int tftp_write(void* handle, struct pbuf* p)
{
  while (p != NULL) {
    if (fwrite(p->payload, 1, p->len, (FILE*)handle) != (size_t)p->len) {
      return -1;
    }
    p = p->next;
  }

  return 0;
}

/* For TFTP client only */
static void tftp_error(void* handle, int err, const char* msg, int size)
{
  char message[100];

  LWIP_UNUSED_ARG(handle);

  memset(message, 0, sizeof(message));
  MEMCPY(message, msg, LWIP_MIN(sizeof(message)-1, (size_t)size));

  printf("TFTP error: %d (%s)", err, message);
}

static const struct tftp_context tftp = {
  tftp_open,
  tftp_close,
  tftp_read,
  tftp_write,
  tftp_error
};

void tftp_usage(void)
{
    printf("Usage: \n");
    printf("      tftp get <remote_ip> <remote_file> [local_file] \n");
    printf("      tftp put <remote_ip> <local_file>  [remote_file]\n");
}

void cmd_func_tftp(struct cmd_arg *arg, int argc, char **argv)
{
    int ret;
    err_t err;
    void *file_handle;
    ip_addr_t ipaddr;
    int file_is_write = 0;
    char *local_file = NULL;
    char *remote_file = NULL;
    char *remote_ip = NULL;

    if (argc < 4 || !strcmp(argv[1], "-h")) {
        tftp_usage();
        return;
    }

    remote_ip = argv[2];

    if (!strncmp(argv[1], "get", strlen("get"))) {
        file_is_write = 1;
        remote_file = argv[3];
        if (argc == 5)
          local_file = argv[4];
        else
          local_file = remote_file;
    }

    if (!strncmp(argv[1], "put", strlen("put"))) {
        file_is_write = 0;
        local_file = argv[3];
        if (argc == 5)
          remote_file = argv[4];
        else
          remote_file = local_file;
    }

    ret = ipaddr_aton(remote_ip, &ipaddr);
    LWIP_ERROR("ipaddr_aton failed", ret == 1, return);

    file_handle = tftp_open_file(local_file, file_is_write);
    LWIP_ERROR("failed to create file", file_handle != NULL, return);

    if (file_is_write) {
      err = tftp_get(file_handle, &ipaddr, TFTP_PORT, remote_file, TFTP_MODE_OCTET);
      LWIP_ERROR("tftp_get failed", err == ERR_OK, return);
    } else {
      err = tftp_put(file_handle, &ipaddr, TFTP_PORT, remote_file, TFTP_MODE_OCTET);
      LWIP_ERROR("tftp_get failed", err == ERR_OK, return);
    }
}

void cmd_tftp_init(void)
{
    err_t err;

    err = tftp_init_client(&tftp);
    LWIP_ERROR("tftp_init_client failed", err == ERR_OK, return);

    shell_cmd_register(cmd_func_tftp, "tftp", NULL, "tftp");
}