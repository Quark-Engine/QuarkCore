#include "QuarkCore/QuarkNetwork.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(QC_HAS_CURL)
#include <curl/curl.h>
#endif

namespace {

constexpr unsigned int DEFAULT_TIMEOUT_MS = 30000;
constexpr size_t DEFAULT_MAX_RESPONSE_BYTES = 16 * 1024 * 1024;

struct StoredHeader {
    std::string name;
    std::string value;
};

bool IsTerminalState(NetworkRequestState state)
{
    return state == NETWORK_REQUEST_COMPLETED ||
           state == NETWORK_REQUEST_FAILED ||
           state == NETWORK_REQUEST_CANCELLED;
}

bool IsValidHeader(const HttpHeader& header)
{
    if (header.name == nullptr || header.value == nullptr || header.name[0] == '\0') {
        return false;
    }

    for (const char* current = header.name; *current != '\0'; ++current) {
        const unsigned char character = static_cast<unsigned char>(*current);
        if (!std::isalnum(character) &&
            std::strchr("!#$%&'*+-.^_`|~", character) == nullptr) {
            return false;
        }
    }

    return std::strchr(header.value, '\r') == nullptr &&
           std::strchr(header.value, '\n') == nullptr;
}

bool IsValidMethod(const char* method)
{
    if (method == nullptr || method[0] == '\0') {
        return true;
    }

    for (const char* current = method; *current != '\0'; ++current) {
        const unsigned char character = static_cast<unsigned char>(*current);
        if (!std::isalnum(character) && std::strchr("!#$%&'*+-.^_`|~", character) == nullptr) {
            return false;
        }
    }
    return true;
}

} // namespace

struct NetworkRequestHandle {
    std::atomic<NetworkRequestState> state{NETWORK_REQUEST_PENDING};
    std::atomic<bool> cancelRequested{false};
    std::string url;
    std::string method;
    std::vector<StoredHeader> requestHeaders;
    std::vector<unsigned char> requestBody;
    std::vector<unsigned char> responseBody;
    std::vector<StoredHeader> responseHeaders;
    std::vector<HttpHeader> responseHeaderViews;
    std::string error;
    std::thread worker;
    unsigned int timeoutMs = DEFAULT_TIMEOUT_MS;
    size_t maxResponseBytes = DEFAULT_MAX_RESPONSE_BYTES;
    long statusCode = 0;
    bool responseTooLarge = false;
    bool callbackAllocationFailed = false;

    ~NetworkRequestHandle()
    {
        cancelRequested.store(true, std::memory_order_relaxed);
        if (worker.joinable()) {
            worker.join();
        }
    }
};

