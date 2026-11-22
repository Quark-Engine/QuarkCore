#include "QuarkCore/QuarkNetwork.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(QC_HAS_CURL)
#include <curl/curl.h>
#endif

namespace
{
    constexpr unsigned int DEFAULT_TIMEOUT_MS = 30000;
    constexpr size_t DEFAULT_MAX_RESPONSE_BYTES = 16 * 1024 * 1024;

    struct StoredHeader
    {
        std::string m_Name;
        std::string m_Value;
    };

    bool IsTerminalState(NetworkRequestState state)
    {
        return state == NETWORK_REQUEST_COMPLETED ||
               state == NETWORK_REQUEST_FAILED ||
               state == NETWORK_REQUEST_CANCELLED;
    }

    bool IsValidHeader(const HttpHeader& header)
    {
        if (header.name == nullptr || header.value == nullptr || header.name[0] == '\0')
        {
            return false;
        }

        for (const char* pCurrent = header.name; *pCurrent != '\0'; ++pCurrent)
        {
            const unsigned char character = static_cast<unsigned char>(*pCurrent);
            if (!std::isalnum(character) &&
                std::strchr("!#$%&'*+-.^_`|~", character) == nullptr)
            {
                return false;
            }
        }

        return std::strchr(header.value, '\r') == nullptr &&
               std::strchr(header.value, '\n') == nullptr;
    }

    bool IsValidMethod(const char* pMethod)
    {
        if (pMethod == nullptr || pMethod[0] == '\0')
        {
            return true;
        }

        for (const char* pCurrent = pMethod; *pCurrent != '\0'; ++pCurrent)
        {
            const unsigned char character = static_cast<unsigned char>(*pCurrent);
            if (!std::isalnum(character) && std::strchr("!#$%&'*+-.^_`|~", character) == nullptr)
            {
                return false;
            }
        }
        return true;
    }
} // anonymous

struct NetworkRequestHandle
{
    std::atomic<NetworkRequestState> m_State{NETWORK_REQUEST_PENDING};
    std::atomic<bool> m_CancelRequested{false};
    std::string m_Url;
    std::string m_Method;
    std::vector<StoredHeader> m_vRequestHeaders;
    std::vector<unsigned char> m_vRequestBody;
    std::vector<unsigned char> m_vResponseBody;
    std::vector<StoredHeader> m_vResponseHeaders;
    std::vector<HttpHeader> m_vResponseHeaderViews;
    std::string m_Error;
    std::thread m_Worker;
    unsigned int m_TimeoutMs = DEFAULT_TIMEOUT_MS;
    size_t m_MaxResponseBytes = DEFAULT_MAX_RESPONSE_BYTES;
    long m_StatusCode = 0;
    bool m_ResponseTooLarge = false;
    bool m_CallbackAllocationFailed = false;

    ~NetworkRequestHandle()
    {
        m_CancelRequested.store(true, std::memory_order_relaxed);
        if (m_Worker.joinable())
        {
            m_Worker.join();
        }
    }
};

#if defined(QC_HAS_CURL)
namespace
{
    std::once_flag s_CurlInitFlag;
    CURLcode s_CurlInitResult = CURLE_FAILED_INIT;

