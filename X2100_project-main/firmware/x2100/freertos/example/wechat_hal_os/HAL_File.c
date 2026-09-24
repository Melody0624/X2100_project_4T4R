/**
 * @file HAL_File_Linux.c
 * @author {hubert} ({hubertxxu@tencent.com})
 * @brief
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright
 *
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright(C) 2018 - 2021 THL A29 Limited, a Tencent company.All rights reserved.
 *
 * Licensed under the MIT License(the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @par Change File:
 * <table>
 * Date				Version		Author			Description
 * 2022-01-11		1.0			hubertxxu		first commit
 * </table>
 */

#include <dfs_posix.h>
// #include "dirent.h"
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include "HAL_Platform.h"
// #include "utils_log.h"

// #define INTERNAL_ADAPT 1


/**
 * @brief open file
 * @param[in] filename file path name
 * @param[in] mode file open mode see @FsOpenMode

 * @return @see IotReturnCode
 */
int32_t HAL_FileOpen(const char *file_name, FsOpenMode mode, uint64_t *fd)
{
    FILE *fp;

    if (FS_OPEN_MODE_R == mode) {
        fp = fopen(file_name, "rb");
        if (!fp) {
            UPLOAD_ERR("open file %s failed mode : %d", file_name, mode);
            return ERR_CODE_GENERALFAIL;
        }
    } else if (FS_OPEN_MODE_RW == mode) {
        fp = fopen(file_name, "rb+");
        if (!fp) {
            fp = fopen(file_name, "wb+");
            if (!fp) {
                UPLOAD_ERR("open file %s failed mode : %d", file_name, mode);
                return ERR_CODE_GENERALFAIL;
            }
        }
    } else {
        UPLOAD_ERR("open file %s failed mode %d invalid", file_name, mode);
        return ERR_CODE_INVALIDPARAM;
    }
    *fd = (uint64_t)fp;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief file seek
 * @param[in] fd file fd
 * @param[in] offset fd offset
 * @param[in] whence @see FsSeekWhence

 * @return @see IotReturnCode
 */
int32_t HAL_FileSeek(uint64_t fd, uint32_t offset, FsSeekWhence whence)
{
    FILE *fp;
    int32_t rc;

    fp = (FILE *)fd;
    if (whence == FS_SEEK_BEGIN) {
        rc = fseek(fp, offset, SEEK_SET);
    } else if (whence == FS_SEEK_CUR) {
        rc = fseek(fp, offset, SEEK_CUR);
    } else {
        rc = fseek(fp, offset, SEEK_END);
    }
    if (rc) {
        return ERR_CODE_GENERALFAIL;
    }
    return ERR_CODE_SUCCESS;
}

/**
 * @brief close file
 * @param[in] filename file path name
 * @param[in] mode file open mode see @FsOpenMode

 * @return @see IotReturnCode
 */
int32_t HAL_FileClose(uint64_t fd)
{
    FILE *fp;

    fp = (FILE *)fd;
    fclose(fp);

    return ERR_CODE_SUCCESS;
}

/**
 * @brief Functions for saving file into NVS(files/FLASH)
 * @param[in] fd file fd
 * @param[in] buf source need write buffer
 * @param[in] write_len length of file to write
 * @param[out] written_len acturelly written len
 * @return @see IotReturnCode
 */
int32_t HAL_FileWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t *written_len)
{
    int32_t rc;
    uint32_t len;
    FILE *fp = (FILE *)fd;
    rc = ERR_CODE_SUCCESS;

    len = fwrite(buf, 1, write_len, fp);
    fflush(fp);
    if (len < 0) {
       rc = ERR_CODE_FS_WRITEFAIL;
    } else {
        *written_len = len;
    }
    return rc;
}

/**
 * @brief Functions for reading file from NVS(files/FLASH)
 * @param[in] fd file fd
 * @param[in] buf destination buffer to store readed data
 * @param[in] read_len length to read
 * @param[out] readed_len acturelly readed len
 * @return @see IotReturnCode
 */
int32_t HAL_FileRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t *readed_len)
{
    int32_t len;
    int32_t rc;
    FILE *fp = (FILE *)fd;
    rc = ERR_CODE_SUCCESS;

    len = fread(buf, 1, read_len, fp);
    if (len > 0) {
        // 读成功
        *readed_len = len;
    } else if (0 == len && feof(fp)) {
        // 已经到了文件的尾部
        *readed_len = 0;
        rc = ERR_CODE_FS_ENDFILE;
    } else {
        rc = ERR_CODE_FS_READFAIL;
    }
    return rc;
}

/**
 * @brief get file char value
 * @param[in] fd file fd

 * @return -1 for failed, others for char value
 */
int32_t HAL_FileGetChar(uint64_t fd)
{
    int32_t rc;
    uint8_t char_value;
    uint32_t readed_len;

    rc = HAL_FileRead(fd, &char_value, 1, &readed_len);
    // 只有读到文件才能正确返回
    if (!rc && readed_len == 1) {
        return char_value;
    }
    // 读错或者读到文件尾部均返回-1
    return -1;
}