#if defined(QC_HAS_CURL)
namespace {

std::once_flag curlInitFlag;
CURLcode curlInitResult = CURLE_FAILED_INIT;

void EnsureCurlInitialized()
{
    std::call_once(curlInitFlag, [] {
        curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

struct CurlSlistDeleter {
    void operator()(curl_slist* list) const
    {
        curl_slist_free_all(list);
    }
};

size_t AppendResponseBody(char* data, size_t size, size_t count, void* userData)
{
    auto* request = static_cast<NetworkRequestHandle*>(userData);
    if (size != 0 && count > static_cast<size_t>(-1) / size) {
        return 0;
    }

    const size_t bytes = size * count;
    if (bytes == 0) {
        return 0;
    }
    if (bytes > request->maxResponseBytes - request->responseBody.size()) {
        request->responseTooLarge = true;
        return 0;
    }

    try {
        request->responseBody.insert(
            request->responseBody.end(),
            reinterpret_cast<unsigned char*>(data),
            reinterpret_cast<unsigned char*>(data) + bytes);
    } catch (const std::bad_alloc&) {
        request->callbackAllocationFailed = true;
        return 0;
    }

    return bytes;
}

std::string TrimHeaderPart(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

size_t AppendResponseHeader(char* data, size_t size, size_t count, void* userData)
{
    auto* request = static_cast<NetworkRequestHandle*>(userData);
    if (size != 0 && count > static_cast<size_t>(-1) / size) {
        return 0;
    }

    const size_t bytes = size * count;
    if (bytes == 0) {
        return 0;
    }
    try {
        std::string line(data, bytes);
        if (line.rfind("HTTP/", 0) == 0) {
            request->responseHeaders.clear();
        } else {
            const size_t separator = line.find(':');
            if (separator != std::string::npos) {
                StoredHeader header{
                    TrimHeaderPart(line.substr(0, separator)),
                    TrimHeaderPart(line.substr(separator + 1))
                };
                request->responseHeaders.push_back(std::move(header));
            }
        }
    } catch (const std::bad_alloc&) {
        request->callbackAllocationFailed = true;
        return 0;
    }

    return bytes;
}

int CheckProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto* request = static_cast<NetworkRequestHandle*>(userData);
    return request->cancelRequested.load(std::memory_order_relaxed) ? 1 : 0;
}

void SetRequestError(NetworkRequestHandle* request, const char* message)
{
    request->error = message;
    request->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
}

void RunHttpRequest(NetworkRequestHandle* request)
{
    request->state.store(NETWORK_REQUEST_RUNNING, std::memory_order_release);
    EnsureCurlInitialized();
    if (curlInitResult != CURLE_OK) {
        SetRequestError(request, curl_easy_strerror(curlInitResult));
        return;
    }

    using EasyHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    EasyHandle curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        SetRequestError(request, "Failed to create a libcurl request handle.");
        return;
    }

    std::unique_ptr<curl_slist, CurlSlistDeleter> headers;
    for (const StoredHeader& header : request->requestHeaders) {
        const std::string line = header.name + ": " + header.value;
        curl_slist* appended = curl_slist_append(headers.get(), line.c_str());
        if (appended == nullptr) {
            SetRequestError(request, "Failed to allocate HTTP request headers.");
            return;
        }
        headers.release();
        headers.reset(appended);
    }

    char errorBuffer[CURL_ERROR_SIZE] = {};
    CURLcode optionResult = CURLE_OK;
#define QC_CURL_SETOPT(option, value) \
    do { \
        optionResult = curl_easy_setopt(curl.get(), option, value); \
        if (optionResult != CURLE_OK) { \
            SetRequestError(request, curl_easy_strerror(optionResult)); \
            return; \
        } \
    } while (false)

    const long timeout = static_cast<long>(std::min(
        static_cast<size_t>(request->timeoutMs),
        static_cast<size_t>(LONG_MAX)));
    QC_CURL_SETOPT(CURLOPT_ERRORBUFFER, errorBuffer);
    QC_CURL_SETOPT(CURLOPT_URL, request->url.c_str());
    QC_CURL_SETOPT(CURLOPT_PROTOCOLS_STR, "http,https");
    QC_CURL_SETOPT(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    QC_CURL_SETOPT(CURLOPT_FOLLOWLOCATION, 1L);
    QC_CURL_SETOPT(CURLOPT_MAXREDIRS, 10L);
    QC_CURL_SETOPT(CURLOPT_NOSIGNAL, 1L);
    QC_CURL_SETOPT(CURLOPT_TIMEOUT_MS, timeout);
    QC_CURL_SETOPT(CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout, 10000L));
    QC_CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
    QC_CURL_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);
    QC_CURL_SETOPT(CURLOPT_WRITEFUNCTION, AppendResponseBody);
    QC_CURL_SETOPT(CURLOPT_WRITEDATA, request);
    QC_CURL_SETOPT(CURLOPT_HEADERFUNCTION, AppendResponseHeader);
    QC_CURL_SETOPT(CURLOPT_HEADERDATA, request);
    QC_CURL_SETOPT(CURLOPT_XFERINFOFUNCTION, CheckProgress);
    QC_CURL_SETOPT(CURLOPT_XFERINFODATA, request);
    QC_CURL_SETOPT(CURLOPT_NOPROGRESS, 0L);
    if (headers) {
        QC_CURL_SETOPT(CURLOPT_HTTPHEADER, headers.get());
    }
    if (request->method != "GET") {
        QC_CURL_SETOPT(CURLOPT_CUSTOMREQUEST, request->method.c_str());
    }
    if (!request->requestBody.empty()) {
        if (request->requestBody.size() >
            static_cast<size_t>(std::numeric_limits<curl_off_t>::max())) {
            SetRequestError(request, "HTTP request body is too large for libcurl.");
            return;
        }
        QC_CURL_SETOPT(CURLOPT_POSTFIELDS, request->requestBody.data());
        QC_CURL_SETOPT(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request->requestBody.size()));
    }

    {
        const CURLcode result = curl_easy_perform(curl.get());
        if (request->cancelRequested.load(std::memory_order_relaxed)) {
            request->error = "Request was cancelled.";
            request->state.store(NETWORK_REQUEST_CANCELLED, std::memory_order_release);
            return;
        }
        if (request->responseTooLarge) {
            SetRequestError(request, "HTTP response exceeded the configured maximum size.");
            return;
        }
        if (request->callbackAllocationFailed) {
            SetRequestError(request, "Failed to allocate HTTP response data.");
            return;
        }
        if (result != CURLE_OK) {
            SetRequestError(request, errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result));
            return;
        }
    }

