#include "Http.h"

#include "Text.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstdio>
#include <memory>

namespace lw::http {
    namespace {
        struct InternetHandle {
            HINTERNET m_Handle = nullptr;

            explicit InternetHandle(HINTERNET p_Handle = nullptr) : m_Handle(p_Handle) {}
            ~InternetHandle() { Close(); }

            InternetHandle(const InternetHandle&) = delete;
            InternetHandle& operator=(const InternetHandle&) = delete;

            void Close() {
                if (m_Handle) {
                    WinHttpCloseHandle(m_Handle);
                    m_Handle = nullptr;
                }
            }

            explicit operator bool() const { return m_Handle != nullptr; }
        };

        std::string LastErrorText(const char* p_Where) {
            const DWORD s_Error = GetLastError();

            switch (s_Error) {
                case ERROR_WINHTTP_CANNOT_CONNECT:
                    return std::string(p_Where) + ": keine Verbindung (laeuft der Server?)";
                case ERROR_WINHTTP_TIMEOUT:
                    return std::string(p_Where) + ": Zeitueberschreitung";
                case ERROR_WINHTTP_NAME_NOT_RESOLVED:
                    return std::string(p_Where) + ": Servername nicht gefunden";
                case ERROR_WINHTTP_OPERATION_CANCELLED:
                    return std::string(p_Where) + ": abgebrochen";
                case ERROR_WINHTTP_CONNECTION_ERROR:
                    return std::string(p_Where) + ": Verbindung abgebrochen";
                default:
                    return std::string(p_Where) + ": Windows-Fehler " + std::to_string(s_Error);
            }
        }

        bool IsLocalHost(const std::wstring& p_Host) {
            return p_Host == L"localhost" || p_Host == L"127.0.0.1" || p_Host == L"::1" || p_Host == L"[::1]";
        }
    }

