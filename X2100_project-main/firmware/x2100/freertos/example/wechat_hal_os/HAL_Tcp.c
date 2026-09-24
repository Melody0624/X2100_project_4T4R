/**
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
 * @file HAL_TCP_linux.c
 * @brief Linux tcp api
 * @author fancyxu (fancyxu@tencent.com)
 * @version 1.0
 * @date 2021-05-31
 *
 * @par Change Log:
 * <table>
 * <tr><th>Date       <th>Version <th>Author    <th>Description
 * <tr><td>2021-05-31 <td>1.0     <td>fancyxu   <td>first commit
 * <tr><td>2021-07-09 <td>1.1     <td>fancyxu   <td>refactor for support tls, change port to str format
 * </table>
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "HAL_Platform.h"
#include "utils_log.h"

/**
 * @brief TCP connect in linux
 *
 * @param[in] ip ip to conenct
 * @param[in] port port to connect
 * @param[in] timeout_ms connect timeout ms
 * @param[out] fd tcp socket fd
 * @return @see IotReturnCode
 */
int32_t HAL_TcpConnect(const char *ip, uint16_t port, uint32_t timeout_ms, uint64_t *fd)
{
    int32_t socket_fd = 0;
    // to avoid process crash when writing to a broken socket
    signal(SIGPIPE, SIG_IGN);

    int32_t rc = ERR_CODE_SUCCESS;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        UPLOAD_ERR("setting ip addr failed");
        return ERR_CODE_INVALIDPARAM;
    }

    socket_fd = (int32_t)socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return ERR_CODE_TCP_FDNOTENOUGH;
    }
    *fd = socket_fd;

    rc = fcntl(*fd, F_SETFL, fcntl(*fd, F_GETFL) | O_NONBLOCK);
    if (rc) {
        UPLOAD_ERR("set socket non block faliled %d", rc);
        close(*fd);
        rc = ERR_CODE_TCP_CONNECTFAIL;
        return rc;
    }

    rc = connect(*fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (!rc) {
        rc = ERR_CODE_SUCCESS;
        return rc;
    }

    if (errno == EINPROGRESS) {
        // IO select to wait for connect result
        struct timeval timeout;
        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_usec = timeout_ms % 1000;

        fd_set sets;
        FD_ZERO(&sets);
        FD_SET(*fd, &sets);

        rc = select(*fd + 1, NULL, &sets, NULL, &timeout);
        if (rc > 0) {
            int       so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(*fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (FD_ISSET(*fd, &sets) && so_error == 0) {
                rc = ERR_CODE_SUCCESS;
                return rc;
            }
        }
    }

    close(*fd);
    rc = ERR_CODE_TCP_CONNECTFAIL;

    return rc;
}

/**
 * @brief TCP disconnect
 *
 * @param[in] fd socket fd
 * @return @see IotReturnCode
 */
int32_t HAL_TcpDisconnect(uint64_t fd)
{
    int rc;

    /* Shutdown both send and receive operations. */
    rc = shutdown(fd, 2);
    if (rc) {
        UPLOAD_ERR("shutdown error: %s", strerror(errno));
    }

    rc = close(fd);
    if (rc) {
        UPLOAD_ERR("closesocket error: %s", strerror(errno));
        return ERR_CODE_GENERALFAIL;
    }

    return ERR_CODE_SUCCESS;
}

/**
 * @brief TCP write
 *
 * @param[in] fd socket fd
 * @param[in] buf buf to write
 * @param[in] write_len want write len
 * @param[in] timeout_ms timeout
 * @param[out] written_len data written length
 * @return @see IotReturnCode
 */
int32_t HAL_TcpWrite(uint64_t fd, const void *buf, uint32_t write_len, uint32_t timeout_ms, uint32_t *written_len)
{
    int32_t        rc = ERR_CODE_SUCCESS;
    uint32_t       len_sent;
    fd_set         sets;
    struct timeval timeout;
    uint64_t       time_expired_ms, time_current_ms;

    HAL_TimeSysTickMsGet(&time_current_ms);
    time_expired_ms = time_current_ms + timeout_ms;
    len_sent = 0;

    /* send one time if timeout_ms is value 0 */
    do  {
        HAL_TimeSysTickMsGet(&time_current_ms);
        timeout.tv_sec  = (time_expired_ms - time_current_ms) / 1000;
        timeout.tv_usec = (time_expired_ms - time_current_ms) % 1000 * 1000;

        FD_ZERO(&sets);
        FD_SET(fd, &sets);

        rc = select(fd + 1, NULL, &sets, NULL, &timeout);
        if (!rc) {
            rc = ERR_CODE_TCP_WRITETIMEOUT;
            UPLOAD_ERR("select-write timeout %d", (int32_t)fd);
            break;
        }

        if (rc < 0) {
            if (EINTR != errno) {
                rc = ERR_CODE_TCP_WRITEFAIL;
                UPLOAD_ERR("select-write fail: %s", strerror(errno));
                break;
            }
            UPLOAD_ERR("EINTR be caught");
            continue;
        }

        rc = send(fd, buf + len_sent, write_len - len_sent, 0);
        if (rc < 0) {
            if (EINTR == errno) {
                UPLOAD_ERR("EINTR be caught");
                continue;
            }
            rc = (EPIPE == errno || ECONNRESET == errno) ? ERR_CODE_TCP_PEERSHUTDOWN : ERR_CODE_TCP_WRITEFAIL;
            UPLOAD_ERR("send fail: %s", strerror(errno));
            break;
        }

        len_sent += rc;

        HAL_TimeSysTickMsGet(&time_current_ms);
        if (time_current_ms > time_expired_ms) {
            break;
        }
    } while (len_sent < write_len);


    *written_len = (uint32_t)len_sent;
    // We always know hom much should write.
    return len_sent == write_len ? ERR_CODE_SUCCESS : rc;
}

/**
 * @brief TCP read.
 *
 * @param[in] fd socket fd
 * @param[out] buf buffer to save read data
 * @param[in] read_len read buffer len
 * @param[in] timeout_ms timeout
 * @param[out] read_len length of data readed
 * @return @see IotReturnCode
 */
int32_t HAL_TcpRead(uint64_t fd, void *buf, uint32_t read_len, uint32_t timeout_ms, uint32_t *readed_len)
{
    int32_t        rc;
    uint32_t       len_recv;
    fd_set         sets;
    struct timeval timeout;
    uint64_t       time_expired_ms, time_current_ms;

    HAL_TimeSysTickMsGet(&time_current_ms);
    time_expired_ms = time_current_ms + timeout_ms;
    len_recv = 0;

    do {
        FD_ZERO(&sets);
        FD_SET(fd, &sets);

        timeout.tv_sec  = (time_expired_ms - time_current_ms) / 1000;
        timeout.tv_usec = (time_expired_ms - time_current_ms) % 1000 * 1000;

        rc = select(fd + 1, &sets, NULL, NULL, &timeout);
        if (!rc) {
            rc = ERR_CODE_TCP_READTIMEOUT;
            break;
        }

        if (rc < 0) {
            if (EINTR != errno) {
                rc = ERR_CODE_TCP_READFAIL;
                UPLOAD_ERR("select-recv fail: %s", strerror(errno));
                break;
            }
            UPLOAD_ERR("EINTR be caught");
            continue;
        }

        rc = recv(fd, buf + len_recv, read_len - len_recv, 0);
        if (rc <= 0) {
            if (!rc) {
                UPLOAD_ERR("connection is closed by server");
                rc = ERR_CODE_TCP_PEERSHUTDOWN;
                break;
            }

            if (EINTR == errno) {
                UPLOAD_ERR("EINTR be caught");
                continue;
            }
            UPLOAD_ERR("recv error: %s", strerror(errno));
            rc = (EPIPE == errno || ECONNRESET == errno) ? ERR_CODE_TCP_PEERSHUTDOWN : ERR_CODE_TCP_READFAIL;
            break;
        }

        len_recv += rc;

        HAL_TimeSysTickMsGet(&time_current_ms);
        if (time_current_ms > time_expired_ms) {
            break;
        }
    } while (len_recv < read_len);

    *readed_len = (uint32_t)len_recv;

    if (rc == ERR_CODE_TCP_READTIMEOUT && len_recv == 0) {
        rc = ERR_CODE_TCP_NOTHINGTOREAD;
    }
    // We always don't know hom much should read.
    return (len_recv > 0) ? ERR_CODE_SUCCESS : rc;
}

/**
 * @brief dns resolve.
 *
 * @param[in] domain domain name
 * @param[out] ip_list ip list of the given domain
 * @param[in] ip_list_len buf to save the ip list
 * @param[in] timeout_ms timeout
 * @return @see IotReturnCode
 */
int32_t HAL_DnsResolve(const char *domain, void *ip_list, uint32_t ip_list_len, uint32_t timeout_ms)
{
    int32_t status;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    char ip_str[128];
    uint32_t len = 0;
    char *ptr = (char *)ip_list;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (NULL == ip_list || ip_list_len < 16) {
        return ERR_CODE_INVALIDPARAM;
    }

    status = getaddrinfo(domain, NULL, &hints, &result);
    if (status) {
        UPLOAD_ERR("getaddrinfo error : %s", gai_strerror(status));
        return ERR_CODE_TCP_DNSFAIL;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        void *addr;

        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr = (void *)&(ipv4->sin_addr);
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr = (void *)&(ipv6->sin6_addr);
        }

        memset(ip_str, 0, sizeof(ip_str));
        inet_ntop(rp->ai_family, addr, ip_str, sizeof(ip_str));
        if (len + strlen(ip_str) > ip_list_len) {
            break;
        }
        if (len) {
            ptr[len] = ';';
            len += 1;
        }
        memcpy(&ptr[len], ip_str, strlen(ip_str));
        len += strlen(ip_str);
    }

    freeaddrinfo(result);
    return ERR_CODE_SUCCESS;
}
