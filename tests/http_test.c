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

    puts("HTTP User-Agent tests passed");
    return 0;
}
