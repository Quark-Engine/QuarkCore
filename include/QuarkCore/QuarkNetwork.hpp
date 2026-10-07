/*
    ========================================================

        Quark Network Module
        By Quark Engine Development Team

    --------------------------------------------------------

        Asynchronous HTTP/HTTPS requests.

    ========================================================
*/

#ifndef __QUARK_NETWORK__
#define __QUARK_NETWORK__

#include "QuarkCore.hpp"
#include <cstddef>

struct NetworkRequestHandle;

enum NetworkRequestState {
    NETWORK_REQUEST_PENDING = 0,
    NETWORK_REQUEST_RUNNING,
    NETWORK_REQUEST_COMPLETED,
    NETWORK_REQUEST_FAILED,
    NETWORK_REQUEST_CANCELLED
};

struct HttpHeader {
    const char* name;
    const char* value;
};

struct HttpRequest {
    const char* url;
    const char* method; // Defaults to GET when null or empty.
    const HttpHeader* headers;
    int headerCount;
    const unsigned char* body;
    size_t bodySize;
    unsigned int timeoutMs; // Defaults to 30 seconds when zero.
    size_t maxResponseBytes; // Defaults to 16 MiB when zero.
};

struct HttpResponse {
    long statusCode;
    const unsigned char* body;
    size_t bodySize;
    const HttpHeader* headers;
    size_t headerCount;
};

/**
 * @brief Start an asynchronous HTTP/HTTPS request.
 *
 * Request data is copied before this function returns. HTTP status codes such
 * as 404 are completed responses; only transport or request errors fail.
 * TLS certificate verification is always enabled.
 *
 * @return A request handle, or nullptr if request data could not be copied.
 */
QCAPI NetworkRequestHandle* SendHttpRequestAsync(const HttpRequest* request);

/**
 * @brief Get the current state of a request.
 */
QCAPI NetworkRequestState GetHttpRequestState(const NetworkRequestHandle* request);

/**
 * @brief Get the transport error after a request fails.
 *
 * The returned string is owned by the request and remains valid until it is
 * unloaded. Returns nullptr while running or after a successful response.
 */
QCAPI const char* GetHttpRequestError(const NetworkRequestHandle* request);

/**
 * @brief Read a completed response.
 *
 * Response body and header pointers are owned by the request and remain valid
 * until UnloadHttpRequest() is called.
 * @return true if the request completed and response was written.
 */
QCAPI bool TryGetHttpResponse(const NetworkRequestHandle* request, HttpResponse* response);

/**
 * @brief Request cancellation. Cancellation is best-effort.
 */
QCAPI void CancelHttpRequest(NetworkRequestHandle* request);

/**
 * @brief Cancel if necessary, wait for completion, and release a request.
 */
QCAPI void UnloadHttpRequest(NetworkRequestHandle* request);

#endif // __QUARK_NETWORK__