/**
 * @brief Functions for deleting file in NVS(files/FLASH).
 * @param[in] file_name file path name
 * @return @see IotReturnCode
 */
int32_t HAL_FileDel(const char *file_name)
{
    int32_t rc;

    rc = remove(file_name);
    if (rc) {
        return ERR_CODE_GENERALFAIL;
    } else {
        return ERR_CODE_SUCCESS;
    }
}

/**
 * @brief Functions for reading the size of file in NVS(files/FLASH).
 * @param[in] filename file path name
 * @return @see IotReturnCode
 */
int32_t HAL_FileGetSize(const char *file_name, uint32_t *file_size)
{
   struct stat file_info;

   if (stat(file_name, &file_info) == 0) {
        *file_size = file_info.st_size;
        return ERR_CODE_SUCCESS;
    } else {
        *file_size = 0;
        return ERR_CODE_GENERALFAIL;
    }
}

/**
 * @brief rename file
 * @param[in] src_path source file
 * @param[in] dst_path destination file

 * @return 0 for success, others for failure
 */
int32_t HAL_FileRename(const char *src_path, const char *dst_path)
{
    int32_t rc;

    rc = rename(src_path, dst_path);
    if (rc) {
        return ERR_CODE_GENERALFAIL;
    } else {
        return ERR_CODE_SUCCESS;
    }
}

/**
 * @brief open directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileOpenDir(const char *dir_path, uint64_t *fd)
{
    DIR *dir_fd;

    dir_fd = opendir(dir_path);
    if (NULL == dir_fd) {
        return ERR_CODE_FS_DIRNOTEXIST;
    }
    *fd = (uint64_t)dir_fd;

    return ERR_CODE_SUCCESS;
}

/**
 * @brief close directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileCloseDir(uint64_t fd)
{
    DIR *dir_fd;
    int32_t rc;

    dir_fd = (DIR *)fd;
    rc = closedir(dir_fd);
    if (!rc) {
        return ERR_CODE_SUCCESS;
    }

    return ERR_CODE_FS_OTHERS;
}

/**
 * @brief close directory
 * @param[in] dir_path directory path name
 * @param[out] fd directory fd

 * @return @see IotReturnCode
 */
int32_t HAL_FileReadDir(uint64_t fd, uint8_t *file_name, uint32_t file_name_len)
{
    DIR *dir_fd;
    struct dirent *entry;

    dir_fd = (DIR *)fd;
    entry = readdir(dir_fd);
    if (NULL == entry) {
        return ERR_CODE_FS_DIREND;
    }
    strncpy((char *)file_name, entry->d_name, file_name_len);

    return ERR_CODE_SUCCESS;
}

int32_t HAL_InternalFileSizeGet(const char *path, uint32_t *size)
{
#ifdef INTERNAL_ADAPT
    if (!path || !size) {
        return ERR_CODE_INVALIDPARAM;
    }

    return HAL_FileGetSize(path, size);
#else
    return ERR_CODE_SUCCESS;
#endif
}

int32_t HAL_InternalFileDel(const char *path)
{
#ifdef INTERNAL_ADAPT
    if (!path) {
        return ERR_CODE_INVALIDPARAM;
    }

    return HAL_FileDel(path);
#else
    return ERR_CODE_SUCCESS;
#endif
}

int32_t HAL_InternalFileWrite(const char *path, const void *data, uint32_t data_len, uint32_t *write_len)
{
#ifdef INTERNAL_ADAPT
    uint64_t fd = 0;
    int32_t rc;

    if (!path || !data || !write_len) {
        return ERR_CODE_INVALIDPARAM;
    }

    rc = HAL_FileOpen(path, FS_OPEN_MODE_RW, &fd);
    if (rc != ERR_CODE_SUCCESS) {
        return ERR_CODE_GENERALFAIL;
    }

    rc = HAL_FileWrite(fd, data, data_len, write_len);
    HAL_FileClose(fd);

    return rc ? ERR_CODE_GENERALFAIL : ERR_CODE_SUCCESS;
#else
    return ERR_CODE_SUCCESS;
#endif
}

int32_t HAL_InternalFileRead(const char *path, void *buf, uint32_t read_len, uint32_t *readed_len)
{
#ifdef INTERNAL_ADAPT
    uint64_t fd = 0;
    int32_t rc;

    if (!path || !buf || !readed_len) {
        return ERR_CODE_INVALIDPARAM;
    }

    rc = HAL_FileOpen(path, FS_OPEN_MODE_R, &fd);
    if (rc != ERR_CODE_SUCCESS) {
        return ERR_CODE_GENERALFAIL;
    }

    rc = HAL_FileRead(fd, buf, read_len, readed_len);
    HAL_FileClose(fd);

    return rc ? ERR_CODE_GENERALFAIL : ERR_CODE_SUCCESS;
#else
    return ERR_CODE_SUCCESS;
#endif
}
