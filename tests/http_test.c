#include "http.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char agent[64];

    const char request[] =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: curl/8.0\r\n"
        "\r\n";

    assert(extract_http_user_agent(request, sizeof request - 1, agent, sizeof agent));
    assert(strcmp(agent, "curl/8.0") == 0);

    const char no_agent[] =
        "GET / HTTP/1.1\r\n"
        "X-User-Agent: fake\r\n"
        "\r\n";

    assert(!extract_http_user_agent(no_agent, sizeof no_agent - 1, agent, sizeof agent));
    assert(agent[0] == '\0');

	const char incomplete[] =
		"GET / HTTP/1.1\r\n"
		"User-Agent: unfinished";

	assert(!extract_http_user_agent(incomplete, sizeof incomplete - 1, agent, sizeof agent));

	const char mixed_case[] =
		"GET / HTTP/1.1\r\n"
		"uSeR-aGeNt:  scanner/1.2 \t\r\n"
		"\r\n";

	assert(extract_http_user_agent(mixed_case, sizeof mixed_case - 1, agent, sizeof agent));
	assert(strcmp(agent, "scanner/1.2") == 0);

	char small_buffer[5];

	assert(extract_http_user_agent(mixed_case, sizeof mixed_case - 1, small_buffer, sizeof small_buffer));
	assert(strcmp(small_buffer, "scan") == 0);


	const char complete_request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\nBODY";

	size_t header_end = 0;

	assert(find_http_header_end(
		complete_request,
		sizeof complete_request - 1,
		&header_end));

	assert(header_end == sizeof complete_request - 1 - 4);
	assert(memcmp(complete_request + header_end, "BODY", 4) == 0);


	const char http_request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	const char non_http[] = "SSH-2.0-OpenSSH_9.0\r\n";
	const char incomplete_request_line[] = "GET / HTTP/1.1";

	assert(is_http1_request(http_request, sizeof http_request - 1));
	assert(!is_http1_request(non_http, sizeof non_http - 1));
	assert(!is_http1_request(incomplete, sizeof incomplete_request_line - 1));


	const char complete_get[] =
    "GET / HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "\r\n";

	assert(http_request_complete(complete_get, sizeof complete_get - 1));


	const char partial_post[] =
    "POST /login HTTP/1.1\r\n"
    "Content-Length: 11\r\n"
    "\r\n"
    "hello";

	assert(!http_request_complete(partial_post, sizeof partial_post - 1));


	const char complete_post[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 11\r\n"
		"\r\n"
		"hello=world";

	assert(http_request_complete(complete_post, sizeof complete_post - 1));


	const char invalid_length[] =
    "POST / HTTP/1.1\r\n"
    "Content-Length: 999999999999999999999999999999999999\r\n"
    "\r\n";

	assert(!http_request_complete(invalid_length, sizeof invalid_length - 1));


	const char chunked_request[] =
    "POST / HTTP/1.1\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "5\r\nhello\r\n0\r\n\r\n";

	assert(!http_request_complete(chunked_request, sizeof chunked_request - 1));


    puts("HTTP tests passed");
    return 0;
}
