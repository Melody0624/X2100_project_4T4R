#include <shell.h>
#include <driver/hash.h>
#include <fcntl.h>
#include <string.h>

#define MAXBUFSIZE 128

static void cmd_func_hash_help(char *cmd)
{
    printf("Usage1: \t%s <mode> <pathname>\n", cmd);
    printf("Example1:\n");
    printf("\t%s MD5 <pathname>\n", cmd);
    printf("\t%s SHA1 <pathname>\n", cmd);
    printf("\t%s SHA224 <pathname>\n", cmd);
    printf("\t%s SHA256 <pathname>\n", cmd);
    printf("Usage2:%s [-h/--help]\n", cmd);
    printf("Example2:\n");
    printf("\t%s --help\n", cmd);
}

void cmd_func_hash(struct cmd_arg *arg, int argc, char **argv)
{
    enum encryption_mode mode;
    int ret, fd, num;
    int file_fd;
    const char *pathname = argv[2];
    unsigned long hash_size;
    unsigned char hash_result[50];
    unsigned char buff[MAXBUFSIZE];
    memset(hash_result, 0, sizeof(hash_result));

    if (argc < 3)
        cmd_func_hash_help(argv[0]);

    if (strcmp(argv[1], "MD5") == 0) {
        mode = MD5;
        hash_size = MD5BYTE;
    }else if (strcmp(argv[1], "SHA1") == 0) {
        mode = SHA1;
        hash_size = SHA1BYTE;
    }else if (strcmp(argv[1], "SHA224") == 0) {
        mode = SHA224;
        hash_size = SHA224BYTE;
    }else if (strcmp(argv[1], "SHA256") == 0) {
        mode = SHA256;
        hash_size = SHA256BYTE;
    }else {
        cmd_func_hash_help(argv[0]);
    }

    if (access(pathname, F_OK) != 0) {
        printf("HASH:file %s is not exist\n", pathname);
        cmd_func_hash_help(argv[0]);
    }

    fd = hash_request(mode, 0);
    if (fd == 0) {
        printf("HASH: hash request error! \n");
        return;
    }

    file_fd = open(pathname, O_RDONLY);
    if (file_fd < 0) {
        printf("HASH: open file %s failed\n", pathname);
        return;
    }

    while ((num = read(file_fd, buff, MAXBUFSIZE)) > 0) {
        ret = hash_write(fd, buff, num);
        if (ret < 0) {
            printf("HASH: hash write error! \n");
            hash_read_free(fd, hash_result, hash_size);
            close(file_fd);
            return;
        }
    }
    close(file_fd);

    ret = hash_read_free(fd, hash_result, hash_size);
    if (ret < 0) {
        printf("HASH: hash read error! \n");
        return;
    }

    for (int i = 0;i < hash_size;i++) {
        printf("%02x", hash_result[i]);
    }
    printf("\n");

}

void cmd_hash_init(void)
{
    shell_cmd_register(cmd_func_hash,    "hash",   NULL,    "hash digest algorithm");
}