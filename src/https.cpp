#include <cstdlib>
#include <curl/curl.h>
#include <iostream>
#include <limits>
#include <memory>

#include "https.h"

namespace
{
std::size_t write_response(char *data, std::size_t size, std::size_t count,
                           void *) noexcept
{
    const auto limit =
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());
    if (size != 0 && count > limit / size)
    {
        return 0;
    }
    const auto length = size * count;
    try
    {
        std::cout.write(data, static_cast<std::streamsize>(length));
        return std::cout ? length : 0;
    }
    catch (...)
    {
        // Exceptions must not cross libcurl's C callback boundary.
        return 0;
    }
}
} // namespace

namespace https
{
int POST(const char *server_address, const char *data, const char *ca_file)
{
    if (server_address == nullptr || data == nullptr)
    {
        std::cerr << "POST requires a URL and a body\n";
        return EXIT_FAILURE;
    }

    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK)
    {
        std::cerr << "curl initialization: " << curl_easy_strerror(res) << '\n';
        return EXIT_FAILURE;
    }

    // Release resources automatically on every return path, in reverse order.
    struct GlobalCleanup
    {
        ~GlobalCleanup()
        {
            curl_global_cleanup();
        }
    } global_cleanup;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        nullptr, &curl_slist_free_all);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
        curl_easy_init(), &curl_easy_cleanup);
    if (!curl)
    {
        std::cerr << "Could not create HTTP request\n";
        return EXIT_FAILURE;
    }
    headers.reset(curl_slist_append(nullptr, "Content-Type: application/json"));
    if (!headers)
    {
        std::cerr << "Could not allocate HTTP headers\n";
        return EXIT_FAILURE;
    }

    // set the options for the HTTPS POST request
    if ((res = curl_easy_setopt(curl.get(), CURLOPT_URL, server_address)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https")) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER,
                                headers.get())) != CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, data)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 10L)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L)) !=
            CURLE_OK ||
        (res = curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
                                &write_response)) != CURLE_OK)
    {
        std::cerr << "HTTPS configuration: " << curl_easy_strerror(res) << '\n';
        return EXIT_FAILURE;
    }

    // nullptr keeps libcurl's default trust store. Verification stays enabled.
    if (ca_file != nullptr &&
        (res = curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca_file)) !=
            CURLE_OK)
    {
        std::cerr << "HTTPS CA configuration: " << curl_easy_strerror(res)
                  << '\n';
        return EXIT_FAILURE;
    }

    // perform the HTTP POST request
    res = curl_easy_perform(curl.get());
    if (res != CURLE_OK)
    {
        std::cerr << "HTTP request: " << curl_easy_strerror(res) << '\n';
        return EXIT_FAILURE;
    }

    // check the response code to determine success
    long response_code = 0;
    res = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if (res != CURLE_OK)
    {
        std::cerr << "HTTP status: " << curl_easy_strerror(res) << '\n';
        return EXIT_FAILURE;
    }
    std::cerr << "HTTP status: " << response_code << '\n';
    return response_code >= 200 && response_code < 300 ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
} // namespace https
