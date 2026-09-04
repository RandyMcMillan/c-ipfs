#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <curl/curl.h>

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void test_kubo_version_endpoint(void) {
    CURL *curl_handle = curl_easy_init();
    assert(curl_handle != NULL);

    struct MemoryStruct chunk = { malloc(1), 0 };

    curl_easy_setopt(curl_handle, CURLOPT_URL, "http://127.0.0.1:5011/api/v0/version");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl_handle);
    assert(res == CURLE_OK);

    assert(strstr(chunk.memory, "cipfs") != NULL);

    free(chunk.memory);
    curl_easy_cleanup(curl_handle);
    printf("PASS: test_kubo_version_endpoint\n");
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    /* Note: Assumes ipfs_start_http_rpc_server is running on port 5011 */
    test_kubo_version_endpoint();

    curl_global_cleanup();
    return 0;
}
