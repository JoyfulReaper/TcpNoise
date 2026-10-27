#include "network.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>

static void test_receive_timeout(void)
{
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    char buffer[64];
    ssize_t received = 0;

    enum receive_status status =
        receive_payload(sockets[0], buffer, sizeof buffer, &received);

    assert(status == RECEIVE_TIMEOUT);
    assert(received == 0);

    close(sockets[0]);
    close(sockets[1]);
}

static void test_buffer_limit(void)
{
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const char payload[] = "ABCDEFGHIJK";
    assert(send(sockets[1], payload, sizeof payload - 1, 0) == sizeof payload - 1);

    char buffer[4];
    ssize_t received = 0;

    enum receive_status status =
        receive_payload(sockets[0], buffer, sizeof buffer, &received);

    assert(status == RECEIVE_DATA);
    assert(received == sizeof buffer);
    assert(memcmp(buffer, "ABCD", sizeof buffer) == 0);

    close(sockets[0]);
    close(sockets[1]);
}

static void test_split_request(void)
{
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const char first[] = "GET / HTTP/1.1\r\n";
    const char second[] = "User-Agent: SplitTest/1.0\r\n\r\n";

    pid_t child = fork();
    assert(child >= 0);

    if (child == 0)
    {
        close(sockets[0]);

        assert(send(sockets[1], first, sizeof first - 1, 0) == sizeof first - 1);

        struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000 };
        assert(nanosleep(&delay, NULL) == 0);

        assert(send(sockets[1], second, sizeof second - 1, 0) == sizeof second - 1);

        close(sockets[1]);
        _exit(0);
    }

    close(sockets[1]);

    char buffer[128];
    ssize_t received = 0;

    enum receive_status status =
        receive_payload(sockets[0], buffer, sizeof buffer, &received);

    size_t expected_length = sizeof first - 1 + sizeof second - 1;

    assert(status == RECEIVE_DATA);
    assert(received == (ssize_t)expected_length);
    assert(memcmp(buffer, first, sizeof first - 1) == 0);
    assert(memcmp(buffer + sizeof first - 1, second, sizeof second - 1) == 0);

    close(sockets[0]);

    int child_status;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status));
    assert(WEXITSTATUS(child_status) == 0);
}

int main(void)
{
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const char request[] = "GET / HTTP/1.1\r\n\r\n";
    size_t request_len = sizeof request - 1;

    assert(send(sockets[1], request, request_len, 0) == (ssize_t)request_len);
    assert(shutdown(sockets[1], SHUT_WR) == 0);

    char buffer[64];
    ssize_t received = 0;

    enum receive_status status =
        receive_payload(sockets[0], buffer, sizeof buffer, &received);

    assert(status == RECEIVE_DATA);
    assert(received == (ssize_t)request_len);
    assert(memcmp(buffer, request, request_len) == 0);

    close(sockets[0]);
    close(sockets[1]);

	test_split_request();

	test_receive_timeout();
	test_buffer_limit();

    puts("Network tests passed");
    return 0;
}
