#if defined(_WIN32)
#ifndef NOGDI
#define NOGDI
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <QuarkCore/QuarkNetwork.hpp>

#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace {

#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket INVALID_TEST_SOCKET = INVALID_SOCKET;

void CloseSocket(TestSocket socket)
{
    closesocket(socket);
}
#else
using TestSocket = int;
constexpr TestSocket INVALID_TEST_SOCKET = -1;

void CloseSocket(TestSocket socket)
{
    close(socket);
}
#endif

bool SendAll(TestSocket socket, const char* data, size_t size)
{
    size_t sent = 0;
    while (sent < size) {
#if defined(_WIN32)
        const int count = send(socket, data + sent, static_cast<int>(size - sent), 0);
#else
        const ssize_t count = send(socket, data + sent, size - sent, 0);
#endif
        if (count <= 0) {
            return false;
        }
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool IsFinished(NetworkRequestState state)
{
    return state == NETWORK_REQUEST_COMPLETED ||
           state == NETWORK_REQUEST_FAILED ||
           state == NETWORK_REQUEST_CANCELLED;
}

} // namespace

int main()
{
#if defined(_WIN32)
    WSADATA winsockData{};
    assert(WSAStartup(MAKEWORD(2, 2), &winsockData) == 0);
#endif

    assert(GetHttpRequestState(nullptr) == NETWORK_REQUEST_FAILED);
    assert(GetHttpRequestError(nullptr) == nullptr);
    assert(!TryGetHttpResponse(nullptr, nullptr));
    CancelHttpRequest(nullptr);
    UnloadHttpRequest(nullptr);

    HttpRequest emptyUrl{};
    NetworkRequestHandle* request = SendHttpRequestAsync(&emptyUrl);
    assert(request != nullptr);
    assert(GetHttpRequestState(request) == NETWORK_REQUEST_FAILED);
    const char* error = GetHttpRequestError(request);
    assert(error != nullptr && std::strstr(error, "URL") != nullptr);

    HttpResponse response{};
    assert(!TryGetHttpResponse(request, &response));
    UnloadHttpRequest(request);

    HttpRequest invalidMethod{};
    invalidMethod.url = "https://example.invalid/";
    invalidMethod.method = "GET\r\nInjected: header";
    request = SendHttpRequestAsync(&invalidMethod);
    assert(request != nullptr);
    assert(GetHttpRequestState(request) == NETWORK_REQUEST_FAILED);
    error = GetHttpRequestError(request);
    assert(error != nullptr && std::strstr(error, "method") != nullptr);
    UnloadHttpRequest(request);

    const HttpHeader invalidHeader{"Invalid Header", "value"};
    HttpRequest invalidHeaders{};
    invalidHeaders.url = "https://example.invalid/";
    invalidHeaders.headers = &invalidHeader;
    invalidHeaders.headerCount = 1;
    request = SendHttpRequestAsync(&invalidHeaders);
    assert(request != nullptr);
    assert(GetHttpRequestState(request) == NETWORK_REQUEST_FAILED);
    error = GetHttpRequestError(request);
    assert(error != nullptr && std::strstr(error, "header") != nullptr);
    UnloadHttpRequest(request);

    const TestSocket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(server != INVALID_TEST_SOCKET);

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddress.sin_port = 0;
    assert(bind(server, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == 0);
    assert(listen(server, 1) == 0);

#if defined(_WIN32)
    int addressSize = sizeof(serverAddress);
#else
    socklen_t addressSize = sizeof(serverAddress);
#endif
    assert(getsockname(server, reinterpret_cast<sockaddr*>(&serverAddress), &addressSize) == 0);

    std::thread serverThread([server] {
        const TestSocket client = accept(server, nullptr, nullptr);
        assert(client != INVALID_TEST_SOCKET);

        char requestBuffer[2048];
#if defined(_WIN32)
        const int received = recv(client, requestBuffer, sizeof(requestBuffer), 0);
#else
        const ssize_t received = recv(client, requestBuffer, sizeof(requestBuffer), 0);
#endif
        assert(received > 0);

        constexpr char responseText[] =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 5\r\n"
            "X-Test: quark\r\n"
            "Connection: close\r\n\r\n"
            "hello";
        assert(SendAll(client, responseText, sizeof(responseText) - 1));
        CloseSocket(client);
        CloseSocket(server);
    });

    const std::string url = "http://127.0.0.1:" +
        std::to_string(ntohs(serverAddress.sin_port)) + "/test";
    HttpRequest httpRequest{};
    httpRequest.url = url.c_str();
    httpRequest.timeoutMs = 5000;
    request = SendHttpRequestAsync(&httpRequest);
    assert(request != nullptr);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!IsFinished(GetHttpRequestState(request)) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(GetHttpRequestState(request) == NETWORK_REQUEST_COMPLETED);
    assert(TryGetHttpResponse(request, &response));
    assert(response.statusCode == 404);
    assert(response.bodySize == 5);
    assert(std::memcmp(response.body, "hello", response.bodySize) == 0);

    bool foundHeader = false;
    for (size_t index = 0; index < response.headerCount; ++index) {
        if (std::strcmp(response.headers[index].name, "X-Test") == 0 &&
            std::strcmp(response.headers[index].value, "quark") == 0) {
            foundHeader = true;
            break;
        }
    }
    assert(foundHeader);

    UnloadHttpRequest(request);
    serverThread.join();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