    void EnsureCurlInitialized()
    {
        std::call_once(s_CurlInitFlag, []
                       {
                           s_CurlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT);
                       });
    }

    struct CurlSlistDeleter
    {
        void operator()(curl_slist* pList) const
        {
            curl_slist_free_all(pList);
        }
    };

    size_t AppendResponseBody(char* pData, size_t size, size_t count, void* pUserData)
    {
        auto* pRequest = static_cast<NetworkRequestHandle*>(pUserData);
        if (size != 0 && count > static_cast<size_t>(-1) / size)
        {
            return 0;
        }

        const size_t bytes = size * count;
        if (bytes == 0)
        {
            return 0;
        }
        if (bytes > pRequest->m_MaxResponseBytes - pRequest->m_vResponseBody.size())
        {
            pRequest->m_ResponseTooLarge = true;
            return 0;
        }

        try
        {
            pRequest->m_vResponseBody.insert(
                pRequest->m_vResponseBody.end(),
                reinterpret_cast<unsigned char*>(pData),
                reinterpret_cast<unsigned char*>(pData) + bytes);
        }
        catch (const std::bad_alloc&)
        {
            pRequest->m_CallbackAllocationFailed = true;
            return 0;
        }

        return bytes;
    }

    std::string TrimHeaderPart(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    size_t AppendResponseHeader(char* pData, size_t size, size_t count, void* pUserData)
    {
        auto* pRequest = static_cast<NetworkRequestHandle*>(pUserData);
        if (size != 0 && count > static_cast<size_t>(-1) / size)
        {
            return 0;
        }

        const size_t bytes = size * count;
        if (bytes == 0)
        {
            return 0;
        }
        try
        {
            std::string line(pData, bytes);
            if (line.rfind("HTTP/", 0) == 0)
            {
                pRequest->m_vResponseHeaders.clear();
            }
            else
            {
                const size_t separator = line.find(':');
                if (separator != std::string::npos)
                {
                    StoredHeader header{
                        TrimHeaderPart(line.substr(0, separator)),
                        TrimHeaderPart(line.substr(separator + 1))};
                    pRequest->m_vResponseHeaders.push_back(std::move(header));
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            pRequest->m_CallbackAllocationFailed = true;
            return 0;
        }

        return bytes;
    }

    int CheckProgress(void* pUserData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        const auto* pRequest = static_cast<NetworkRequestHandle*>(pUserData);
        return pRequest->m_CancelRequested.load(std::memory_order_relaxed) ? 1 : 0;
    }

    void SetRequestError(NetworkRequestHandle* pRequest, const char* pMessage)
    {
        pRequest->m_Error = pMessage;
        pRequest->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
    }

    void RunHttpRequest(NetworkRequestHandle* pRequest)
    {
        pRequest->m_State.store(NETWORK_REQUEST_RUNNING, std::memory_order_release);
        EnsureCurlInitialized();
        if (s_CurlInitResult != CURLE_OK)
        {
            SetRequestError(pRequest, curl_easy_strerror(s_CurlInitResult));
            return;
        }

        using EasyHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
        EasyHandle curl(curl_easy_init(), &curl_easy_cleanup);
        if (!curl)
        {
            SetRequestError(pRequest, "Failed to create a libcurl request handle.");
            return;
        }

        std::unique_ptr<curl_slist, CurlSlistDeleter> headers;
        for (const StoredHeader& header : pRequest->m_vRequestHeaders)
        {
            const std::string line = header.m_Name + ": " + header.m_Value;
            curl_slist* pAppended = curl_slist_append(headers.get(), line.c_str());
            if (pAppended == nullptr)
            {
                SetRequestError(pRequest, "Failed to allocate HTTP request headers.");
                return;
            }
            headers.release();
            headers.reset(pAppended);
        }

        char aErrorBuffer[CURL_ERROR_SIZE] = {};
        CURLcode optionResult = CURLE_OK;
#define QC_CURL_SETOPT(option, value)                                    \
    do                                                                   \
    {                                                                    \
        optionResult = curl_easy_setopt(curl.get(), option, value);      \
        if (optionResult != CURLE_OK)                                    \
        {                                                                \
            SetRequestError(pRequest, curl_easy_strerror(optionResult)); \
            return;                                                      \
        }                                                                \
    } while (false)

        const long timeout = static_cast<long>(std::min(
            static_cast<size_t>(pRequest->m_TimeoutMs),
            static_cast<size_t>(LONG_MAX)));
        QC_CURL_SETOPT(CURLOPT_ERRORBUFFER, aErrorBuffer);
        QC_CURL_SETOPT(CURLOPT_URL, pRequest->m_Url.c_str());
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
        QC_CURL_SETOPT(CURLOPT_WRITEDATA, pRequest);
        QC_CURL_SETOPT(CURLOPT_HEADERFUNCTION, AppendResponseHeader);
        QC_CURL_SETOPT(CURLOPT_HEADERDATA, pRequest);
        QC_CURL_SETOPT(CURLOPT_XFERINFOFUNCTION, CheckProgress);
        QC_CURL_SETOPT(CURLOPT_XFERINFODATA, pRequest);
        QC_CURL_SETOPT(CURLOPT_NOPROGRESS, 0L);
        if (headers)
        {
            QC_CURL_SETOPT(CURLOPT_HTTPHEADER, headers.get());
        }
        if (pRequest->m_Method != "GET")
        {
            QC_CURL_SETOPT(CURLOPT_CUSTOMREQUEST, pRequest->m_Method.c_str());
        }
        if (!pRequest->m_vRequestBody.empty())
        {
            if (pRequest->m_vRequestBody.size() >
                static_cast<size_t>(std::numeric_limits<curl_off_t>::max()))
            {
                SetRequestError(pRequest, "HTTP request body is too large for libcurl.");
                return;
            }
            QC_CURL_SETOPT(CURLOPT_POSTFIELDS, pRequest->m_vRequestBody.data());
            QC_CURL_SETOPT(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(pRequest->m_vRequestBody.size()));
        }

        {
            const CURLcode result = curl_easy_perform(curl.get());
            if (pRequest->m_CancelRequested.load(std::memory_order_relaxed))
            {
                pRequest->m_Error = "Request was cancelled.";
                pRequest->m_State.store(NETWORK_REQUEST_CANCELLED, std::memory_order_release);
                return;
            }
            if (pRequest->m_ResponseTooLarge)
            {
                SetRequestError(pRequest, "HTTP response exceeded the configured maximum size.");
                return;
            }
            if (pRequest->m_CallbackAllocationFailed)
            {
                SetRequestError(pRequest, "Failed to allocate HTTP response data.");
                return;
            }
            if (result != CURLE_OK)
            {
                SetRequestError(pRequest, aErrorBuffer[0] != '\0' ? aErrorBuffer : curl_easy_strerror(result));
                return;
            }
        }

        const CURLcode infoResult = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &pRequest->m_StatusCode);
        if (infoResult != CURLE_OK)
        {
            SetRequestError(pRequest, curl_easy_strerror(infoResult));
            return;
        }

        try
        {
            pRequest->m_vResponseHeaderViews.reserve(pRequest->m_vResponseHeaders.size());
            for (const StoredHeader& header : pRequest->m_vResponseHeaders)
            {
                pRequest->m_vResponseHeaderViews.push_back({header.m_Name.c_str(), header.m_Value.c_str()});
            }
        }
        catch (const std::bad_alloc&)
        {
            SetRequestError(pRequest, "Failed to allocate HTTP response headers.");
            return;
        }

        pRequest->m_State.store(NETWORK_REQUEST_COMPLETED, std::memory_order_release);
    }

#undef QC_CURL_SETOPT
} // anonymous
#endif

NetworkRequestHandle* SendHttpRequestAsync(const HttpRequest* pRequest)
{
    if (pRequest == nullptr)
    {
        return nullptr;
    }

    std::unique_ptr<NetworkRequestHandle> handle(new (std::nothrow) NetworkRequestHandle());
    if (!handle)
    {
        return nullptr;
    }

    try
    {
        if (pRequest->url == nullptr || pRequest->url[0] == '\0')
        {
            handle->m_Error = "HTTP request URL must not be empty.";
            handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (!IsValidMethod(pRequest->method))
        {
            handle->m_Error = "HTTP method contains invalid characters.";
            handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (pRequest->headerCount < 0 ||
            (pRequest->headerCount > 0 && pRequest->headers == nullptr))
        {
            handle->m_Error = "HTTP request header array is invalid.";
            handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }
        if (pRequest->bodySize > 0 && pRequest->body == nullptr)
        {
            handle->m_Error = "HTTP request body is null but its size is non-zero.";
            handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
            return handle.release();
        }

        handle->m_Url = pRequest->url;
        handle->m_Method = (pRequest->method == nullptr || pRequest->method[0] == '\0')
                             ? "GET"
                             : pRequest->method;
        handle->m_MaxResponseBytes = pRequest->maxResponseBytes == 0
                                       ? DEFAULT_MAX_RESPONSE_BYTES
                                       : pRequest->maxResponseBytes;
        handle->m_TimeoutMs = pRequest->timeoutMs == 0 ? DEFAULT_TIMEOUT_MS : pRequest->timeoutMs;

        handle->m_vRequestHeaders.reserve(static_cast<size_t>(pRequest->headerCount));
        for (int index = 0; index < pRequest->headerCount; ++index)
        {
            if (!IsValidHeader(pRequest->headers[index]))
            {
                handle->m_Error = "HTTP request contains an invalid header.";
                handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
                return handle.release();
            }
            handle->m_vRequestHeaders.push_back(
                {pRequest->headers[index].name,
                 pRequest->headers[index].value});
        }
        if (pRequest->bodySize > 0)
        {
            handle->m_vRequestBody.assign(pRequest->body, pRequest->body + pRequest->bodySize);
        }
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }

#if defined(QC_HAS_CURL)
    try
    {
        handle->m_Worker = std::thread([pRequestHandle = handle.get()]
                                     {
                                         try
                                         {
                                             RunHttpRequest(pRequestHandle);
                                         }
                                         catch (const std::bad_alloc&)
                                         {
                                             pRequestHandle->m_Error = "Failed to allocate HTTP request data.";
                                             pRequestHandle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
                                         }
                                     });
    }
    catch (const std::system_error&)
    {
        handle->m_Error = "Failed to start an HTTP request worker thread.";
        handle->m_State.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
    }
#else
    handle->error = "HTTP networking is unavailable on this platform build.";
    handle->state.store(NETWORK_REQUEST_FAILED, std::memory_order_release);
#endif

    return handle.release();
}

NetworkRequestState GetHttpRequestState(const NetworkRequestHandle* pRequest)
{
    return pRequest == nullptr
               ? NETWORK_REQUEST_FAILED
               : pRequest->m_State.load(std::memory_order_acquire);
}

const char* GetHttpRequestError(const NetworkRequestHandle* pRequest)
{
    if (pRequest == nullptr ||
        !IsTerminalState(pRequest->m_State.load(std::memory_order_acquire)) ||
        pRequest->m_State.load(std::memory_order_relaxed) == NETWORK_REQUEST_COMPLETED)
    {
        return nullptr;
    }
    return pRequest->m_Error.c_str();
}

bool TryGetHttpResponse(const NetworkRequestHandle* pRequest, HttpResponse* pResponse)
{
    if (pRequest == nullptr || pResponse == nullptr ||
        pRequest->m_State.load(std::memory_order_acquire) != NETWORK_REQUEST_COMPLETED)
    {
        return false;
    }

    pResponse->statusCode = pRequest->m_StatusCode;
    pResponse->body = pRequest->m_vResponseBody.empty() ? nullptr : pRequest->m_vResponseBody.data();
    pResponse->bodySize = pRequest->m_vResponseBody.size();
    pResponse->headers = pRequest->m_vResponseHeaderViews.empty()
                             ? nullptr
                             : pRequest->m_vResponseHeaderViews.data();
    pResponse->headerCount = pRequest->m_vResponseHeaderViews.size();
    return true;
}

void CancelHttpRequest(NetworkRequestHandle* pRequest)
{
    if (pRequest != nullptr && !IsTerminalState(pRequest->m_State.load(std::memory_order_acquire)))
    {
        pRequest->m_CancelRequested.store(true, std::memory_order_relaxed);
    }
}

void UnloadHttpRequest(NetworkRequestHandle* pRequest)
{
    delete pRequest;
}
