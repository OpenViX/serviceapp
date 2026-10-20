#include "m3u8.h"
#include <cstring>

#define M3U8_HEADER "#EXTM3U"
#define M3U8_HEADER_MAX_LINE 5

#define M3U8_STREAM_INFO "#EXT-X-STREAM-INF"
#define M3U8_MEDIA_SEQUENCE "#EXT-X-MEDIA-SEQUENCE"

// a variant URL relative to the master playlist, its dot segments resolved
static std::string resolveUrl(const std::string& base, const std::string& ref)
{
    if (!ref.compare(0, 4, "http"))
        return ref;
    size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos)
        return ref;
    if (!ref.compare(0, 2, "//"))
        return base.substr(0, schemeEnd + 1) + ref;
    size_t pathStart = base.find('/', schemeEnd + 3);
    std::string origin = base.substr(0, pathStart);
    std::string path;
    if (!ref.empty() && ref[0] == '/')
        path = ref;
    else
    {
        std::string basePath = pathStart == std::string::npos ? "/" : base.substr(pathStart);
        basePath = basePath.substr(0, basePath.find_first_of("?#"));
        path = basePath.substr(0, basePath.rfind('/') + 1) + ref;
    }
    size_t queryStart = path.find_first_of("?#");
    std::string query = queryStart == std::string::npos ? "" : path.substr(queryStart);
    path = path.substr(0, queryStart);

    std::vector<std::string> segments;
    std::istringstream parts(path.substr(1));
    std::string part;
    bool directory = path.size() > 1 && path[path.size() - 1] == '/';
    while (std::getline(parts, part, '/'))
    {
        directory = part == "." || part == "..";
        if (part == "..")
        {
            if (!segments.empty())
                segments.pop_back();
        }
        else if (part != ".")
            segments.push_back(part);
    }
    std::string resolved;
    for (size_t i = 0; i < segments.size(); i++)
        resolved += "/" + segments[i];
    if (directory || resolved.empty())
        resolved += "/";
    return origin + resolved + query;
}

bool isM3U8Url(const std::string &url)
{
    Url purl(url);
    std::string path = purl.path();
    size_t delim_idx = path.rfind(".");
    if((purl.proto() == "http" || purl.proto() == "https")
            && delim_idx != std::string::npos
            && !path.compare(delim_idx, 5, ".m3u8"))
        return true;
    return false;
}

int parse_attribute(char **ptr, char **key, char **value)
{
    if (ptr == NULL || *ptr == NULL || key == NULL || value == NULL)
        return -1;

    char *end;
    char *p;
    p = end = strchr(*ptr, ',');
    if (end)
    {
        char *q = strchr(*ptr, '"');
        if (q && q < end)
        {
            q = strchr(++q, '"');
            if (q)
            {
                p = end = strchr(++q, ',');
            }
        }
    }
    if (end)
    {
        do
        {
            end++;
        }
        while(*end && *end == ' ');
        *p = '\0';
    }

    *key = *ptr;
    p = strchr(*ptr, '=');
    if (!p)
        return -1;
    *p++ = '\0';
    *value = p;
    *ptr = end;
    return 0;
}

//https://tools.ietf.org/html/draft-pantos-http-live-streaming-13#section-3.4.10
int M3U8VariantsExplorer::parseStreamInfoAttributes(const char *attributes, M3U8StreamInfo& info)
{
    char *myline = strdup(attributes);
    char *ptr = myline;
    char *key = NULL;
    char *value = NULL;
    while (!parse_attribute(&ptr, &key, &value))
    {
        if (!strcasecmp(key, "bandwidth"))
            info.bitrate = atoi(value);
        if (!strcasecmp(key, "resolution"))
            info.resolution = value;
        if (!strcasecmp(key, "codecs"))
            info.codecs = value;
    }
    free(myline);
    return 0;
}