    void CancelToken::Cancel() {
        m_Cancelled = true;

        std::scoped_lock s_Lock(m_Mutex);

        // Das Schliessen des Handles bricht blockierende WinHTTP-Aufrufe im anderen Thread ab.
        if (m_Handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(m_Handle));
            m_Handle = nullptr;
        }
    }

    bool CancelToken::Attach(void* p_Handle) {
        std::scoped_lock s_Lock(m_Mutex);

        if (m_Cancelled) {
            return false;
        }

        m_Handle = p_Handle;
        return true;
    }

    bool CancelToken::Detach() {
        std::scoped_lock s_Lock(m_Mutex);
        const bool s_StillAttached = m_Handle != nullptr;
        m_Handle = nullptr;
        return s_StillAttached;
    }

    Response Send(const Request& p_Request) {
        Response s_Response;

        const std::wstring s_Url = Utf8ToWide(p_Request.m_Url);

        URL_COMPONENTS s_Parts {};
        s_Parts.dwStructSize = sizeof(s_Parts);
        wchar_t s_Host[256] = {};
        wchar_t s_Path[2048] = {};
        wchar_t s_Extra[2048] = {};
        s_Parts.lpszHostName = s_Host;
        s_Parts.dwHostNameLength = static_cast<DWORD>(std::size(s_Host));
        s_Parts.lpszUrlPath = s_Path;
        s_Parts.dwUrlPathLength = static_cast<DWORD>(std::size(s_Path));
        s_Parts.lpszExtraInfo = s_Extra;
        s_Parts.dwExtraInfoLength = static_cast<DWORD>(std::size(s_Extra));

        if (!WinHttpCrackUrl(s_Url.c_str(), static_cast<DWORD>(s_Url.size()), 0, &s_Parts)) {
            s_Response.m_Error = "Ungueltige URL: " + p_Request.m_Url;
            return s_Response;
        }

        const std::wstring s_HostName(s_Host, s_Parts.dwHostNameLength);
        const bool s_Secure = s_Parts.nScheme == INTERNET_SCHEME_HTTPS;

        InternetHandle s_Session(WinHttpOpen(
            L"LivingWorld/1.0",
            IsLocalHost(s_HostName) ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        ));

        if (!s_Session) {
            // Aeltere Systeme kennen AUTOMATIC_PROXY nicht.
            s_Session.m_Handle = WinHttpOpen(
                L"LivingWorld/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
            );
        }

        if (!s_Session) {
            s_Response.m_Error = LastErrorText("WinHttpOpen");
            return s_Response;
        }

        const int s_Timeout = p_Request.m_TimeoutMs;
        WinHttpSetTimeouts(s_Session.m_Handle, 5000, 5000, s_Timeout, s_Timeout);

        DWORD s_Decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s_Session.m_Handle, WINHTTP_OPTION_DECOMPRESSION, &s_Decompression, sizeof(s_Decompression));

        InternetHandle s_Connection(WinHttpConnect(s_Session.m_Handle, s_HostName.c_str(), s_Parts.nPort, 0));

        if (!s_Connection) {
            s_Response.m_Error = LastErrorText("WinHttpConnect");
            return s_Response;
        }

        const std::wstring s_Method = Utf8ToWide(p_Request.m_Method);
        const std::wstring s_Object =
            std::wstring(s_Path, s_Parts.dwUrlPathLength) + std::wstring(s_Extra, s_Parts.dwExtraInfoLength);

        HINTERNET s_RawRequest = WinHttpOpenRequest(
            s_Connection.m_Handle,
            s_Method.c_str(),
            s_Object.empty() ? L"/" : s_Object.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            s_Secure ? WINHTTP_FLAG_SECURE : 0
        );

        if (!s_RawRequest) {
            s_Response.m_Error = LastErrorText("WinHttpOpenRequest");
            return s_Response;
        }

        // Das Request-Handle gehoert ab hier dem Abbruch-Token oder uns.
        bool s_OwnedByToken = false;

        if (p_Request.m_Cancel) {
            if (!p_Request.m_Cancel->Attach(s_RawRequest)) {
                WinHttpCloseHandle(s_RawRequest);
                s_Response.m_Error = "abgebrochen";
                return s_Response;
            }

            s_OwnedByToken = true;
        }

        auto s_CloseRequest = [&]() {
            // Hat Cancel() das Handle bereits geschlossen, liefert Detach() false – dann nicht doppelt schliessen.
            if (!s_OwnedByToken || p_Request.m_Cancel->Detach()) {
                WinHttpCloseHandle(s_RawRequest);
            }
        };

        std::wstring s_Headers;

        for (const auto& [s_Name, s_Value] : p_Request.m_Headers) {
            s_Headers += Utf8ToWide(s_Name) + L": " + Utf8ToWide(s_Value) + L"\r\n";
        }

        const BOOL s_Sent = WinHttpSendRequest(
            s_RawRequest,
            s_Headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : s_Headers.c_str(),
            s_Headers.empty() ? 0 : static_cast<DWORD>(-1L),
            p_Request.m_Body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(p_Request.m_Body.data()),
            static_cast<DWORD>(p_Request.m_Body.size()),
            static_cast<DWORD>(p_Request.m_Body.size()),
            0
        );

        if (!s_Sent || !WinHttpReceiveResponse(s_RawRequest, nullptr)) {
            s_Response.m_Error = (p_Request.m_Cancel && p_Request.m_Cancel->IsCancelled())
                ? "abgebrochen"
                : LastErrorText(s_Sent ? "Antwort" : "Senden");
            s_CloseRequest();
            return s_Response;
        }

        DWORD s_Status = 0;
        DWORD s_StatusSize = sizeof(s_Status);
        WinHttpQueryHeaders(
            s_RawRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &s_Status, &s_StatusSize, WINHTTP_NO_HEADER_INDEX
        );
        s_Response.m_Status = static_cast<int>(s_Status);

        uint64_t s_Total = 0;
        wchar_t s_LengthText[32] = {};
        DWORD s_LengthSize = sizeof(s_LengthText);

        if (WinHttpQueryHeaders(
            s_RawRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
            s_LengthText, &s_LengthSize, WINHTTP_NO_HEADER_INDEX
        )) {
            s_Total = _wcstoui64(s_LengthText, nullptr, 10);
        }

        uint64_t s_Received = 0;
        std::vector<char> s_Buffer(64 * 1024);

        for (;;) {
            if (p_Request.m_Cancel && p_Request.m_Cancel->IsCancelled()) {
                s_Response.m_Error = "abgebrochen";
                break;
            }

            DWORD s_Available = 0;

            if (!WinHttpQueryDataAvailable(s_RawRequest, &s_Available)) {
                s_Response.m_Error = LastErrorText("Empfangen");
                break;
            }

            if (s_Available == 0) {
                break;
            }

            if (s_Available > s_Buffer.size()) {
                s_Buffer.resize(s_Available);
            }

            DWORD s_Read = 0;

            if (!WinHttpReadData(s_RawRequest, s_Buffer.data(), s_Available, &s_Read)) {
                s_Response.m_Error = LastErrorText("Lesen");
                break;
            }

            if (s_Read == 0) {
                break;
            }

            s_Received += s_Read;

            if (p_Request.m_OnData) {
                if (!p_Request.m_OnData(s_Buffer.data(), s_Read)) {
                    s_Response.m_Error = "abgebrochen";
                    break;
                }
            }
            else {
                s_Response.m_Body.append(s_Buffer.data(), s_Read);
            }

            if (p_Request.m_OnProgress) {
                p_Request.m_OnProgress(s_Received, s_Total);
            }
        }

        s_CloseRequest();
        return s_Response;
    }

    bool DownloadToFile(
        const std::string& p_Url,
        const std::wstring& p_TargetPath,
        const std::function<void(uint64_t, uint64_t)>& p_Progress,
        CancelToken* p_Cancel,
        std::string& p_Error
    ) {
        const std::wstring s_PartPath = p_TargetPath + L".part";
        FILE* s_File = nullptr;

        if (_wfopen_s(&s_File, s_PartPath.c_str(), L"wb") != 0 || !s_File) {
            p_Error = "Datei kann nicht geschrieben werden: " + WideToUtf8(s_PartPath);
            return false;
        }

        bool s_WriteFailed = false;

        Request s_Request;
        s_Request.m_Url = p_Url;
        s_Request.m_TimeoutMs = 60000;
        s_Request.m_Cancel = p_Cancel;
        s_Request.m_OnData = [&](const char* p_Data, size_t p_Size) {
            if (fwrite(p_Data, 1, p_Size, s_File) != p_Size) {
                s_WriteFailed = true;
                return false;
            }

            return true;
        };
        s_Request.m_OnProgress = p_Progress;

        const Response s_Response = Send(s_Request);
        fclose(s_File);

        if (s_WriteFailed) {
            p_Error = "Schreibfehler (Speicherplatz?)";
        }
        else if (!s_Response.m_Error.empty()) {
            p_Error = s_Response.m_Error;
        }
        else if (s_Response.m_Status != 200) {
            p_Error = "HTTP " + std::to_string(s_Response.m_Status);
        }

        if (!p_Error.empty()) {
            DeleteFileW(s_PartPath.c_str());
            return false;
        }

        if (!MoveFileExW(s_PartPath.c_str(), p_TargetPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            p_Error = "Umbenennen fehlgeschlagen";
            DeleteFileW(s_PartPath.c_str());
            return false;
        }

        return true;
    }
}