    const CURLcode infoResult = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &request->statusCode);
    if (infoResult != CURLE_OK) {
        SetRequestError(request, curl_easy_strerror(infoResult));
        return;
    }

    try {
        request->responseHeaderViews.reserve(request->responseHeaders.size());
        for (const StoredHeader& header : request->responseHeaders) {
            request->responseHeaderViews.push_back({header.name.c_str(), header.value.c_str()});
        }
    } catch (const std::bad_alloc&) {
        SetRequestError(request, "Failed to allocate HTTP response headers.");
        return;
    }

    request->state.store(NETWORK_REQUEST_COMPLETED, std::memory_order_release);
}

#undef QC_CURL_SETOPT

} // namespace
#endif

NetworkRequestHandle* SendHttpRequestAsync(const HttpRequest* request)
{
    if (request == nullptr) {
        return nullptr;
    }

    std::unique_ptr<NetworkRequestHandle> handle(new (std::nothrow) NetworkRequestHandle());
    if (!handle) {
        return nullptr;
    }

    try {
        if (request->url == nullptr || request->url[0] == '\0') {
            handle->error = "HTTP request URL must not be empty.";
            handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (!IsValidMethod(request->method)) {
            handle->error = "HTTP method contains invalid characters.";
            handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (request->headerCount < 0 ||
            (request->headerCount > 0 && request->headers == nullptr)) {
            handle->error = "HTTP request header array is invalid.";
            handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (request->bodySize > 0 && request->body == nullptr) {
            handle->error = "HTTP request body is null but its size is non-zero.";
            handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }

        handle->url = request->url;
        handle->method = (request->method == nullptr || request->method[0] == '\0')
            ? "GET"
            : request->method;
        handle->maxResponseBytes = request->maxResponseBytes == 0
            ? DEFAULT_MAX_RESPONSE_BYTES
            : request->maxResponseBytes;
        handle->timeoutMs = request->timeoutMs == 0 ? DEFAULT_TIMEOUT_MS : request->timeoutMs;

        handle->requestHeaders.reserve(static_cast<size_t>(request->headerCount));
        for (int index = 0; index < request->headerCount; ++index) {
            if (!IsValidHeader(request->headers[index])) {
                handle->error = "HTTP request contains an invalid header.";
                handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
                return handle.release();
            }
            handle->requestHeaders.push_back({
                request->headers[index].name,
                request->headers[index].value
            });
        }
        if (request->bodySize > 0) {
            handle->requestBody.assign(request->body, request->body + request->bodySize);
        }
    } catch (const std::bad_alloc&) {
        return nullptr;
    }

#if defined(QC_HAS_CURL)
    try {
        handle->worker = std::thread([requestHandle = handle.get()] {
            try {
                RunHttpRequest(requestHandle);
            } catch (const std::bad_alloc&) {
                requestHandle->error = "Failed to allocate HTTP request data.";
                requestHandle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            }
        });
    } catch (const std::system_error&) {
        handle->error = "Failed to start an HTTP request worker thread.";
        handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
    }
#else
    handle->error = "HTTP networking is unavailable on this platform build.";
    handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
#endif

    return handle.release();
}

NetworkRequestState GetHttpRequestState(const NetworkRequestHandle* request)
{
    return request == nullptr
        ? NETWORK_REQUEST_FAILED
        : request->state.load(std::memory_order_acquire);
}

const char* GetHttpRequestError(const NetworkRequestHandle* request)
{
    if (request == nullptr ||
        !IsTerminalState(request->state.load(std::memory_order_acquire)) ||
        request->state.load(std::memory_order_relaxed) == NETWORK_REQUEST_COMPLETED) {
        return nullptr;
    }
    return request->error.c_str();
}

bool TryGetHttpResponse(const NetworkRequestHandle* request, HttpResponse* response)
{
    if (request == nullptr || response == nullptr ||
        request->state.load(std::memory_order_acquire) != NETWORK_REQUEST_COMPLETED) {
        return false;
    }

    response->statusCode = request->statusCode;
    response->body = request->responseBody.empty() ? nullptr : request->responseBody.data();
    response->bodySize = request->responseBody.size();
    response->headers = request->responseHeaderViews.empty()
        ? nullptr
        : request->responseHeaderViews.data();
    response->headerCount = request->responseHeaderViews.size();
    return true;
}

void CancelHttpRequest(NetworkRequestHandle* request)
{
    if (request != nullptr && !IsTerminalState(request->state.load(std::memory_order_acquire))) {
        request->cancelRequested.store(true, std::memory_order_relaxed);
    }
}

void UnloadHttpRequest(NetworkRequestHandle* request)
{
    delete request;
}