int M3U8VariantsExplorer::getVariantsFromMasterUrl(const std::string& url, HeaderMap& headers, unsigned int redirect)
{
    if (redirect > redirectLimit)
    {
        fprintf(stderr, "[%s] - reached maximum number of %d - redirects\n", __func__, redirectLimit);
        return -1;
    }
    Url purl(url);

    int port = purl.port();
    if (port == -1)
    {
        if (purl.proto() == "http")
            port = 80;
        else if (purl.proto() == "https")
            port = 443;
        else
        {
            fprintf(stderr, "[%s] - not defined port!\n", __func__);
            return -1;
        }
    }
    int sd;
    if((sd = Connect(purl.host().c_str(), port, 5)) < 0)
    {
        fprintf(stderr, "[%s] - Error in Connect\n", __func__);
        return -1;
    }

    SSL *ssl = NULL;
    SSL_CTX *ssl_ctx = NULL;

    if (purl.proto() == "https")
    {
        if (SSLConnect(purl.host().c_str(), sd, &ssl, &ssl_ctx) < 0)
        {
            ::close(sd);
            return -1;
        }
        fprintf(stderr, "[%s] - (SSL) Connected with %s encryption\n",
                __func__, SSL_get_cipher(ssl));

        // just inform about verification error but continue to work
        if (SSL_get_verify_result(ssl) != X509_V_OK)
        {
            fprintf(stderr, "[%s] - (SSL) Error in certificate verification: %s\n",
                    __func__, X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
        }
    }
    std::string userAgent = "HbbTV/1.1.1 (+PVR+RTSP+DL; Sonic; TV44; 1.32.455; 2.002) Bee/3.5";
    HeaderMap::const_iterator it;
    if ((it = headers.find("User-Agent")) != headers.end())
    {
        userAgent = it->second;
    }
    headers["User-Agent"] = userAgent;

    std::string path = purl.path();
    std::string query = purl.query();
    if (!query.empty())
        path += "?" + query;
    std::string request = "GET ";
    request.append(path).append(" HTTP/1.1\r\n");
    request.append("Host: ").append(purl.host());
    if (purl.port() > 0)
    {
        request.append(":").append(std::to_string(purl.port()));
    }
    request.append("\r\n");
    request.append("User-Agent: ").append(userAgent).append("\r\n");
    request.append("Accept: */*\r\n");
    for (HeaderMap::const_iterator it(headers.begin()); it != headers.end(); it++)
    {
        if ((it->first).compare("User-Agent"))
        {
            request.append(it->first + ": ").append(it->second).append("\r\n");
        }
    }
    request.append("Connection: close\r\n");
    request.append("\r\n");

    fprintf(stderr, "[%s] - Request:\n", __func__);
    fprintf(stderr, "%s\n", request.c_str());

    if (writeAll(ssl, sd, request.c_str(), request.length()) < (signed long) request.length())
    {
        fprintf(stderr, "[%s] - writeAll, didn't write everything\n", __func__);
        ::close(sd);
        if (ssl)
        {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
        }
        return -1;
    }
    int lines = 0;
    int contentLines = 0;

    int contentLength = 0;
    int contentSize = 0;
    bool contentStarted = false;
    bool contentTypeParsed = false;
    bool m3u8HeaderParsed = false;
    bool m3u8StreamInfoParsing = false;
    // audio of its own (EXT-X-MEDIA) is not in the variant playlists
    bool alternativeAudio = false;
    M3U8StreamInfo m3u8StreamInfo;

    size_t bufferSize = 1024;
    char *lineBuffer = (char *) malloc(bufferSize);

    int statusCode;
    char protocol[64], statusMessage[64];

    int result = readLine(ssl, sd, &lineBuffer, &bufferSize);
    fprintf(stderr, "[%s] Response[%d](size=%d): %s\n", __func__, lines++, result, lineBuffer);
    result = sscanf(lineBuffer, "%99s %d %99s", protocol, &statusCode, statusMessage);
    if (result != 3 || (statusCode != 200 && statusCode != 301 && statusCode != 302))
    {
            fprintf(stderr, "[%s] - wrong http response code: %d\n", __func__, statusCode);
            free(lineBuffer);
            if (ssl)
            {
                SSL_free(ssl);
                SSL_CTX_free(ssl_ctx);
            }
            ::close(sd);
            return -1;
    }
    int ret = -1;
    std::string redirectUrl;
    while(1)
    {
        result = readLine(ssl, sd, &lineBuffer, &bufferSize);
        fprintf(stderr, "[%s] Response[%d](size=%d): %s\n", __func__, lines++, result, lineBuffer);
        if (result < 0)
        {
            fprintf(stderr, "[%s] - end of read, nothing was read\n", __func__);
            break;
        }

        if (!contentStarted)
        {
            if (!contentLength)
            {
                sscanf(lineBuffer, "Content-Length: %d", &contentLength);
            }
            if (!contentTypeParsed)
            {
                char contenttype[33];
                if (sscanf(lineBuffer, "Content-Type: %32s", contenttype) == 1)
                {
                    contentTypeParsed = true;
                    if (!(!strncasecmp(contenttype, "application/text", 16)
                            || !strncasecmp(contenttype, "text/plain", 10)
                            || !strncasecmp(contenttype, "audio/x-mpegurl", 15)
                            || !strncasecmp(contenttype, "application/x-mpegurl", 21)
                            || !strncasecmp(contenttype, "application/vnd.apple.mpegurl", 29)
                            || !strncasecmp(contenttype, "audio/mpegurl", 13)
                            || !strncasecmp(contenttype, "application/m3u", 15)))
                    {
                        if (statusCode == 200)
                        {
                            fprintf(stderr, "[%s] - not supported contenttype detected: %s!\n", __func__, contenttype);
                            break;
                        }
                    }
                }
            }
            if ((statusCode == 301 || statusCode == 302) && strncasecmp(lineBuffer, "location: ", 10) == 0)
            {
                redirectUrl = &lineBuffer[10];
            }
            if (!strncmp(lineBuffer, "Set-Cookie: ", 12))
            {
                if (headers.find("Cookie") == headers.end())
                    headers["Cookie"] = &lineBuffer[12];
                else
                    headers["Cookie"].append(";").append(&lineBuffer[12]);
            }
            if (!result)
            {
                contentStarted = true;
                fprintf(stderr, "[%s] - content part started\n", __func__);
                if (!redirectUrl.empty())
                {
                    fprintf(stderr, "[%s] - redirecting to: %s\n", __func__, redirectUrl.c_str());
                    ret = getVariantsFromMasterUrl(redirectUrl, headers, ++redirect);
                    break;
                }
            }
        }
        else
        {
            contentLines++;
            contentSize += result + 1; // newline char
            if (!m3u8HeaderParsed)
            {
                if (contentLines > M3U8_HEADER_MAX_LINE)
                {
                    fprintf(stderr, "[%s] - invalid M3U8 playlist, '%s' header is not in first %d lines\n",
                            __func__, M3U8_HEADER, M3U8_HEADER_MAX_LINE);
                    break;
                }

                // find M3U8 header
                if (result && !strncmp(lineBuffer, M3U8_HEADER, strlen(M3U8_HEADER)))
                {
                    m3u8HeaderParsed = true;
                }
                continue;
            }

            if (!strncmp(lineBuffer, M3U8_MEDIA_SEQUENCE, strlen(M3U8_MEDIA_SEQUENCE)))
            {
                fprintf(stderr, "[%s] - we need master playlist not media sequence!\n", __func__);
                break;
            }

            if (m3u8StreamInfoParsing)
            {
                // there shouldn't be any empty line
                if (!result)
                {
                    m3u8StreamInfoParsing = false;
                    continue;
                }

                fprintf(stderr, "[%s] - continue parsing m3u8 stream info\n", __func__);
                m3u8StreamInfo.url = resolveUrl(url, lineBuffer);
                m3u8StreamInfo.headers = headers;
                streams.push_back(m3u8StreamInfo);
                m3u8StreamInfoParsing = false;
            }
            else
            {
                if (!strncmp(lineBuffer, M3U8_STREAM_INFO, 17))
                {
                    m3u8StreamInfoParsing = true;
                    std::string parsed(lineBuffer);
                    parseStreamInfoAttributes(parsed.substr(18).c_str(), m3u8StreamInfo);
                }
                else if (!strncmp(lineBuffer, "#EXT-X-MEDIA:", 13) && strstr(lineBuffer, "TYPE=AUDIO") && strstr(lineBuffer, "URI="))
                {
                    alternativeAudio = true;
                }
                else
                {
                    fprintf(stderr, "[%s] - skipping unrecognised data\n", __func__);
                }
            }

            if (contentLength && contentLength <= contentSize)
            {
                fprintf(stderr, "[%s] - end of read, Content-Length reached\n", __func__);
                break;
            }
        }
    }
    // a single variant would play without that audio: the player gets the
    // master playlist and its audio tracks
    if (alternativeAudio && !streams.empty())
    {
        fprintf(stderr, "[%s] - audio tracks of their own, playing the master playlist\n", __func__);
        streams.clear();
    }
    if (!streams.empty())
        ret = 0;
    free(lineBuffer);
    ::close(sd);
    if (ssl)
    {
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
    }
    return ret;
}

std::vector<M3U8StreamInfo> M3U8VariantsExplorer::getStreams()
{
    streams.clear();
    std::vector<std::string> masterUrl;
    masterUrl.push_back(url);
    // try also some common master playlist filenames
    // masterUrl.push_back(url.substr(0, url.rfind('/') + 1) + "master.m3u8");
    // masterUrl.push_back(url.substr(0, url.rfind('/') + 1) + "playlist.m3u8");
    for (std::vector<std::string>::const_iterator it(masterUrl.begin()); it != masterUrl.end(); it++)
    {
        int ret = getVariantsFromMasterUrl(*it, headers, 0);
        if (ret < 0)
            continue;
        break;
    }
    return streams;
}

