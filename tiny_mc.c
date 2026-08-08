/*
Tiny MC Launcher - Full Command Line Version (v260809-1)
Cross-platform: Windows/ReactOS + Linux + macOS + BSD
Compile Windows/ReactOS: gcc -Os tiny_mc.c cJSON.c -o mc.exe -lwinhttp -lshell32 -luser32 -lz
Compile Linux:   gcc tiny_mc.c cJSON.c -o mc -lcurl -lz
Compile macOS:   clang tiny_mc.c cJSON.c -o mc -lcurl
Compile BSD:     clang tiny_mc.c cJSON.c -o mc -lcurl
Licensed under the MIT License.
Copyright (c) 2026 qwq672

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!! 发布/开源前务必删除 MSA_CLIENT_ID 私有凭据！          !!!
!!! REMOVE MSA_CLIENT_ID private credential before release !!!
!!! 搜索 "f07d70ba" 定位并删除                             !!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
*/

// ==================== Platform Detection ====================
#if defined(_WIN32) || defined(_WIN64) || defined(__REACTOS__)
    #define PLATFORM_WINAPI
    #include <windows.h>
    #include <tchar.h>
    #include <winhttp.h>
    #include <tlhelp32.h>
    #pragma comment(lib, "winhttp.lib")
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_POSIX
    #define PLATFORM_MACOS
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <curl/curl.h>
#elif defined(__linux__) || defined(__linux) || defined(linux)
    #define PLATFORM_POSIX
    #define PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <curl/curl.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    #define PLATFORM_POSIX
    #define PLATFORM_BSD
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <curl/curl.h>
#else
    #error "Unsupported platform."
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef PLATFORM_WINAPI
#include <process.h>
#else
#include <sys/types.h>
#endif

// zlib for deflate decompression (required for native JAR files)
#include <zlib.h>

#include "cJSON.h"

/* cJSON_min compatibility - define missing functions if needed */
#ifndef CJSON_MIN_LOADED
#define CJSON_MIN_LOADED
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// ==================== Cross-platform helpers ====================
#ifdef PLATFORM_WINAPI
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#define EXE_EXT ".exe"
#define NATIVE_SUFFIX "natives-windows"
#define CP_SEP ';'
#define CP_SEP_STR ";"
#define JAVA_EXE "java.exe"
#define JAVAW_EXE "javaw.exe"
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#define EXE_EXT ""
#define CP_SEP ':'
#define CP_SEP_STR ":"
#define JAVA_EXE "java"
#define JAVAW_EXE "java"
#define _strdup strdup
#ifdef PLATFORM_MACOS
#define NATIVE_SUFFIX "natives-macos"
#else
#define NATIVE_SUFFIX "natives-linux"
#endif
#endif

// ==================== Configuration Constants ====================
#define MAX_PATH_LEN 260
#define MAX_JAVA 8
#define MAX_ACCOUNTS 8
#define MAX_ARGC 32
#define MAX_CLASSPATH 65536
#define MAX_LIB_PATH 512
#define MAX_LIBS 1024
#define MAX_PLAYER_IDS 5
#define CLIENT_TOKEN_LEN 33
#define CONFIG_FILE "mc_config.ini"
#define MC_BASE_URL "https://launchermeta.mojang.com"
#define LIBRARIES_URL "https://libraries.minecraft.net"
#define AUTHLIB_URL "https://authlib-injector.yushi.moe/artifact/55/authlib-injector-1.2.7.jar"
#define AUTHLIB_JAR_NAME "authlib-injector-1.2.7.jar"

// ==================== Data Structures ====================
typedef struct {
    char path[MAX_PATH_LEN];
    char version[32];
    int major;
    int valid;
} JavaInfo;

typedef struct {
    char username[64];
    char email[64];
    char type[16];
    char server[128];
    char password[64];
    char accessToken[2048];
    char uuid[64];
    int is_default;
    char player_ids[MAX_PLAYER_IDS][64];
    int player_id_count;
    char custom_params[256];
} AccountInfo;

// ==================== Global Variables ====================
JavaInfo java_list[MAX_JAVA];
AccountInfo accounts[MAX_ACCOUNTS];
int java_count = 0;
int account_count = 0;
char mc_path[MAX_PATH_LEN];
char default_ver[64] = "";
char default_java_path[MAX_PATH_LEN] = "";
char jvm_args[512] = "-Xmx2G -Xms512M";
char launcher_dir[MAX_PATH_LEN];
char custom_java_path[MAX_PATH_LEN] = "";
int use_java_exe = 0;  // 0 = javaw.exe, 1 = java.exe
char custom_authlib_path[MAX_PATH_LEN] = "";
char window_title[256] = "";
#ifdef PLATFORM_WINAPI
DWORD game_pid = 0;
HANDLE game_process_handle = NULL;
#else
int game_pid = 0;
int game_process_handle = -1; /* POSIX pid 句柄 */
#endif

// Forward declarations
int yggdrasil_authenticate(const char* api_root, const char* email, const char* password,
                           char* out_username, char* out_uuid, char* out_accessToken);

// ==================== Safe String Operations ====================
void safe_str_cpy(char* dest, size_t dest_size, const char* src) {
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (!src) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void safe_str_cat(char* dest, size_t dest_size, const char* src) {
    if (!dest || dest_size == 0 || !src) return;
    size_t current = strnlen(dest, dest_size);
    if (current >= dest_size - 1) return;
    strncat(dest, src, dest_size - current - 1);
}

#define str_len(s) (s ? strlen(s) : 0)
#define str_cmp(a,b) (a && b ? strcmp(a,b) : -1)
void str_cpy(char* d, const char* s) { safe_str_cpy(d, MAX_PATH_LEN, s); }
void str_cat(char* d, const char* s) { safe_str_cat(d, MAX_PATH_LEN, s); }
void str_trim_quotes(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    if (len >= 2 && (s[0] == '"' || s[0] == '\'') && (s[len-1] == '"' || s[len-1] == '\'')) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

// ==================== Path Builders ====================
/* Build mc_path/subdir/id/id.suffix */
static void build_mc_path(char* out, size_t sz, const char* id, const char* subdir, const char* suffix) {
    safe_str_cpy(out, sz, mc_path);
    safe_str_cat(out, sz, PATH_SEP_STR); safe_str_cat(out, sz, subdir);
    safe_str_cat(out, sz, PATH_SEP_STR); safe_str_cat(out, sz, id);
    safe_str_cat(out, sz, PATH_SEP_STR); safe_str_cat(out, sz, id);
    safe_str_cat(out, sz, suffix);
}
/* Build mc_path/libraries/lib_name (slash to PATH_SEP) */
static void build_lib_path(char* out, size_t sz, const char* lib_name) {
    char temp[MAX_LIB_PATH];
    safe_str_cpy(out, sz, mc_path);
    safe_str_cat(out, sz, PATH_SEP_STR "libraries" PATH_SEP_STR);
    safe_str_cpy(temp, sizeof(temp), lib_name);
    for (char* p = temp; *p; p++) if (*p == '/') *p = PATH_SEP;
    safe_str_cat(out, sz, temp);
}

// ==================== Integer Conversion ====================
void int_to_str(int num, char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    snprintf(buf, buf_size, "%d", num);
}
int str_to_int(const char* s) { return s ? atoi(s) : 0; }

// ==================== Output ====================
#ifndef NDEBUG
void print(const char* s) { if (s) printf("%s", s); }
void print_int(int x) { printf("%d", x); }
#else
#define print(s)   ((void)0)
#define print_int(x) ((void)0)
#endif

// ==================== Config File Path ====================
void get_config_path(char* out, size_t size) {
    safe_str_cpy(out, size, launcher_dir);
    safe_str_cat(out, size, PATH_SEP_STR);
    safe_str_cat(out, size, CONFIG_FILE);
}

#ifdef PLATFORM_WINAPI
void write_config(const char* key, const char* val) {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    WritePrivateProfileStringA("config", key, val, path);
}

int read_config(const char* key, char* val, size_t val_size) {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    DWORD ret = GetPrivateProfileStringA("config", key, "", val, (DWORD)val_size, path);
    return ret > 0;
}

void clear_config() {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    DeleteFileA(path);
}
#else
/* ---- POSIX: 手写 INI 读写 ---- */
static char* config_cache = NULL;

static void config_reload(void) {
    free(config_cache); config_cache = NULL;
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return;
    long sz;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        config_cache = (char*)malloc((size_t)sz + 1);
        if (config_cache) {
            size_t rd = fread(config_cache, 1, (size_t)sz, f);
            config_cache[rd] = '\0';
        }
    }
    fclose(f);
}

static const char* config_find(const char* key) {
    if (!config_cache) config_reload();
    if (!config_cache) return NULL;
    size_t klen = strlen(key);
    char* p = config_cache;
    while (p && *p) {
        char* nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        if (linelen > 0 && p[linelen-1] == '\r') linelen--;
        if (linelen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            static char val[MAX_PATH_LEN*2];
            size_t vlen = linelen - klen - 1;
            if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
            memcpy(val, p, vlen); val[vlen] = '\0';
            return val;
        }
        p = nl ? nl + 1 : NULL;
    }
    return NULL;
}

void write_config(const char* key, const char* val) {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    config_reload();
    char* content = config_cache;
    char* out = (char*)malloc(strlen(content ? content : "") + strlen(key) + strlen(val) + 8);
    if (!out) return;
    out[0] = '\0';
    int replaced = 0;
    if (content) {
        size_t klen = strlen(key);
        char* p = content;
        while (p && *p) {
            char* nl = strchr(p, '\n');
            size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
            size_t len = linelen;
            if (len > 0 && p[len-1] == '\r') len--;
            if (len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
                strcat(out, key); strcat(out, "="); strcat(out, val); strcat(out, "\n");
                replaced = 1;
            } else {
                strncat(out, p, linelen); strcat(out, "\n");
            }
            p = nl ? nl + 1 : NULL;
        }
    }
    if (!replaced) { strcat(out, key); strcat(out, "="); strcat(out, val); strcat(out, "\n"); }
    FILE* f = fopen(path, "wb");
    if (f) { fputs(out, f); fclose(f); }
    free(out);
    config_reload();
}

int read_config(const char* key, char* val, size_t val_size) {
    const char* v = config_find(key);
    if (v) { safe_str_cpy(val, val_size, v); return 1; }
    if (val && val_size) val[0] = '\0';
    return 0;
}

void clear_config() {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    remove(path);
    config_reload();
}
#endif

// ==================== Portable Helpers ====================
/* 读取整个文件到 malloc 缓冲区（NULL 结尾），失败返回 NULL */
char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    long sz;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* 读取一行控制台输入到 buf（含最多 len-1 字符），返回读取字符数 */
int console_read_line(char* buf, int len) {
    if (!buf || len <= 0) return 0;
    if (fgets(buf, len, stdin) == NULL) { buf[0] = '\0'; return 0; }
    int n = (int)strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return n;
}

/* 系统物理内存大小（MB），失败返回 0 */
long get_total_physical_mb(void) {
#ifdef PLATFORM_WINAPI
    MEMORYSTATUSEX ms = { sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) return (long)(ms.ullTotalPhys / 1024 / 1024);
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) return (long)(((double)pages * (double)page_size) / 1024.0 / 1024.0);
    return 0;
#endif
}

void make_key(char* key, const char* prefix, int num) {
    char num_str[16];
    int_to_str(num, num_str, sizeof(num_str));
    safe_str_cpy(key, 32, prefix);
    safe_str_cat(key, 32, num_str);
}

// ==================== Execute Command ====================
#ifdef PLATFORM_WINAPI
int exec_cmd(const char* cmd, char* output, int max_len) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return 0;
    STARTUPINFOA si = { sizeof(si) };
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, (char*)cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite); return 0;
    }
    CloseHandle(hWrite);
    DWORD dwRead;
    int total = 0;
    char buf[1024];
    while (ReadFile(hRead, buf, sizeof(buf)-1, &dwRead, NULL) && dwRead > 0 && total < max_len-1) {
        buf[dwRead] = '\0';
        for (DWORD i = 0; i < dwRead && total < max_len-1; i++) output[total++] = buf[i];
    }
    output[total] = '\0';
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);
    return 1;
}
#else
int exec_cmd(const char* cmd, char* output, int max_len) {
    FILE* fp = popen(cmd, "r");
    if (!fp) { if (output && max_len > 0) output[0] = '\0'; return 0; }
    int total = 0;
    while (total < max_len-1 && fgets(output + total, max_len - total, fp)) {
        total = (int)strlen(output);
    }
    if (total == 0 && output && max_len > 0) output[0] = '\0';
    int rc = pclose(fp);
    (void)rc;
    return 1;
}
#endif

/* 在指定工作目录运行命令并等待结束，返回退出码；启动失败返回 -1 */
int run_cmd_blocking(const char* cmd, const char* workdir) {
#ifdef PLATFORM_WINAPI
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, (char*)cmd, NULL, NULL, FALSE, 0, NULL, workdir, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (workdir) chdir(workdir);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

// ==================== HTTP Helpers ====================
#ifdef PLATFORM_WINAPI
typedef struct {
    wchar_t host[256];
    wchar_t path[1024];
    INTERNET_PORT port;
    BOOL use_tls;
} UrlInfo;

static int parse_url(const char* url, UrlInfo* info) {
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszHostName = info->host;
    urlComp.dwHostNameLength = sizeof(info->host)/sizeof(wchar_t);
    urlComp.lpszUrlPath = info->path;
    urlComp.dwUrlPathLength = sizeof(info->path)/sizeof(wchar_t);
    urlComp.dwSchemeLength = -1;

    int urlLen = strlen(url) + 1;
    wchar_t* wideUrl = (wchar_t*)malloc(urlLen * sizeof(wchar_t));
    if (!wideUrl) return 0;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wideUrl, urlLen);
    int result = WinHttpCrackUrl(wideUrl, 0, 0, &urlComp);
    free(wideUrl);
    if (!result) return 0;

    info->use_tls = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    info->port = urlComp.nPort ? urlComp.nPort : (info->use_tls ? 443 : 80);
    return 1;
}

static void set_tls_options(HINTERNET hRequest) {
    DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
}

static char* read_response(HINTERNET hRequest) {
    char* response = NULL;
    size_t totalSize = 0;
    DWORD bytesRead = 0;
    do {
        DWORD dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* temp = (char*)realloc(response, totalSize + dwSize + 1);
        if (!temp) break;
        response = temp;
        if (!WinHttpReadData(hRequest, response + totalSize, dwSize, &bytesRead)) break;
        totalSize += bytesRead;
    } while (bytesRead > 0);
    if (response) response[totalSize] = '\0';
    return response;
}

// ==================== HTTP POST (for external login) ====================
char* http_post(const char* url, const char* json_data) {
    UrlInfo info;
    if (!parse_url(url, &info)) return NULL;

    HINTERNET hSession = WinHttpOpen(L"TinyMC/v260809-1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;

    HINTERNET hConnect = WinHttpConnect(hSession, info.host, info.port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", info.path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            info.use_tls ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL;
    }

    if (info.use_tls) set_tls_options(hRequest);

    LPCWSTR headers = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           (LPVOID)json_data, (DWORD)strlen(json_data),
                           (DWORD)strlen(json_data), 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }

    char* response = read_response(hRequest);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return response;
}

char* http_get(const char* url) {
    UrlInfo info;
    if (!parse_url(url, &info)) return NULL;

    HINTERNET hSession = WinHttpOpen(L"TinyMC/v260809-1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;

    HINTERNET hConnect = WinHttpConnect(hSession, info.host, info.port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", info.path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            info.use_tls ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL;
    }

    if (info.use_tls) set_tls_options(hRequest);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }

    char* response = read_response(hRequest);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return response;
}

// ==================== HTTP GET (for downloading files) ====================
int http_get_file(const char* url, const char* save_path) {
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {0};
    wchar_t pathBuf[1024] = {0};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = sizeof(hostBuf)/sizeof(wchar_t);
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = sizeof(pathBuf)/sizeof(wchar_t);
    urlComp.dwSchemeLength = -1;

    int urlLen = strlen(url) + 1;
    wchar_t* wideUrl = (wchar_t*)malloc(urlLen * sizeof(wchar_t));
    if (!wideUrl) return 0;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wideUrl, urlLen);
    if (!WinHttpCrackUrl(wideUrl, 0, 0, &urlComp)) {
        free(wideUrl);
        return 0;
    }
    free(wideUrl);

    BOOL useTls = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;
    if (port == 0) port = useTls ? 443 : 80;

    HINTERNET hSession = WinHttpOpen(L"TinyMC Launcher/v260809-1",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return 0;

    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return 0;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathBuf,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            (useTls ? WINHTTP_FLAG_SECURE : 0));
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    if (useTls) {
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                        SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        print("HTTP Error: "); 
        char code[16]; int_to_str(statusCode, code, sizeof(code));
        print(code); print("\n");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    // 获取文件大小
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    wchar_t contentLengthBuf[64] = {0};
    DWORD contentLengthBufSize = sizeof(contentLengthBuf);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        // 成功获取 Content-Length
    } else {
        contentLength = 0; // 未知大小
    }

    HANDLE hFile = CreateFileA(save_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    int success = 1;
    char buffer[8192];
    DWORD bytesRead = 0;
    DWORD totalDownloaded = 0;
    DWORD lastProgress = 0;
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        DWORD bytesWritten = 0;
        if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL)) {
            success = 0;
            break;
        }
        totalDownloaded += bytesWritten;

        // 显示进度条（仅当知道文件大小时）
        if (contentLength > 0) {
            DWORD progress = (DWORD)((totalDownloaded * 100) / contentLength);
            if (progress != lastProgress) {
                lastProgress = progress;
                // 计算速度
                QueryPerformanceCounter(&end);
                double elapsed = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
                double speedMBs = (elapsed > 0) ? (totalDownloaded / 1024.0 / 1024.0) / elapsed : 0;
                double totalMB = totalDownloaded / 1024.0 / 1024.0;
                double fileMB = contentLength / 1024.0 / 1024.0;

                // 打印进度条
                char bar[52];
                int filled = (int)(progress / 2);
                for (int i = 0; i < 50; i++) {
                    bar[i] = (i < filled) ? '=' : ' ';
                }
                bar[50] = '\0';

                char speedStr[32];
                snprintf(speedStr, sizeof(speedStr), "%.2f", speedMBs);

                char totalStr[32];
                snprintf(totalStr, sizeof(totalStr), "%.2f", totalMB);

                char fileStr[32];
                snprintf(fileStr, sizeof(fileStr), "%.2f", fileMB);

                print("\r["); print(bar); print("] ");
                char pct[8]; int_to_str(progress, pct, sizeof(pct));
                print(pct); print("% ");
                print(totalStr); print("MB/"); print(fileStr); print("MB @ ");
                print(speedStr); print("MB/s");

                // 移动到行首
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
                    COORD pos = {0, csbi.dwCursorPosition.Y};
                    SetConsoleCursorPosition(hConsole, pos);
                }
            }
        }
    }

    // 下载完成后换行
    if (contentLength > 0) {
        print("\n");
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}
#else
// ==================== Linux HTTP Functions (using libcurl) ====================
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct { char* data; size_t size; }* mem = (struct { char* data; size_t size; }*)userp;
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

char* http_post(const char* url, const char* json_data) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct { char* data; size_t size; } chunk = {0};
    chunk.data = malloc(1);
    if (!chunk.data) { curl_easy_cleanup(curl); return NULL; }
    chunk.data[0] = '\0';
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(chunk.data);
        return NULL;
    }
    return chunk.data;
}

char* http_get(const char* url) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct { char* data; size_t size; } chunk = {0};
    chunk.data = malloc(1);
    if (!chunk.data) { curl_easy_cleanup(curl); return NULL; }
    chunk.data[0] = '\0';
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(chunk.data);
        return NULL;
    }
    return chunk.data;
}

int http_get_file(const char* url, const char* save_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return 0;
    
    FILE* fp = fopen(save_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return 0; }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK);
}
#endif

// ==================== Create directory recursively ====================
void create_parent_dirs(const char* filepath) {
    char dir[MAX_PATH_LEN];
    safe_str_cpy(dir, sizeof(dir), filepath);
    char* last_sep = strrchr(dir, PATH_SEP);
    if (!last_sep) last_sep = strrchr(dir, '/');
    if (last_sep) {
        *last_sep = '\0';
#ifdef PLATFORM_WINAPI
        char* p = dir;
        while (*p) {
            if (*p == '\\') {
                *p = '\0';
                CreateDirectoryA(dir, NULL);
                *p = '\\';
            }
            p++;
        }
        CreateDirectoryA(dir, NULL);
#else
        char* p = dir;
        while (*p) {
            if (*p == '/') {
                *p = '\0';
                mkdir(dir, 0755);
                *p = '/';
            }
            p++;
        }
        mkdir(dir, 0755);
#endif
    }
}

// ==================== File exists check ====================
int file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

// ==================== Java Management ====================
int parse_java_major_version(const char* version_str) {
    if (strstr(version_str, "1.8")) return 8;
    int major = 0;
    const char* p = version_str;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (*p) major = atoi(p);
    if (major >= 9) return major;
    if (major == 1 && p[1] == '.' && p[2] >= '0' && p[2] <= '9') return p[2] - '0';
    return 8;
}
void get_java_version(const char* java_path, char* version, int max_len) {
    char cmd[512];
    char output[1024] = {0};
    safe_str_cpy(cmd, sizeof(cmd), "\"");
    safe_str_cat(cmd, sizeof(cmd), java_path);
    safe_str_cat(cmd, sizeof(cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE "\" -version");
    if (exec_cmd(cmd, output, sizeof(output))) {
        char* ver_start = strstr(output, "version \"");
        if (ver_start) {
            ver_start += 9;
            char* ver_end = strchr(ver_start, '"');
            if (ver_end) {
                int len = ver_end - ver_start;
                if (len > 0 && len < max_len) {
                    for (int i = 0; i < len; i++) version[i] = ver_start[i];
                    version[len] = '\0';
                    return;
                }
            }
        }
    }
    safe_str_cpy(version, max_len, "1.8");
}
int check_java_valid(const char* path) {
    char exe[MAX_PATH_LEN];
    safe_str_cpy(exe, sizeof(exe), path);
    safe_str_cat(exe, sizeof(exe), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE);
    return file_exists(exe);
}

/* 列出 dir 下的所有直接子目录名到 names（不含 . 和 ..），返回数量 */
int list_subdirs(const char* dir, char names[][MAX_PATH_LEN], int max) {
    int count = 0;
#ifdef PLATFORM_WINAPI
    char search[MAX_PATH_LEN];
    safe_str_cpy(search, sizeof(search), dir);
    safe_str_cat(search, sizeof(search), "\\*");
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            str_cmp(fd.cFileName, ".") != 0 && str_cmp(fd.cFileName, "..") != 0 &&
            count < max) {
            safe_str_cpy(names[count], MAX_PATH_LEN, fd.cFileName);
            count++;
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* d = opendir(dir);
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && count < max) {
        if (str_cmp(e->d_name, ".") == 0 || str_cmp(e->d_name, "..") == 0) continue;
        char full[MAX_PATH_LEN];
        safe_str_cpy(full, sizeof(full), dir);
        safe_str_cat(full, sizeof(full), PATH_SEP_STR);
        safe_str_cat(full, sizeof(full), e->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            safe_str_cpy(names[count], MAX_PATH_LEN, e->d_name);
            count++;
        }
    }
    closedir(d);
#endif
    return count;
}

void auto_scan_java() {
    java_count = 0;
#ifdef PLATFORM_WINAPI
    const char* roots[] = { "C:\\Program Files\\Java", "C:\\Program Files (x86)\\Java", "C:\\Program Files\\Eclipse Adoptium", "C:\\Java", NULL };
#else
    const char* roots[] = { "/usr/lib/jvm", "/usr/java", "/opt/java", "/Library/Java/JavaVirtualMachines", NULL };
#endif
    for (int i = 0; roots[i] && java_count < MAX_JAVA; i++) {
        char dirs[MAX_JAVA][MAX_PATH_LEN];
        int ndirs = list_subdirs(roots[i], dirs, MAX_JAVA);
        for (int d = 0; d < ndirs && java_count < MAX_JAVA; d++) {
            char java_path[MAX_PATH_LEN];
            safe_str_cpy(java_path, sizeof(java_path), roots[i]);
            safe_str_cat(java_path, sizeof(java_path), PATH_SEP_STR);
            safe_str_cat(java_path, sizeof(java_path), dirs[d]);
            int dup = 0;
            for (int j = 0; j < java_count; j++) if (str_cmp(java_list[j].path, java_path) == 0) { dup = 1; break; }
            if (dup) continue;
            if (!check_java_valid(java_path)) continue;
            safe_str_cpy(java_list[java_count].path, sizeof(java_list[java_count].path), java_path);
            get_java_version(java_path, java_list[java_count].version, 32);
            java_list[java_count].major = parse_java_major_version(java_list[java_count].version);
            java_list[java_count].valid = 1;
            char key[32];
            make_key(key, "JAVA_", java_count); str_cat(key, "_PATH"); write_config(key, java_list[java_count].path);
            make_key(key, "JAVA_", java_count); str_cat(key, "_VER"); write_config(key, java_list[java_count].version);
            make_key(key, "JAVA_", java_count); str_cat(key, "_MAJOR");
            char major_str[8]; int_to_str(java_list[java_count].major, major_str, sizeof(major_str)); write_config(key, major_str);
            java_count++;
        }
    }
    for (int i = 0; i < java_count; i++) if (java_list[i].valid) { safe_str_cpy(default_java_path, sizeof(default_java_path), java_list[i].path); write_config("DEFAULT_JAVA", default_java_path); break; }
    print("Java scan complete. Found "); print_int(java_count); print(" valid entries.\n");
}
int select_java_by_major(int required_major) {
    if (java_count == 0) auto_scan_java();
    if (java_count == 0) return -1;
    for (int i = 0; i < java_count; i++) if (java_list[i].major == required_major) return i;
    int best = -1;
    for (int i = 0; i < java_count; i++) if (java_list[i].major >= required_major && (best == -1 || java_list[i].major < java_list[best].major)) best = i;
    if (best != -1) return best;
    return 0;
}
int select_java_interactive() {
    if (java_count == 0) auto_scan_java();
    if (java_count == 0) { print("No Java found.\n"); return -1; }
    if (java_count == 1) { print("Using Java: "); print(java_list[0].version); print("\n"); return 0; }
    print("=== Select Java ===\n");
    for (int i = 0; i < java_count; i++) {
        print("  ["); print_int(i+1); print("] ");
        print(java_list[i].path); print(" | "); print(java_list[i].version); print("\n");
    }
    print("Select (1-"); print_int(java_count); print("): ");
    char input[4] = {0};
    console_read_line(input, sizeof(input));
    int sel = str_to_int(input);
    if (sel > 0 && sel <= java_count) return sel-1;
    print("Invalid, using default.\n");
    return 0;
}

// ==================== Version JSON Parsing (using cJSON) ====================
typedef struct {
    char id[64];
    char inheritsFrom[64];
    char clientVersion[64];  // Fabric uses this instead of inheritsFrom
    char mainClass[128];
    char assets[64];
    char assetIndex[64];
    char assetIndexUrl[512];
    int assetIndexTotalSize;
    char jar[64];
    char client_url[512];
    int java_major;
    char minecraftArguments[1024];  // 旧版 JSON (1.12 及以下) 使用此字段
    char* libraries[MAX_LIBS];
    int lib_sizes[MAX_LIBS];  // expected file sizes from JSON
    int lib_count;
    char* natives[MAX_LIBS];
    int native_count;
    char* jvm_args[MAX_LIBS];
    int jvm_arg_count;
    char* game_args[MAX_LIBS];
    int game_arg_count;
} VersionInfo;

void version_info_init(VersionInfo* info) { memset(info, 0, sizeof(VersionInfo)); info->java_major = 8; }
void version_info_free(VersionInfo* info) {
    for (int i = 0; i < info->lib_count; i++) free(info->libraries[i]);
    for (int i = 0; i < info->native_count; i++) free(info->natives[i]);
    for (int i = 0; i < info->jvm_arg_count; i++) free(info->jvm_args[i]);
    for (int i = 0; i < info->game_arg_count; i++) free(info->game_args[i]);
}

int parse_version_json(const char* version_id, VersionInfo* info) {
    print("  [PARSE] Parsing version JSON: "); print(version_id); print("\n");
    char json_path[MAX_PATH_LEN];
    build_mc_path(json_path, sizeof(json_path), version_id, "versions", ".json");

    char* buf = read_file(json_path);
    if (!buf) {
        print("JSON not found: "); print(json_path); print("\n");
        return 0;
    }
    char* json = buf;
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) json += 3;

    cJSON* root = cJSON_Parse(json);
    free(buf);
    if (!root) {
        print("Failed to parse version JSON.\n");
        return 0;
    }

    cJSON* id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && cJSON_IsString(id_item)) safe_str_cpy(info->id, sizeof(info->id), id_item->valuestring);
    else safe_str_cpy(info->id, sizeof(info->id), version_id);

    cJSON* inherits_item = cJSON_GetObjectItem(root, "inheritsFrom");
    if (inherits_item && cJSON_IsString(inherits_item)) safe_str_cpy(info->inheritsFrom, sizeof(info->inheritsFrom), inherits_item->valuestring);
    // Fabric uses "clientVersion" instead of "inheritsFrom" for the parent version
    cJSON* clientVer_item = cJSON_GetObjectItem(root, "clientVersion");
    if (clientVer_item && cJSON_IsString(clientVer_item)) safe_str_cpy(info->clientVersion, sizeof(info->clientVersion), clientVer_item->valuestring);
    cJSON* mainClass_item = cJSON_GetObjectItem(root, "mainClass");
    if (mainClass_item && cJSON_IsString(mainClass_item)) {
        safe_str_cpy(info->mainClass, sizeof(info->mainClass), mainClass_item->valuestring);
    }
    cJSON* assets_item = cJSON_GetObjectItem(root, "assets");
    if (assets_item && cJSON_IsString(assets_item)) safe_str_cpy(info->assets, sizeof(info->assets), assets_item->valuestring);
    cJSON* assetIndex_item = cJSON_GetObjectItem(root, "assetIndex");
    if (assetIndex_item && cJSON_IsObject(assetIndex_item)) {
        cJSON* assetId = cJSON_GetObjectItem(assetIndex_item, "id");
        if (assetId && cJSON_IsString(assetId)) safe_str_cpy(info->assetIndex, sizeof(info->assetIndex), assetId->valuestring);
        cJSON* assetUrl = cJSON_GetObjectItem(assetIndex_item, "url");
        if (assetUrl && cJSON_IsString(assetUrl)) safe_str_cpy(info->assetIndexUrl, sizeof(info->assetIndexUrl), assetUrl->valuestring);
        cJSON* assetSize = cJSON_GetObjectItem(assetIndex_item, "totalSize");
        if (assetSize && cJSON_IsNumber(assetSize)) info->assetIndexTotalSize = (int)assetSize->valuedouble;
    }
    cJSON* javaVersion_item = cJSON_GetObjectItem(root, "javaVersion");
    if (javaVersion_item && cJSON_IsObject(javaVersion_item)) {
        cJSON* majorVersion = cJSON_GetObjectItem(javaVersion_item, "majorVersion");
        if (majorVersion && cJSON_IsNumber(majorVersion)) info->java_major = (int)majorVersion->valuedouble;
    }

    // 解析 downloads.client.url 获取游戏 JAR 下载 URL
    cJSON* downloads = cJSON_GetObjectItem(root, "downloads");
    if (downloads && cJSON_IsObject(downloads)) {
        cJSON* client = cJSON_GetObjectItem(downloads, "client");
        if (client && cJSON_IsObject(client)) {
            cJSON* client_url_item = cJSON_GetObjectItem(client, "url");
            if (client_url_item && cJSON_IsString(client_url_item)) {
                safe_str_cpy(info->client_url, sizeof(info->client_url), client_url_item->valuestring);
            }
        }
    }

    // 解析旧版 minecraftArguments (1.12 及以下)
    cJSON* mcArgs = cJSON_GetObjectItem(root, "minecraftArguments");
    if (mcArgs && cJSON_IsString(mcArgs)) {
        safe_str_cpy(info->minecraftArguments, sizeof(info->minecraftArguments), mcArgs->valuestring);
    }

    cJSON* libraries = cJSON_GetObjectItem(root, "libraries");
    if (libraries && cJSON_IsArray(libraries)) {
        int size = cJSON_GetArraySize(libraries);
        for (int i = 0; i < size; i++) {
            cJSON* lib = cJSON_GetArrayItem(libraries, i);
            if (!lib) continue;

            // Check rules to determine if this library should be included
            int should_include = 1; // default: include (no rules = include)
            cJSON* rules = cJSON_GetObjectItem(lib, "rules");
            if (rules && cJSON_IsArray(rules)) {
                // When rules exist, default is disallow, rules determine inclusion
                should_include = 0;
                int rule_count = cJSON_GetArraySize(rules);
                for (int r = 0; r < rule_count; r++) {
                    cJSON* rule = cJSON_GetArrayItem(rules, r);
                    if (!rule) continue;
                    cJSON* action = cJSON_GetObjectItem(rule, "action");
                    if (!action || !cJSON_IsString(action)) continue;

                    int rule_applies = 1; // whether this rule applies to current OS

                    // Check OS rule
                    cJSON* os_rule = cJSON_GetObjectItem(rule, "os");
                    if (os_rule && cJSON_IsObject(os_rule)) {
                        cJSON* os_name = cJSON_GetObjectItem(os_rule, "name");
                        if (os_name && cJSON_IsString(os_name)) {
#ifdef PLATFORM_WINAPI
                            if (str_cmp(os_name->valuestring, "windows") != 0) {
                                rule_applies = 0; // OS rule doesn't match Windows
                            }
#else
                            // On non-Windows, check if rule targets this OS
                            const char* target_os = os_name->valuestring;
                            if (str_cmp(target_os, "linux") != 0
#ifdef PLATFORM_MACOS
                                && str_cmp(target_os, "osx") != 0
#endif
                            ) {
                                rule_applies = 0;
                            }
#endif
                        }
                    }

                    if (rule_applies) {
                        if (str_cmp(action->valuestring, "allow") == 0) should_include = 1;
                        else if (str_cmp(action->valuestring, "disallow") == 0) should_include = 0;
                    }
                }
            }

            if (!should_include) continue;

            cJSON* downloads = cJSON_GetObjectItem(lib, "downloads");
            
            // 检查是否有原生库（旧版本用 "natives" 字段 + classifiers，新版本用 classifiers 直接判断）
            int is_natives_lib = 0;
            cJSON* natives_field = cJSON_GetObjectItem(lib, "natives");
            if (natives_field && cJSON_IsObject(natives_field)) {
                // 旧版本格式：有 "natives": {"windows": "natives-windows"} 字段
                cJSON* win_native = cJSON_GetObjectItem(natives_field, "windows");
                if (win_native && cJSON_IsString(win_native)) {
                    is_natives_lib = 1;
                }
            }
            
            cJSON* classifiers = NULL;
            if (downloads && cJSON_IsObject(downloads)) {
                classifiers = cJSON_GetObjectItem(downloads, "classifiers");
            }
            if (!is_natives_lib && classifiers && cJSON_IsObject(classifiers)) {
                // 新版本格式：classifiers 中有 natives-windows
                if (cJSON_GetObjectItem(classifiers, "natives-windows")) {
                    is_natives_lib = 1;
                }
            }
            
            if (is_natives_lib && classifiers && cJSON_IsObject(classifiers)) {
                // 从 classifiers 中获取原生库路径
                cJSON* native_classifier = NULL;
                // 优先查找 natives-windows
                native_classifier = cJSON_GetObjectItem(classifiers, "natives-windows");
                // 其次查找 natives-windows-x64
                if (!native_classifier) native_classifier = cJSON_GetObjectItem(classifiers, "natives-windows-x64");
                // 再次查找 natives-windows-arm64
                if (!native_classifier) native_classifier = cJSON_GetObjectItem(classifiers, "natives-windows-arm64");
                
                if (native_classifier && cJSON_IsObject(native_classifier)) {
                    cJSON* native_path = cJSON_GetObjectItem(native_classifier, "path");
                    if (native_path && cJSON_IsString(native_path) && info->native_count < MAX_LIBS) {
                        info->natives[info->native_count++] = _strdup(native_path->valuestring);
                        print("  [NATIVE] "); print(native_path->valuestring); print("\n");
                    }
                }
            } else {
                // 普通库，从 artifact 获取路径
                cJSON* artifact = NULL;
                if (downloads && cJSON_IsObject(downloads)) {
                    artifact = cJSON_GetObjectItem(downloads, "artifact");
                }
                if (artifact && cJSON_IsObject(artifact)) {
                    cJSON* path = cJSON_GetObjectItem(artifact, "path");
                    if (path && cJSON_IsString(path)) {
                        int expected_size = 0;
                        cJSON* size_item = cJSON_GetObjectItem(artifact, "size");
                        if (size_item && cJSON_IsNumber(size_item)) expected_size = (int)size_item->valuedouble;

                        if (info->lib_count < MAX_LIBS) {
                            info->libraries[info->lib_count] = _strdup(path->valuestring);
                            info->lib_sizes[info->lib_count] = expected_size;
                            info->lib_count++;
                        }
                    }
                } else {
                    // 没有 downloads 字段时，从 name 构造路径
                    cJSON* name_item = cJSON_GetObjectItem(lib, "name");
                    if (name_item && cJSON_IsString(name_item) && info->lib_count < MAX_LIBS) {
                        char lib_path[MAX_LIB_PATH];
                        char name_copy[MAX_LIB_PATH];
                        safe_str_cpy(name_copy, sizeof(name_copy), name_item->valuestring);
                        
                        // 解析 Maven name: groupId:artifactId:version
                        char* first_colon = strchr(name_copy, ':');
                        if (first_colon) {
                            *first_colon = '\0';
                            char* group_id = name_copy;
                            char* rest = first_colon + 1;
                            char* second_colon = strchr(rest, ':');
                            if (second_colon) {
                                *second_colon = '\0';
                                char* artifact_id = rest;
                                char* version = second_colon + 1;
                                
                                // 构造路径: groupId/artifactId/version/artifactId-version.jar
                                // groupId 中的 . 替换为 /
                                char group_path[MAX_LIB_PATH];
                                safe_str_cpy(group_path, sizeof(group_path), group_id);
                                for (char* p = group_path; *p; p++) if (*p == '.') *p = '/';
                                
                                snprintf(lib_path, sizeof(lib_path), "%s/%s/%s/%s-%s.jar",
                                         group_path, artifact_id, version, artifact_id, version);
                                
                                info->libraries[info->lib_count] = _strdup(lib_path);
                                info->lib_sizes[info->lib_count] = 0;
                                info->lib_count++;
                            }
                        }
                    }
                }
            }
        }
    }

    cJSON* arguments = cJSON_GetObjectItem(root, "arguments");
    if (arguments && cJSON_IsObject(arguments)) {
        cJSON* jvm = cJSON_GetObjectItem(arguments, "jvm");
        if (jvm && cJSON_IsArray(jvm)) {
            int size = cJSON_GetArraySize(jvm);
            for (int i = 0; i < size; i++) {
                cJSON* arg = cJSON_GetArrayItem(jvm, i);
                if (arg && cJSON_IsString(arg)) {
                    info->jvm_args[info->jvm_arg_count++] = _strdup(arg->valuestring);
                } else if (arg && cJSON_IsObject(arg)) {
                    // 处理带rules的对象格式: {"rules": [...], "value": "..."} 或 {"rules": [...], "value": ["..."]}
                    cJSON* value = cJSON_GetObjectItem(arg, "value");
                    if (value) {
                        // 检查rules是否允许
                        cJSON* rules = cJSON_GetObjectItem(arg, "rules");
                        int allowed = 1; // 默认允许（无rules时）
                        if (rules && cJSON_IsArray(rules)) {
                            allowed = 0; // 有rules时默认不允许
                            int rules_size = cJSON_GetArraySize(rules);
                            for (int r = 0; r < rules_size; r++) {
                                cJSON* rule = cJSON_GetArrayItem(rules, r);
                                if (rule && cJSON_IsObject(rule)) {
                                    // 检查 OS 条件
                                    cJSON* os = cJSON_GetObjectItem(rule, "os");
                                    if (os && cJSON_IsObject(os)) {
                                        cJSON* os_name = cJSON_GetObjectItem(os, "name");
                                        if (os_name && cJSON_IsString(os_name)) {
#ifdef _WIN32
                                            if (strcmp(os_name->valuestring, "windows") != 0) continue;
#elif __linux__
                                            if (strcmp(os_name->valuestring, "linux") != 0) continue;
#elif __APPLE__
                                            if (strcmp(os_name->valuestring, "osx") != 0) continue;
#else
                                            continue;  // 未知平台跳过
#endif
                                        }
                                    }
                                    cJSON* action = cJSON_GetObjectItem(rule, "action");
                                    if (action && cJSON_IsString(action)) {
                                        // 检查是否有features规则
                                        cJSON* features = cJSON_GetObjectItem(rule, "features");
                                        if (features) {
                                            continue;
                                        }
                                        if (strcmp(action->valuestring, "allow") == 0) {
                                            allowed = 1;
                                            break;
                                        } else if (strcmp(action->valuestring, "disallow") == 0) {
                                            allowed = 0;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (allowed) {
                            if (cJSON_IsString(value)) {
                                info->jvm_args[info->jvm_arg_count++] = _strdup(value->valuestring);
                            } else if (cJSON_IsArray(value)) {
                                // value 是数组格式: ["arg1", "arg2", ...]
                                int val_size = cJSON_GetArraySize(value);
                                for (int v = 0; v < val_size && info->jvm_arg_count < MAX_LIBS; v++) {
                                    cJSON* val_item = cJSON_GetArrayItem(value, v);
                                    if (val_item && cJSON_IsString(val_item)) {
                                        info->jvm_args[info->jvm_arg_count++] = _strdup(val_item->valuestring);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        cJSON* game = cJSON_GetObjectItem(arguments, "game");
        if (game && cJSON_IsArray(game)) {
            int size = cJSON_GetArraySize(game);
            for (int i = 0; i < size; i++) {
                cJSON* arg = cJSON_GetArrayItem(game, i);
                if (arg && cJSON_IsString(arg)) {
                    info->game_args[info->game_arg_count++] = _strdup(arg->valuestring);
                } else if (arg && cJSON_IsObject(arg)) {
                    // 处理带rules的对象格式: {"rules": [...], "value": "..."} 或 {"rules": [...], "value": ["..."]}
                    cJSON* value = cJSON_GetObjectItem(arg, "value");
                    if (value) {
                        cJSON* rules = cJSON_GetObjectItem(arg, "rules");
                        int allowed = 1; // 默认允许（无rules时）
                        if (rules && cJSON_IsArray(rules)) {
                            allowed = 0; // 有rules时默认不允许
                            int rules_size = cJSON_GetArraySize(rules);
                            for (int r = 0; r < rules_size; r++) {
                                cJSON* rule = cJSON_GetArrayItem(rules, r);
                                if (rule && cJSON_IsObject(rule)) {
                                    // 检查 OS 条件
                                    cJSON* os = cJSON_GetObjectItem(rule, "os");
                                    if (os && cJSON_IsObject(os)) {
                                        cJSON* os_name = cJSON_GetObjectItem(os, "name");
                                        if (os_name && cJSON_IsString(os_name)) {
#ifdef _WIN32
                                            if (strcmp(os_name->valuestring, "windows") != 0) continue;
#elif __linux__
                                            if (strcmp(os_name->valuestring, "linux") != 0) continue;
#elif __APPLE__
                                            if (strcmp(os_name->valuestring, "osx") != 0) continue;
#else
                                            continue;
#endif
                                        }
                                    }
                                    cJSON* action = cJSON_GetObjectItem(rule, "action");
                                    if (action && cJSON_IsString(action)) {
                                        // 检查是否有features规则（如is_demo_user）
                                        // 如果有features，我们不支持，所以跳过
                                        cJSON* features = cJSON_GetObjectItem(rule, "features");
                                        if (features) {
                                            continue;
                                        }
                                        if (strcmp(action->valuestring, "allow") == 0) {
                                            allowed = 1;
                                            break;
                                        } else if (strcmp(action->valuestring, "disallow") == 0) {
                                            allowed = 0;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (allowed) {
                            if (cJSON_IsString(value)) {
                                info->game_args[info->game_arg_count++] = _strdup(value->valuestring);
                            } else if (cJSON_IsArray(value)) {
                                // value 是数组格式: ["arg1", "arg2", ...]
                                int val_size = cJSON_GetArraySize(value);
                                for (int v = 0; v < val_size && info->game_arg_count < MAX_LIBS; v++) {
                                    cJSON* val_item = cJSON_GetArrayItem(value, v);
                                    if (val_item && cJSON_IsString(val_item)) {
                                        info->game_args[info->game_arg_count++] = _strdup(val_item->valuestring);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return 1;
}

// 版本解析循环检测
#define MAX_RESOLVE_DEPTH 32
static char g_resolve_stack[MAX_RESOLVE_DEPTH][256];
static int g_resolve_depth = 0;

static int is_in_resolve_stack(const char* version_id) {
    for (int i = 0; i < g_resolve_depth; i++) {
        if (strcmp(g_resolve_stack[i], version_id) == 0) return 1;
    }
    return 0;
}

int resolve_version(const char* version_id, VersionInfo* result) {
    // 循环检测
    if (g_resolve_depth >= MAX_RESOLVE_DEPTH) {
        print("[WARN] Max resolve depth reached for: "); print(version_id); print("\n");
        version_info_init(result);
        safe_str_cpy(result->id, sizeof(result->id), version_id);
        return 1;
    }
    if (is_in_resolve_stack(version_id)) {
        print("[WARN] Circular dependency detected for: "); print(version_id); print("\n");
        version_info_init(result);
        safe_str_cpy(result->id, sizeof(result->id), version_id);
        return 1;
    }
    
    // 入栈
    safe_str_cpy(g_resolve_stack[g_resolve_depth], sizeof(g_resolve_stack[g_resolve_depth]), version_id);
    g_resolve_depth++;
    
    version_info_init(result);
    if (!parse_version_json(version_id, result)) {
        g_resolve_depth--;
        safe_str_cpy(result->id, sizeof(result->id), version_id);
        return 0;
    }
    
    // 确定父版本 ID（优先 inheritsFrom，其次 clientVersion）
    const char* parent_id = NULL;
    if (strlen(result->inheritsFrom) > 0) parent_id = result->inheritsFrom;
    else if (strlen(result->clientVersion) > 0) parent_id = result->clientVersion;
    
    if (parent_id && strlen(parent_id) > 0) {
        VersionInfo parent;
        if (resolve_version(parent_id, &parent)) {
            for (int i = 0; i < parent.lib_count; i++) {
                int found = 0;
                for (int j = 0; j < result->lib_count; j++) if (strcmp(parent.libraries[i], result->libraries[j]) == 0) { found = 1; break; }
                if (!found && result->lib_count < MAX_LIBS) {
                    result->libraries[result->lib_count] = _strdup(parent.libraries[i]);
                    result->lib_sizes[result->lib_count] = parent.lib_sizes[i];
                    result->lib_count++;
                }
            }
            for (int i = 0; i < parent.native_count; i++) {
                int found = 0;
                for (int j = 0; j < result->native_count; j++) if (strcmp(parent.natives[i], result->natives[j]) == 0) { found = 1; break; }
                if (!found && result->native_count < MAX_LIBS) result->natives[result->native_count++] = _strdup(parent.natives[i]);
            }
            // 合并 JVM 参数（去重）
            for (int i = 0; i < parent.jvm_arg_count; i++) {
                int found = 0;
                for (int j = 0; j < result->jvm_arg_count; j++) {
                    if (strcmp(parent.jvm_args[i], result->jvm_args[j]) == 0) { found = 1; break; }
                }
                if (!found && result->jvm_arg_count < MAX_LIBS) {
                    result->jvm_args[result->jvm_arg_count++] = _strdup(parent.jvm_args[i]);
                }
            }
            // 合并游戏参数（去重）
            for (int i = 0; i < parent.game_arg_count; i++) {
                int found = 0;
                for (int j = 0; j < result->game_arg_count; j++) {
                    if (strcmp(parent.game_args[i], result->game_args[j]) == 0) { found = 1; break; }
                }
                if (!found && result->game_arg_count < MAX_LIBS) {
                    result->game_args[result->game_arg_count++] = _strdup(parent.game_args[i]);
                }
            }
            // 继承父版本的 minecraftArguments（如果子版本没有）
            if (strlen(result->minecraftArguments) == 0 && strlen(parent.minecraftArguments) > 0) {
                safe_str_cpy(result->minecraftArguments, sizeof(result->minecraftArguments), parent.minecraftArguments);
            }
            // mainClass: 子版本优先（Fabric/Forge 有自己的 mainClass）
            if (strlen(result->mainClass) == 0 && strlen(parent.mainClass) > 0) {
                safe_str_cpy(result->mainClass, sizeof(result->mainClass), parent.mainClass);
            }
            // assetIndex: 子版本优先，如果没有则继承父版本
            if (strlen(result->assetIndex) == 0 && strlen(parent.assetIndex) > 0) {
                safe_str_cpy(result->assetIndex, sizeof(result->assetIndex), parent.assetIndex);
            }
            // assets: 子版本优先，如果没有则继承父版本
            if (strlen(result->assets) == 0 && strlen(parent.assets) > 0) {
                safe_str_cpy(result->assets, sizeof(result->assets), parent.assets);
            }
            if (result->java_major == 8 && parent.java_major > 8) result->java_major = parent.java_major;
            version_info_free(&parent);
        }
    }
    if (strlen(result->id) == 0) safe_str_cpy(result->id, sizeof(result->id), version_id);
    
    // 出栈
    g_resolve_depth--;
    return 1;
}

// ==================== Classpath Building ====================
int build_classpath(VersionInfo* info, char* classpath_buf, size_t buf_size) {
    classpath_buf[0] = '\0';
    size_t used = 0;
    print("=== Building Classpath ===\n");
    print("  Libraries to add: "); print_int(info->lib_count); print("\n");
    for (int i = 0; i < info->lib_count; i++) {
        char full_path[MAX_PATH_LEN];
        build_lib_path(full_path, sizeof(full_path), info->libraries[i]);
        print("  ["); print_int(i+1); print("/"); print_int(info->lib_count); print("] ");
        char* last_sep = strrchr(full_path, PATH_SEP);
        if (last_sep) print(last_sep + 1);
        else print(full_path);
        if (!file_exists(full_path)) print(" (MISSING)");
        print("\n");
        size_t len = strlen(full_path);
        if (used + len + 1 >= buf_size) {
            print("  [WARN] Classpath buffer full!\n");
            return 0;
        }
        if (used > 0) classpath_buf[used++] = CP_SEP;
        memcpy(classpath_buf + used, full_path, len);
        used += len;
    }

    // Add current version JAR
    char version_jar[MAX_PATH_LEN];
    build_mc_path(version_jar, sizeof(version_jar), info->id, "versions", ".jar");
    size_t len = strlen(version_jar);
    if (used + len + 1 >= buf_size) return 0;
    if (used > 0) classpath_buf[used++] = ';';
    memcpy(classpath_buf + used, version_jar, len);
    used += len;

    // If this version inherits from a parent (Fabric/Forge), also add parent's client JAR
    // The parent JAR contains net.minecraft.client.main.Main which is required to start the game
    const char* parent_id = NULL;
    if (strlen(info->inheritsFrom) > 0) parent_id = info->inheritsFrom;
    else if (strlen(info->clientVersion) > 0) parent_id = info->clientVersion;
    
    // Don't add parent if it's the same as current version
    // For Forge/NeoForge (--add-modules ALL-MODULE-PATH), skip parent jar to avoid module conflicts
    int skip_parent = 0;
    if (parent_id && str_cmp(parent_id, info->id) != 0) {
        for (int i = 0; i < info->jvm_arg_count; i++) {
            if (strstr(info->jvm_args[i], "--add-modules")) { skip_parent = 1; break; }
        }
        if (!skip_parent) {
            char parent_jar[MAX_PATH_LEN];
            build_mc_path(parent_jar, sizeof(parent_jar), parent_id, "versions", ".jar");
            len = strlen(parent_jar);
            if (used + len + 1 < buf_size) {
                classpath_buf[used++] = ';';
                memcpy(classpath_buf + used, parent_jar, len);
                used += len;
                print("  [PARENT] "); print(parent_id); print(".jar added to classpath\n");
            }
        }
    }

    classpath_buf[used] = '\0';
    return 1;
}

// ==================== ZIP Extraction (Pure C, No PowerShell) ====================

// Minimal ZIP file parser - JAR files are ZIP files
#pragma pack(push, 1)
typedef struct {
    unsigned int signature;
    unsigned short version;
    unsigned short flags;
    unsigned short compression;
    unsigned short mod_time;
    unsigned short mod_date;
    unsigned int crc32;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned short filename_len;
    unsigned short extra_len;
} zip_local_header;

typedef struct {
    unsigned int signature;
    unsigned short version_made;
    unsigned short version_needed;
    unsigned short flags;
    unsigned short compression;
    unsigned short mod_time;
    unsigned short mod_date;
    unsigned int crc32;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned short filename_len;
    unsigned short extra_len;
    unsigned short comment_len;
    unsigned short disk_start;
    unsigned short internal_attr;
    unsigned int external_attr;
    unsigned int local_header_offset;
} zip_central_dir;
#pragma pack(pop)

// Simple inflate buffer for stored (uncompressed) entries
static int extract_zip_entry(FILE* zip_file, zip_local_header* header, const char* dest_path) {
    // Create parent directories
    char dir_path[MAX_PATH_LEN];
    safe_str_cpy(dir_path, sizeof(dir_path), dest_path);
    char* last_sep = strrchr(dir_path, PATH_SEP);
    if (!last_sep && PATH_SEP != '/') last_sep = strrchr(dir_path, '/');
    if (last_sep) {
        *last_sep = '\0';
        // Create directories recursively
        for (char* p = dir_path; *p; p++) {
            if (*p == PATH_SEP || (PATH_SEP != '/' && *p == '/')) {
                char saved = *p;
                *p = '\0';
#ifdef PLATFORM_WINAPI
                CreateDirectoryA(dir_path, NULL);
#else
                mkdir(dir_path, 0755);
#endif
                *p = saved;
            }
        }
#ifdef PLATFORM_WINAPI
        CreateDirectoryA(dir_path, NULL);
#else
        mkdir(dir_path, 0755);
#endif
    }
    
    // Check if it's a directory entry
    if (header->filename_len > 0 && (dest_path[strlen(dest_path) - 1] == PATH_SEP || (PATH_SEP != '/' && dest_path[strlen(dest_path) - 1] == '/'))) {
#ifdef PLATFORM_WINAPI
        CreateDirectoryA(dest_path, NULL);
#else
        mkdir(dest_path, 0755);
#endif
        // Skip directory entry data
        fseek(zip_file, header->compressed_size, SEEK_CUR);
        return 1;
    }
    
    // For stored (compression=0) entries, just read and write
    if (header->compression == 0) {
        FILE* out = fopen(dest_path, "wb");
        if (!out) return 0;
        
        unsigned char* buf = (unsigned char*)malloc(65536);
        if (!buf) { fclose(out); return 0; }
        
        unsigned int remaining = header->compressed_size;
        int success = 1;
        while (remaining > 0) {
            unsigned int to_read = remaining < 65536 ? remaining : 65536;
            size_t rd = fread(buf, 1, to_read, zip_file);
            if (rd != to_read) { success = 0; break; }
            if (fwrite(buf, 1, rd, out) != rd) { success = 0; break; }
            remaining -= (unsigned int)rd;
        }
        
        free(buf);
        fclose(out);
        return success;
    }
    
    // For deflate (compression=8) entries, use zlib
    if (header->compression == 8) {
        // Read compressed data
        unsigned char* compressed = (unsigned char*)malloc(header->compressed_size);
        if (!compressed) return 0;
        
        size_t rd = fread(compressed, 1, header->compressed_size, zip_file);
        if (rd != header->compressed_size) {
            free(compressed);
            return 0;
        }
        
        // Decompress with zlib
        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        strm.next_in = compressed;
        strm.avail_in = header->compressed_size;
        
        // Use negative window bits for raw deflate (no zlib header)
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
            free(compressed);
            return 0;
        }
        
        FILE* out = fopen(dest_path, "wb");
        if (!out) {
            inflateEnd(&strm);
            free(compressed);
            return 0;
        }
        
        unsigned char* out_buf = (unsigned char*)malloc(65536);
        if (!out_buf) {
            fclose(out);
            inflateEnd(&strm);
            free(compressed);
            return 0;
        }
        
        int success = 1;
        do {
            strm.next_out = out_buf;
            strm.avail_out = 65536;
            
            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                success = 0;
                break;
            }
            
            unsigned int have = 65536 - strm.avail_out;
            if (have > 0) {
                if (fwrite(out_buf, 1, have, out) != have) {
                    success = 0;
                    break;
                }
            }
            
            if (ret == Z_STREAM_END) break;
        } while (strm.avail_out == 0);
        
        free(out_buf);
        fclose(out);
        inflateEnd(&strm);
        free(compressed);
        return success;
    }
    
    // For other compression methods, skip
    fseek(zip_file, header->compressed_size, SEEK_CUR);
    return 1;
}

int extract_zip(const char* zip_path, const char* dest_dir) {
    print("Extracting: "); print(zip_path); print(" -> "); print(dest_dir); print("\n");
    
    // Pure C extraction with zlib deflate support
    FILE* zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        print("Cannot open ZIP file: "); print(zip_path); print("\n");
        return 0;
    }
    
    // Create destination directory
#ifdef PLATFORM_WINAPI
    CreateDirectoryA(dest_dir, NULL);
#else
    mkdir(dest_dir, 0755);
#endif
    
    // Read central directory to find all entries
    fseek(zip_file, 0, SEEK_END);
    long file_size = ftell(zip_file);
    
    // Search for EOCD signature (0x06054b50) from end of file
    long eocd_pos = -1;
    unsigned char eocd_buf[4];
    for (long i = file_size - 22; i >= 0 && i >= file_size - 65557; i--) {
        fseek(zip_file, i, SEEK_SET);
        fread(eocd_buf, 1, 4, zip_file);
        if (eocd_buf[0] == 0x50 && eocd_buf[1] == 0x4b && eocd_buf[2] == 0x05 && eocd_buf[3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }
    
    int success = 1;
    
    if (eocd_pos >= 0) {
            // Read EOCD
            fseek(zip_file, eocd_pos + 10, SEEK_SET);
            unsigned short central_dir_entries;
            fread(&central_dir_entries, 2, 1, zip_file);
            
            fseek(zip_file, eocd_pos + 16, SEEK_SET);
            unsigned int central_dir_offset;
            fread(&central_dir_offset, 4, 1, zip_file);
            
            // Read central directory entries
            fseek(zip_file, central_dir_offset, SEEK_SET);
        
        char filename[MAX_PATH_LEN];
        char dest_path[MAX_PATH_LEN];
        unsigned int current_offset = central_dir_offset;
        
        for (int i = 0; i < central_dir_entries; i++) {
            // Seek to current entry position
            fseek(zip_file, current_offset, SEEK_SET);
            
            zip_central_dir cd;
            if (fread(&cd, sizeof(cd), 1, zip_file) != 1) break;
            
            if (cd.signature != 0x02014b50) break;
            
            // Read filename
            if (cd.filename_len >= MAX_PATH_LEN) {
                current_offset += sizeof(cd) + cd.filename_len + cd.extra_len + cd.comment_len;
                continue;
            }
            
            fread(filename, 1, cd.filename_len, zip_file);
            filename[cd.filename_len] = '\0';
            
            // Convert forward slashes to platform-specific separator
            for (char* p = filename; *p; p++) {
                if (*p == '/') *p = PATH_SEP;
            }
            
            // Build destination path
            safe_str_cpy(dest_path, sizeof(dest_path), dest_dir);
            safe_str_cat(dest_path, sizeof(dest_path), PATH_SEP_STR);
            safe_str_cat(dest_path, sizeof(dest_path), filename);
            
            // Read local header to get compressed data
            long saved_pos = ftell(zip_file);
            fseek(zip_file, cd.local_header_offset, SEEK_SET);
            zip_local_header lh;
            fread(&lh, sizeof(lh), 1, zip_file);
            
            // Skip local header variable parts
            fseek(zip_file, lh.filename_len + lh.extra_len, SEEK_CUR);
            
            // Extract entry (supports stored + deflate)
            if (!extract_zip_entry(zip_file, &lh, dest_path)) {
                print("Failed to extract: "); print(filename); print("\n");
                success = 0;
            }
            
            // Restore position in central directory
            fseek(zip_file, saved_pos, SEEK_SET);
            current_offset += sizeof(cd) + cd.filename_len + cd.extra_len + cd.comment_len;
        }
    }
    
    fclose(zip_file);
    return success;
}

// 递归删除目录
static int delete_directory_recursive(const char* path) {
#ifdef PLATFORM_WINAPI
    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH_LEN];
    safe_str_cpy(search_path, sizeof(search_path), path);
    safe_str_cat(search_path, sizeof(search_path), "\\*");
    
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return DeleteFileA(path) || RemoveDirectoryA(path);
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH_LEN];
        safe_str_cpy(full_path, sizeof(full_path), path);
        safe_str_cat(full_path, sizeof(full_path), "\\");
        safe_str_cat(full_path, sizeof(full_path), find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            delete_directory_recursive(full_path);
        } else {
            DeleteFileA(full_path);
        }
    } while (FindNextFileA(hFind, &find_data));
    
    FindClose(hFind);
    RemoveDirectoryA(path);
#else
    DIR* dir = opendir(path);
    if (!dir) {
        unlink(path);
        return rmdir(path) == 0;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            delete_directory_recursive(full_path);
        } else {
            unlink(full_path);
        }
    }
    closedir(dir);
    rmdir(path);
#endif
    return 1;
}

// 复制文件
static int copy_file_simple(const char* src, const char* dst) {
#ifdef PLATFORM_WINAPI
    return CopyFileA(src, dst, FALSE);
#else
    FILE* sf = fopen(src, "rb");
    if (!sf) return 0;
    FILE* df = fopen(dst, "wb");
    if (!df) { fclose(sf); return 0; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), sf)) > 0) {
        fwrite(buf, 1, n, df);
    }
    fclose(sf);
    fclose(df);
    return 1;
#endif
}

// 递归复制目录
static int copy_directory_recursive(const char* src, const char* dst) {
#ifdef PLATFORM_WINAPI
    CreateDirectoryA(dst, NULL);
    
    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH_LEN];
    safe_str_cpy(search_path, sizeof(search_path), src);
    safe_str_cat(search_path, sizeof(search_path), "\\*");
    
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    
    int success = 1;
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        
        char src_path[MAX_PATH_LEN];
        char dst_path[MAX_PATH_LEN];
        safe_str_cpy(src_path, sizeof(src_path), src);
        safe_str_cat(src_path, sizeof(src_path), "\\");
        safe_str_cat(src_path, sizeof(src_path), find_data.cFileName);
        
        safe_str_cpy(dst_path, sizeof(dst_path), dst);
        safe_str_cat(dst_path, sizeof(dst_path), "\\");
        safe_str_cat(dst_path, sizeof(dst_path), find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copy_directory_recursive(src_path, dst_path)) success = 0;
        } else {
            if (!copy_file_simple(src_path, dst_path)) success = 0;
        }
    } while (FindNextFileA(hFind, &find_data));
    
    FindClose(hFind);
#else
    mkdir(dst, 0755);
    
    DIR* dir = opendir(src);
    if (!dir) return 0;
    
    int success = 1;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char src_path[MAX_PATH_LEN];
        char dst_path[MAX_PATH_LEN];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        
        struct stat st;
        if (stat(src_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!copy_directory_recursive(src_path, dst_path)) success = 0;
        } else {
            if (!copy_file_simple(src_path, dst_path)) success = 0;
        }
    }
    closedir(dir);
#endif
    return success;
}

int extract_natives(VersionInfo* info) {
    char natives_dir[MAX_PATH_LEN];
    build_mc_path(natives_dir, sizeof(natives_dir), info->id, "versions", "-natives");
#ifdef PLATFORM_WINAPI
    CreateDirectoryA(natives_dir, NULL);
#else
    mkdir(natives_dir, 0755);
#endif

    // 清空 natives 目录以确保干净的状态
    delete_directory_recursive(natives_dir);
#ifdef PLATFORM_WINAPI
    CreateDirectoryA(natives_dir, NULL);
#else
    mkdir(natives_dir, 0755);
#endif

    int success = 1;
    for (int i = 0; i < info->native_count; i++) {
        char full_path[MAX_PATH_LEN];
        build_lib_path(full_path, sizeof(full_path), info->natives[i]);
        
        // Try original path first
        if (!file_exists(full_path)) {
            // Try natives-windows/natives-linux variant
            char natives_path[MAX_PATH_LEN];
            safe_str_cpy(natives_path, sizeof(natives_path), full_path);
            char* dot_jar = strrchr(natives_path, '.');
            if (dot_jar) {
                *dot_jar = '\0';
                safe_str_cat(natives_path, sizeof(natives_path), "-");
                safe_str_cat(natives_path, sizeof(natives_path), NATIVE_SUFFIX);
                safe_str_cat(natives_path, sizeof(natives_path), ".jar");
                if (file_exists(natives_path)) {
                    safe_str_cpy(full_path, sizeof(full_path), natives_path);
                }
            }
        }
        
        if (file_exists(full_path)) {
            // 直接解压到 natives 目录
            if (!extract_zip(full_path, natives_dir)) {
                print("Failed to extract: "); print(full_path); print("\n");
                success = 0;
            }
        } else { print("Native not found: "); print(full_path); print("\n"); }
    }

    return success;
}

// ==================== Placeholder Replacement ====================
void replace_all_placeholders(char* arg, size_t arg_size,
                              const char* classpath,
                              const char* asset_index,
                              const char* launcher_name,
                              const char* launcher_version,
                              const char* clientid,
                              const char* auth_xuid,
                              const char* auth_player_name,
                              const char* version_name,
                              const char* game_directory,
                              const char* game_assets,
                              const char* assets_root,
                              const char* auth_uuid,
                              const char* auth_access_token,
                              const char* user_type,
                              const char* version_type,
                              const char* natives_directory,
                              const char* library_directory,
                              const char* classpath_separator,
                              const char* user_properties) {
    /* 循环替换所有出现（不只是第一个），避免 Forge -p 参数中多个占位符遗漏 */
#define REPLACE_ALL(placeholder, value) do { \
    while (1) { \
        char* pos = strstr(arg, placeholder); \
        if (!pos) break; \
        ptrdiff_t offset = pos - arg; \
        size_t plen = strlen(placeholder); \
        size_t vlen = strlen(value); \
        size_t old_len = strlen(arg); \
        size_t tail_len = old_len - offset - plen; \
        if (offset + vlen + tail_len >= arg_size - 1) break; \
        memmove(pos + vlen, pos + plen, tail_len + 1); \
        memcpy(pos, value, vlen); \
    } \
} while(0)
    REPLACE_ALL("${classpath}", classpath);
    REPLACE_ALL("${classpath_separator}", classpath_separator);
    REPLACE_ALL("${assets_index_name}", asset_index);
    REPLACE_ALL("${launcher_name}", launcher_name);
    REPLACE_ALL("${launcher_version}", launcher_version);
    REPLACE_ALL("${clientid}", clientid);
    REPLACE_ALL("${auth_xuid}", auth_xuid);
    REPLACE_ALL("${auth_player_name}", auth_player_name);
    REPLACE_ALL("${version_name}", version_name);
    REPLACE_ALL("${game_directory}", game_directory);
    REPLACE_ALL("${game_assets}", game_assets);
    REPLACE_ALL("${assets_root}", assets_root);
    REPLACE_ALL("${auth_uuid}", auth_uuid);
    REPLACE_ALL("${auth_access_token}", auth_access_token);
    REPLACE_ALL("${auth_session}", auth_access_token);
    REPLACE_ALL("${user_type}", user_type);
    REPLACE_ALL("${version_type}", version_type);
    REPLACE_ALL("${natives_directory}", natives_directory);
    REPLACE_ALL("${library_directory}", library_directory);
    REPLACE_ALL("${user_properties}", user_properties);
#undef REPLACE_ALL
}

int download_library(const char* lib_path) {
    char full_path[MAX_PATH_LEN];
    build_lib_path(full_path, sizeof(full_path), lib_path);

    if (file_exists(full_path)) return 1;

    char url[MAX_PATH_LEN + 128];
    safe_str_cpy(url, sizeof(url), LIBRARIES_URL);
    safe_str_cat(url, sizeof(url), "/");
    char url_path[MAX_LIB_PATH];
    safe_str_cpy(url_path, sizeof(url_path), lib_path);
    for (char* q = url_path; *q; q++) if (*q == PATH_SEP) *q = '/';
    safe_str_cat(url, sizeof(url), url_path);

    print("Downloading: "); print(url); print("\n");
    create_parent_dirs(full_path);
    return http_get_file(url, full_path);
}

int download_authlib_injector(char* out_path, size_t out_size) {
    // 放到游戏目录下
    safe_str_cpy(out_path, out_size, mc_path);
    safe_str_cat(out_path, out_size, PATH_SEP_STR);
    safe_str_cat(out_path, out_size, AUTHLIB_JAR_NAME);

    if (file_exists(out_path)) {
        print("authlib-injector already exists.\n");
        return 1;
    }

    print("Downloading authlib-injector...\n");
    if (http_get_file(AUTHLIB_URL, out_path)) {
        print("authlib-injector downloaded successfully.\n");
        return 1;
    } else {
        print("Failed to download authlib-injector. Please download authlib-injector-1.2.7.jar from another source and move it into the .minecraft folder.\n");
        out_path[0] = '\0';
        return 0;
    }
}

int download_version_json(const char* version_id) {
    char json_path[MAX_PATH_LEN];
    build_mc_path(json_path, sizeof(json_path), version_id, "versions", ".json");

    if (file_exists(json_path)) return 1;

    char url[512];
    safe_str_cpy(url, sizeof(url), MC_BASE_URL);
    safe_str_cat(url, sizeof(url), "/mc/game/version_manifest.json");

    char* manifest = http_get(url);
    if (!manifest) return 0;

    cJSON* root = cJSON_Parse(manifest);
    free(manifest);
    if (!root) return 0;

    cJSON* versions = cJSON_GetObjectItem(root, "versions");
    if (versions && cJSON_IsArray(versions)) {
        int size = cJSON_GetArraySize(versions);
        for (int i = 0; i < size; i++) {
            cJSON* ver = cJSON_GetArrayItem(versions, i);
            cJSON* id = cJSON_GetObjectItem(ver, "id");
            cJSON* url_item = cJSON_GetObjectItem(ver, "url");
            if (id && cJSON_IsString(id) && str_cmp(id->valuestring, version_id) == 0 && url_item && cJSON_IsString(url_item)) {
                create_parent_dirs(json_path);
                int result = http_get_file(url_item->valuestring, json_path);
                cJSON_Delete(root);
                return result;
            }
        }
    }
    cJSON_Delete(root);
    return 0;
}

// Read entire file into a malloc'd string
int verify_and_download_files(const char* version_id) {
    print("=== File Verification ===\n"); print("Version: "); print(version_id); print("\n");
    print("Verifying game files...\n");
    if (!download_version_json(version_id)) {
        print("Warning: Failed to download version JSON, using local files\n");
    }

    VersionInfo info;
    if (!resolve_version(version_id, &info)) return 0;

    print("Resolved version: "); print(info.id); print("\n");
    if (str_len(info.inheritsFrom) > 0) { print("  Inherits from: "); print(info.inheritsFrom); print("\n"); }
    if (str_len(info.clientVersion) > 0) { print("  Client version: "); print(info.clientVersion); print("\n"); }
    print("  Total libraries: "); print_int(info.lib_count); print("\n");
    print("  Total natives: "); print_int(info.native_count); print("\n");

    int downloaded = 0;
    int verified = 0;
    int size_mismatch = 0;
    for (int i = 0; i < info.lib_count; i++) {
        char full_path[MAX_PATH_LEN];
        build_lib_path(full_path, sizeof(full_path), info.libraries[i]);

        int need_download = 0;
        if (!file_exists(full_path)) {
            need_download = 1;
            print("  [MISS] "); print(info.libraries[i]); print("\n");
        } else if (info.lib_sizes[i] > 0) {
            // Verify file size matches expected size from JSON
            FILE* f = fopen(full_path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long actual_size = ftell(f);
                fclose(f);
                if (actual_size != info.lib_sizes[i]) {
                    need_download = 1;
                    size_mismatch++;
                    print("  [SIZE] "); print(info.libraries[i]);
                    print(" (expected "); print_int(info.lib_sizes[i]);
                    print(", got "); print_int((int)actual_size); print(")\n");
                } else {
                    verified++;
                    print("  [OK] "); print(info.libraries[i]); print("\n");
                }
            } else {
                need_download = 1;
                print("  [ERR] Cannot open: "); print(info.libraries[i]); print("\n");
            }
        } else {
            verified++;
        }

        if (need_download) {
            int dl_ok = 0;
            char url[MAX_PATH_LEN + 128];
            safe_str_cpy(url, sizeof(url), LIBRARIES_URL);
            safe_str_cat(url, sizeof(url), "/");
            char url_path[MAX_LIB_PATH];
            safe_str_cpy(url_path, sizeof(url_path), info.libraries[i]);
            for (char* q = url_path; *q; q++) if (*q == '\\') *q = '/';
            safe_str_cat(url, sizeof(url), url_path);

            print("  [DL] "); print(info.libraries[i]); print("\n");
            print("    Source: "); print(url); print("\n");
            print("    Target: "); print(full_path); print("\n");
            create_parent_dirs(full_path);
            if (http_get_file(url, full_path)) {
                dl_ok = 1;
            } else {
                /* 尝试从 JSON 中查找自定义 Maven URL (Quilt 等使用 fabricmc.net/quiltmc.org) */
                char json_path[MAX_PATH_LEN];
                build_mc_path(json_path, sizeof(json_path), version_id, "versions", ".json");
                char* jbuf = read_file(json_path);
                if (jbuf) {
                    cJSON* jroot = cJSON_Parse(jbuf);
                    if (jroot) {
                        cJSON* libs = cJSON_GetObjectItem(jroot, "libraries");
                        if (libs && cJSON_IsArray(libs)) {
                            int jsize = cJSON_GetArraySize(libs);
                            for (int j = 0; j < jsize && !dl_ok; j++) {
                                cJSON* jlib = cJSON_GetArrayItem(libs, j);
                                if (!jlib || !cJSON_IsObject(jlib)) continue;
                                cJSON* jurl = cJSON_GetObjectItem(jlib, "url");
                                cJSON* jname = cJSON_GetObjectItem(jlib, "name");
                                if (!jurl || !cJSON_IsString(jurl) || !jname || !cJSON_IsString(jname)) continue;
                                /* 对比 Maven name 与 lib 路径是否匹配 */
                                char nc[MAX_LIB_PATH]; safe_str_cpy(nc, sizeof(nc), jname->valuestring);
                                char* c1 = strchr(nc, ':'); if (!c1) continue; *c1 = '\0';
                                char* c2 = strchr(c1+1, ':'); if (!c2) continue; *c2 = '\0';
                                char gp[MAX_LIB_PATH]; safe_str_cpy(gp, sizeof(gp), nc);
                                for (char* p = gp; *p; p++) if (*p == '.') *p = '/';
                                char expected[MAX_LIB_PATH];
                                snprintf(expected, sizeof(expected), "%s/%s/%s/%s-%s.jar",
                                         gp, c1+1, c2+1, c1+1, c2+1);
                                if (str_cmp(info.libraries[i], expected) == 0) {
                                    char cust_url[MAX_PATH_LEN+128];
                                    safe_str_cpy(cust_url, sizeof(cust_url), jurl->valuestring);
                                    size_t ulen = str_len(cust_url);
                                    if (ulen > 0 && cust_url[ulen-1] != '/') safe_str_cat(cust_url, sizeof(cust_url), "/");
                                    safe_str_cat(cust_url, sizeof(cust_url), url_path);
                                    print("    Retry: "); print(cust_url); print("\n");
                                    if (http_get_file(cust_url, full_path)) dl_ok = 1;
                                }
                            }
                        }
                        cJSON_Delete(jroot);
                    }
                    free(jbuf);
                }
            }
            if (dl_ok) {
                downloaded++;
                print("  [DONE] Downloaded: "); print(info.libraries[i]); print("\n");
            } else {
                print("  [FAIL] Failed to download: "); print(info.libraries[i]); print("\n");
            }
        }
    }

    // 下载原生库（natives）
    if (info.native_count > 0) {
        print("=== Downloading Native Libraries ===\n");
    }
    for (int i = 0; i < info.native_count; i++) {
        char full_path[MAX_PATH_LEN];
        build_lib_path(full_path, sizeof(full_path), info.natives[i]);

        if (!file_exists(full_path)) {
            char url[MAX_PATH_LEN + 128];
            safe_str_cpy(url, sizeof(url), LIBRARIES_URL);
            safe_str_cat(url, sizeof(url), "/");
            char url_path[MAX_LIB_PATH];
            safe_str_cpy(url_path, sizeof(url_path), info.natives[i]);
            for (char* q = url_path; *q; q++) if (*q == PATH_SEP) *q = '/';
            safe_str_cat(url, sizeof(url), url_path);

            print("  [NATIVE-DL] "); print(info.natives[i]); print("\n");
            print("    Source: "); print(url); print("\n");
            print("    Target: "); print(full_path); print("\n");
            create_parent_dirs(full_path);
            if (http_get_file(url, full_path)) {
                downloaded++;
                print("  [DONE] Downloaded native: "); print(info.natives[i]); print("\n");
            } else {
                print("  [FAIL] Failed to download native: "); print(info.natives[i]); print("\n");
            }
        } else {
            print("  [NATIVE-OK] "); print(info.natives[i]); print("\n");
        }
    }

    char version_jar[MAX_PATH_LEN];
    build_mc_path(version_jar, sizeof(version_jar), info.id, "versions", ".jar");

    if (!file_exists(version_jar)) {
        create_parent_dirs(version_jar);
        int downloaded_jar = 0;

        // 优先使用版本 JSON 中的 client_url（适用于新版本如 26.1.2）
        if (str_len(info.client_url) > 0) {
            print("=== Downloading Client JAR ===\n");
            print("  Source: "); print(info.client_url); print("\n");
            print("  Target: "); print(version_jar); print("\n");
            if (http_get_file(info.client_url, version_jar)) {
                downloaded_jar = 1;
            }
        }

        // 如果有 inheritsFrom 或 clientVersion（如 Fabric/Forge），尝试从父版本获取 client_url
        const char* parent_for_url = NULL;
        if (str_len(info.inheritsFrom) > 0) parent_for_url = info.inheritsFrom;
        else if (str_len(info.clientVersion) > 0) parent_for_url = info.clientVersion;
        
        if (!downloaded_jar && parent_for_url) {
            VersionInfo parent;
            version_info_init(&parent);
            if (parse_version_json(parent_for_url, &parent)) {
                if (str_len(parent.client_url) > 0) {
                    print("=== Downloading Client JAR (from parent) ===\n");
                    print("  Source: "); print(parent.client_url); print("\n");
                    print("  Target: "); print(version_jar); print("\n");
                    if (http_get_file(parent.client_url, version_jar)) {
                        downloaded_jar = 1;
                    }
                }
                version_info_free(&parent);
            }
        }

        // 如果版本 JSON 中没有 client_url，使用旧格式（适用于旧版本）
        // 如果有 inheritsFrom/clientVersion，使用它作为版本 ID，而非当前版本 ID
        if (!downloaded_jar) {
            char jar_url[MAX_PATH_LEN + 128];
            const char* jar_version_id = parent_for_url ? parent_for_url : version_id;
            safe_str_cpy(jar_url, sizeof(jar_url), LIBRARIES_URL);
            safe_str_cat(jar_url, sizeof(jar_url), "/net/minecraft/client/");
            safe_str_cat(jar_url, sizeof(jar_url), jar_version_id);
            safe_str_cat(jar_url, sizeof(jar_url), "/client-");
            safe_str_cat(jar_url, sizeof(jar_url), jar_version_id);
            safe_str_cat(jar_url, sizeof(jar_url), ".jar");

            print("=== Downloading Client JAR (legacy) ===\n");
            print("  Source: "); print(jar_url); print("\n");
            print("  Target: "); print(version_jar); print("\n");
            if (http_get_file(jar_url, version_jar)) {
                downloaded_jar = 1;
            }
        }

        if (downloaded_jar) {
            downloaded++;
        }
    }

    // Download asset index JSON
    char asset_index_path[MAX_PATH_LEN];
    safe_str_cpy(asset_index_path, sizeof(asset_index_path), mc_path);
    safe_str_cat(asset_index_path, sizeof(asset_index_path), PATH_SEP_STR "assets" PATH_SEP_STR "indexes" PATH_SEP_STR);
    safe_str_cat(asset_index_path, sizeof(asset_index_path), info.assetIndex);
    safe_str_cat(asset_index_path, sizeof(asset_index_path), ".json");

    if (str_len(info.assetIndexUrl) > 0 && !file_exists(asset_index_path)) {
        create_parent_dirs(asset_index_path);
        print("=== Downloading Asset Index ===\n");
        print("  Source: "); print(info.assetIndexUrl); print("\n");
        print("  Target: "); print(asset_index_path); print("\n");
        if (http_get_file(info.assetIndexUrl, asset_index_path)) {
            downloaded++;
        }
    }

    // Download asset files from asset index
    if (file_exists(asset_index_path)) {
        char* asset_json_content = read_file(asset_index_path);
        if (asset_json_content) {
            cJSON* asset_root = cJSON_Parse(asset_json_content);
            free(asset_json_content);
            if (asset_root) {
                cJSON* objects = cJSON_GetObjectItem(asset_root, "objects");
                if (objects && cJSON_IsObject(objects)) {
                    // Create assets/objects directory
                    char assets_dir[MAX_PATH_LEN];
                    safe_str_cpy(assets_dir, sizeof(assets_dir), mc_path);
                    safe_str_cat(assets_dir, sizeof(assets_dir), PATH_SEP_STR "assets" PATH_SEP_STR "objects");
#ifdef PLATFORM_WINAPI
                    CreateDirectoryA(assets_dir, NULL);
#else
                    mkdir(assets_dir, 0755);
#endif

                    cJSON* obj = objects->child;
                    while (obj) {
                        if (cJSON_IsObject(obj)) {
                            cJSON* hash_item = cJSON_GetObjectItem(obj, "hash");
                            if (hash_item && cJSON_IsString(hash_item)) {
                                const char* hash = hash_item->valuestring;
                                // Asset file path: assets/objects/<first 2 chars of hash>/<full hash>
                                char asset_file_path[MAX_PATH_LEN];
                                safe_str_cpy(asset_file_path, sizeof(asset_file_path), mc_path);
                                safe_str_cat(asset_file_path, sizeof(asset_file_path), PATH_SEP_STR "assets" PATH_SEP_STR "objects" PATH_SEP_STR);
                                
                                char hash_prefix[3] = {0};
                                if (strlen(hash) >= 2) {
                                    hash_prefix[0] = hash[0];
                                    hash_prefix[1] = hash[1];
                                }
                                safe_str_cat(asset_file_path, sizeof(asset_file_path), hash_prefix);
                                safe_str_cat(asset_file_path, sizeof(asset_file_path), PATH_SEP_STR);
                                safe_str_cat(asset_file_path, sizeof(asset_file_path), hash);

                                if (!file_exists(asset_file_path)) {
                                    create_parent_dirs(asset_file_path);
                                    char asset_url[512];
                                    safe_str_cpy(asset_url, sizeof(asset_url), "https://resources.download.minecraft.net/");
                                    safe_str_cat(asset_url, sizeof(asset_url), hash_prefix);
                                    safe_str_cat(asset_url, sizeof(asset_url), "/");
                                    safe_str_cat(asset_url, sizeof(asset_url), hash);

                                    print("Downloading asset: "); print(asset_url); print("\n");
                                    if (http_get_file(asset_url, asset_file_path)) {
                                        downloaded++;
                                    }
                                }
                            }
                        }
                        obj = obj->next;
                    }
                }
                cJSON_Delete(asset_root);
            }
        }
    }

    // If this version inherits from a parent (Fabric/Forge), ensure parent's client JAR exists
    const char* parent_ver = NULL;
    if (str_len(info.inheritsFrom) > 0) parent_ver = info.inheritsFrom;
    else if (str_len(info.clientVersion) > 0) parent_ver = info.clientVersion;
    
    if (parent_ver) {
        char parent_jar[MAX_PATH_LEN];
        build_mc_path(parent_jar, sizeof(parent_jar), parent_ver, "versions", ".jar");

        if (!file_exists(parent_jar)) {
            print("=== Downloading Parent JAR ===\n");
            print("  Parent version: "); print(parent_ver); print("\n");
            print("  Target: "); print(parent_jar); print("\n");
            // Download parent version JSON first
            download_version_json(parent_ver);

            // Parse parent version to get client_url
            VersionInfo parent;
            version_info_init(&parent);
            if (parse_version_json(parent_ver, &parent)) {
                create_parent_dirs(parent_jar);
                int parent_downloaded = 0;

                if (str_len(parent.client_url) > 0) {
                    print("  Source: "); print(parent.client_url); print("\n");
                    if (http_get_file(parent.client_url, parent_jar)) {
                        parent_downloaded = 1;
                    }
                }

                if (!parent_downloaded) {
                    char parent_jar_url[MAX_PATH_LEN + 128];
                    safe_str_cpy(parent_jar_url, sizeof(parent_jar_url), LIBRARIES_URL);
                    safe_str_cat(parent_jar_url, sizeof(parent_jar_url), "/net/minecraft/client/");
                    safe_str_cat(parent_jar_url, sizeof(parent_jar_url), parent_ver);
                    safe_str_cat(parent_jar_url, sizeof(parent_jar_url), "/client-");
                    safe_str_cat(parent_jar_url, sizeof(parent_jar_url), parent_ver);
                    safe_str_cat(parent_jar_url, sizeof(parent_jar_url), ".jar");

                    print("  Source (legacy): "); print(parent_jar_url); print("\n");
                    if (http_get_file(parent_jar_url, parent_jar)) {
                        parent_downloaded = 1;
                    }
                }

                if (parent_downloaded) downloaded++;
                version_info_free(&parent);
            }
        }
    }

    version_info_free(&info);
    print("=== Verification Summary ===\n");
    print("  Verified OK: "); print_int(verified); print("\n");
    print("  Size mismatch: "); print_int(size_mismatch); print("\n");
    print("  Downloaded: "); print_int(downloaded); print("\n");
    return 1;
}

// ==================== 构建启动命令 ====================
int build_command(const char* version_id, const AccountInfo* acc, char* cmd_buf, size_t cmd_size) {
    VersionInfo info;
    if (!resolve_version(version_id, &info)) {
        print("Failed to parse version info\n");
        return 0;
    }

    if (strlen(info.id) == 0) safe_str_cpy(info.id, sizeof(info.id), version_id);

    int java_idx = -1;
    if (str_len(custom_java_path) > 0) {
        // Use custom java path directly
        for (int i = 0; i < java_count; i++) {
            if (str_cmp(java_list[i].path, custom_java_path) == 0) { java_idx = i; break; }
        }
        if (java_idx == -1) {
            // Add custom java to list
            if (java_count < MAX_JAVA) {
                safe_str_cpy(java_list[java_count].path, sizeof(java_list[java_count].path), custom_java_path);
                java_list[java_count].major = info.java_major;
                java_list[java_count].valid = 1;
                java_idx = java_count;
                java_count++;
            }
        }
    } else {
        java_idx = select_java_by_major(info.java_major);
    }
    if (java_idx == -1) {
        print("No suitable Java found for required major version ");
        print_int(info.java_major);
        print("\n");
        version_info_free(&info);
        return 0;
    }

    if (info.native_count > 0 && !extract_natives(&info))
        print("Warning: Failed to extract some natives\n");

    char classpath[MAX_CLASSPATH];
    if (!build_classpath(&info, classpath, sizeof(classpath))) {
        print("Failed to build classpath\n");
        version_info_free(&info);
        return 0;
    }

    print("=== Classpath ===\n");
    print("  Classpath length: "); print_int((int)strlen(classpath)); print("\n");
    // Print each library in classpath
    char* cp_copy = classpath;
    int lib_idx = 0;
    while (*cp_copy) {
        if (lib_idx == 0) print("  [CP] ");
        else if (lib_idx % 5 == 0) print("\n  [CP] ");
        // Find next semicolon or end
        char* semi = strchr(cp_copy, ';');
        if (semi) {
            char lib_name[256] = {0};
            int len = (int)(semi - cp_copy);
            if (len > 255) len = 255;
            strncpy(lib_name, cp_copy, len);
            // Just print the filename part (after last / or \)
            char* last_sep = strrchr(lib_name, '/');
            if (!last_sep) last_sep = strrchr(lib_name, PATH_SEP);
            if (last_sep) print(last_sep + 1);
            else print(lib_name);
            print(";");
            cp_copy = semi + 1;
        } else {
            char* last_sep = strrchr(cp_copy, '/');
            if (!last_sep) last_sep = strrchr(cp_copy, PATH_SEP);
            if (last_sep) print(last_sep + 1);
            else print(cp_copy);
            print("\n");
            break;
        }
        lib_idx++;
    }

    // Add authlib-injector to classpath if specified
    if (str_len(custom_authlib_path) > 0) {
        size_t cp_len = strlen(classpath);
        size_t auth_len = strlen(custom_authlib_path);
        if (cp_len + auth_len + 2 < MAX_CLASSPATH) {
            classpath[cp_len] = CP_SEP;
            memcpy(classpath + cp_len + 1, custom_authlib_path, auth_len);
            classpath[cp_len + 1 + auth_len] = '\0';
        }
    }

    char asset_index[64];
    if (strlen(info.assetIndex) > 0) safe_str_cpy(asset_index, sizeof(asset_index), info.assetIndex);
    else if (strlen(info.assets) > 0) safe_str_cpy(asset_index, sizeof(asset_index), info.assets);
    else safe_str_cpy(asset_index, sizeof(asset_index), version_id);

    char launcher_name[] = "TinyMC", launcher_version[] = "260809-1", clientid[] = "", auth_xuid[] = "";
    char auth_player_name[128]; safe_str_cpy(auth_player_name, sizeof(auth_player_name), acc->username);
    char version_name[64]; safe_str_cpy(version_name, sizeof(version_name), version_id);
    char game_directory[MAX_PATH_LEN]; safe_str_cpy(game_directory, sizeof(game_directory), mc_path);
    char game_assets[MAX_PATH_LEN]; safe_str_cpy(game_assets, sizeof(game_assets), mc_path); safe_str_cat(game_assets, sizeof(game_assets), PATH_SEP_STR "assets");
    char assets_root[MAX_PATH_LEN]; safe_str_cpy(assets_root, sizeof(assets_root), mc_path); safe_str_cat(assets_root, sizeof(assets_root), PATH_SEP_STR "assets");
    char auth_uuid[64]; safe_str_cpy(auth_uuid, sizeof(auth_uuid), acc->uuid);
    char auth_access_token[2048]; safe_str_cpy(auth_access_token, sizeof(auth_access_token), acc->accessToken);
    if (auth_access_token[0] == '\0') safe_str_cpy(auth_access_token, sizeof(auth_access_token), "0");
    char user_type[16];
    if (strcmp(acc->type, "official") == 0) safe_str_cpy(user_type, sizeof(user_type), "msa");
    else safe_str_cpy(user_type, sizeof(user_type), (strcmp(acc->type, "external") == 0) ? "mojang" : "legacy");
    char version_type[] = "release";
    char natives_directory[MAX_PATH_LEN]; build_mc_path(natives_directory, sizeof(natives_directory), version_id, "versions", "-natives");
    char library_directory[MAX_PATH_LEN]; safe_str_cpy(library_directory, sizeof(library_directory), mc_path); safe_str_cat(library_directory, sizeof(library_directory), PATH_SEP_STR "libraries");
    char classpath_separator[] = ";";

    char* p = cmd_buf;
    size_t remaining = cmd_size;

    char java_exe[MAX_PATH_LEN];
    if (str_len(java_list[java_idx].path) == 0) {
        print("[ERROR] Java path is empty for index ");
        print_int(java_idx);
        print("\n");
        version_info_free(&info);
        return 0;
    }
    safe_str_cpy(java_exe, sizeof(java_exe), java_list[java_idx].path);
    safe_str_cat(java_exe, sizeof(java_exe), PATH_SEP_STR "bin" PATH_SEP_STR);
    safe_str_cat(java_exe, sizeof(java_exe), use_java_exe ? "java.exe" : "javaw.exe");
    int written = snprintf(p, remaining, "\"%s\" ", java_exe);
    if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
    p += written; remaining -= written;

    if (strlen(jvm_args) == 0) safe_str_cpy(jvm_args, sizeof(jvm_args), "-Xmx2G -Xms512M");
    written = snprintf(p, remaining, "%s ", jvm_args);
    if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
    p += written; remaining -= written;

    // Add authlib-injector javaagent if specified
    if (str_len(custom_authlib_path) > 0 && str_len(acc->server) > 0) {
        written = snprintf(p, remaining, "-javaagent:\"%s\"=%s ", custom_authlib_path, acc->server);
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
    }

    // LWJGL 3.x 需要 org.lwjgl.librarypath，旧版使用 java.library.path
    // 添加额外的 natives 相关参数（参考 PCL 脚本）
    written = snprintf(p, remaining, "-Djava.library.path=\"%s\" -Dorg.lwjgl.librarypath=\"%s\" -Djna.tmpdir=\"%s\" -Dorg.lwjgl.system.SharedLibraryExtractPath=\"%s\" -Dio.netty.native.workdir=\"%s\" ", natives_directory, natives_directory, natives_directory, natives_directory, natives_directory);
    if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
    p += written; remaining -= written;

    char user_properties[] = "{}";

    print("  JVM args from version: "); print_int(info.jvm_arg_count); print("\n");
    for (int i = 0; i < info.jvm_arg_count; i++) {
        print("    ["); print_int(i); print("] "); print(info.jvm_args[i]); print("\n");
    }

    for (int i = 0; i < info.jvm_arg_count; i++) {
        // 跳过 -cp 和 ${classpath}（我们会单独添加）
        if (strcmp(info.jvm_args[i], "-cp") == 0 || 
            strstr(info.jvm_args[i], "${classpath}") != NULL) {
            continue;
        }
        
        char arg[4096];
        safe_str_cpy(arg, sizeof(arg), info.jvm_args[i]);
        
        // 跳过我们已经手动添加的参数（避免重复）
        if (strstr(arg, "-Djava.library.path=") || strstr(arg, "-Dorg.lwjgl.librarypath=") || 
            strstr(arg, "-Djna.tmpdir=") || strstr(arg, "-Dorg.lwjgl.system.SharedLibraryExtractPath=") ||
            strstr(arg, "-Dio.netty.native.workdir=")) {
            continue;
        }
        
        replace_all_placeholders(arg, sizeof(arg),
            classpath, asset_index,
            launcher_name, launcher_version,
            clientid, auth_xuid,
            auth_player_name, version_name,
            game_directory, game_assets, assets_root,
            auth_uuid, auth_access_token, user_type, version_type,
            natives_directory, library_directory, classpath_separator, user_properties);
        
        // 如果参数包含空格，需要加引号（例如 -DFabricMcEmu= net.minecraft.client.main.Main ）
        if (strchr(arg, ' ') != NULL) {
            written = snprintf(p, remaining, "\"%s\" ", arg);
        } else {
            written = snprintf(p, remaining, "%s ", arg);
        }
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
    }

    /* Forge/NeoForge 模块系统修复：检测是否使用了 --add-modules */
    for (int i = 0; i < info.jvm_arg_count; i++) {
        if (strstr(info.jvm_args[i], "--add-modules")) {
            /* 添加通用模块开放参数，修复 minecraft 模块冲突 */
            const char* forge_opens[] = {
                "--add-opens", "java.base/java.lang.invoke=ALL-UNNAMED",
                "--add-opens", "java.base/java.lang=ALL-UNNAMED",
                "--add-opens", "java.base/java.lang.reflect=ALL-UNNAMED",
                "--add-opens", "java.base/java.util=ALL-UNNAMED"
            };
            for (int k = 0; k < 8; k++) {
                written = snprintf(p, remaining, "%s ", forge_opens[k]);
                if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
                p += written; remaining -= written;
            }
            break;
        }
    }

    /* 自定义窗口标题 - 作为系统属性传给游戏 */
    if (str_len(window_title) > 0) {
        written = snprintf(p, remaining, "-Dtinycraft.title=\"%s\" ", window_title);
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
    }

    written = snprintf(p, remaining, "-cp \"%s\" ", classpath);
    if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
    p += written; remaining -= written;

    if (strlen(info.mainClass) == 0) safe_str_cpy(info.mainClass, sizeof(info.mainClass), "net.minecraft.client.main.Main");
    written = snprintf(p, remaining, "%s ", info.mainClass);
    if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
    p += written; remaining -= written;

    if (info.game_arg_count > 0) {
        for (int i = 0; i < info.game_arg_count; i++) {
            char arg[4096];
            safe_str_cpy(arg, sizeof(arg), info.game_args[i]);
            replace_all_placeholders(arg, sizeof(arg),
                classpath, asset_index,
                launcher_name, launcher_version,
                clientid, auth_xuid,
                auth_player_name, version_name,
                game_directory, game_assets, assets_root,
                auth_uuid, auth_access_token, user_type, version_type,
                natives_directory, library_directory, classpath_separator, user_properties);
            // 含空格的参数需要加引号（例如版本名 "Test Client For LavaArcade"）
            if (strchr(arg, ' ') != NULL) {
                written = snprintf(p, remaining, "\"%s\" ", arg);
            } else {
                written = snprintf(p, remaining, "%s ", arg);
            }
            if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
            p += written; remaining -= written;
        }
    } else if (strlen(info.minecraftArguments) > 0) {
        // 使用 JSON 中的 minecraftArguments (1.12 及以下)
        char mc_args[2048];
        safe_str_cpy(mc_args, sizeof(mc_args), info.minecraftArguments);
        replace_all_placeholders(mc_args, sizeof(mc_args),
            classpath, asset_index,
            launcher_name, launcher_version,
            clientid, auth_xuid,
            auth_player_name, version_name,
            game_directory, game_assets, assets_root,
            auth_uuid, auth_access_token, user_type, version_type,
            natives_directory, library_directory, classpath_separator, user_properties);
        written = snprintf(p, remaining, "%s ", mc_args);
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
    } else {
        // 最终 fallback：硬编码基本参数
        written = snprintf(p, remaining,
            "--username \"%s\" --version \"%s\" --gameDir \"%s\" --assetsDir \"%s" PATH_SEP_STR "assets\" --assetIndex %s ",
            acc->username, version_id, mc_path, mc_path, asset_index);
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
        if (strlen(acc->accessToken) > 0) {
            written = snprintf(p, remaining, "--accessToken \"%s\" ", acc->accessToken);
            if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
            p += written; remaining -= written;
        }
        if (strlen(acc->uuid) > 0) {
            written = snprintf(p, remaining, "--uuid \"%s\" ", acc->uuid);
            if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
            p += written; remaining -= written;
        }
        written = snprintf(p, remaining, "--userType %s --userProperties %s ", user_type, user_properties);
        if (written < 0 || (size_t)written >= remaining) { version_info_free(&info); return 0; }
        p += written; remaining -= written;
    }

    version_info_free(&info);
    return 1;
}

// ==================== 启动游戏 ====================
#ifdef PLATFORM_WINAPI
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        if (game_process_handle != NULL && game_pid != 0) {
            print("\nTerminating Minecraft (PID: ");
            char pid[16]; int_to_str(game_pid, pid, sizeof(pid));
            print(pid); print(")...\n");
            // Terminate the game process tree
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32 pe = { sizeof(pe) };
                if (Process32First(hSnapshot, &pe)) {
                    do {
                        if (pe.th32ParentProcessID == game_pid) {
                            HANDLE hChild = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (hChild) { TerminateProcess(hChild, 1); CloseHandle(hChild); }
                        }
                    } while (Process32Next(hSnapshot, &pe));
                }
                CloseHandle(hSnapshot);
            }
            TerminateProcess(game_process_handle, 1);
            CloseHandle(game_process_handle);
            game_process_handle = NULL;
            game_pid = 0;
            print("Game terminated.\n");
        }
        ExitProcess(0);
        return TRUE;
    }
    return FALSE;
}

void start_game(const char* version_id, const AccountInfo* acc, int skip_verify) {
    print("=== Launch Sequence ===\n");
    print("Version: "); print(version_id); print("\n");
    print("Account: "); print(acc->username); print(" ("); print(acc->type); print(")\n");
    print("MC Path: "); print(mc_path); print("\n");

    /* 自定义窗口标题 */
    if (str_len(window_title) > 0) {
        SetConsoleTitleA(window_title);
        print("Window title set: "); print(window_title); print("\n");
    }

    if (skip_verify) {
        print("Skipping file verification (no_verify flag set)\n");
    } else {
        if (!verify_and_download_files(version_id)) {
            print("Failed to verify/download game files, cannot launch\n");
            return;
        }
    }

    print("Starting game...\n");

    char cmd[32768];
    if (!build_command(version_id, acc, cmd, sizeof(cmd))) {
        print("Failed to build launch command\n");
        print("Troubleshooting:\n");
        print("  1. Check if Java is available: mc -j -list\n");
        print("  2. Check if version exists: mc -lv\n");
        print("  3. Check if game files are complete: mc -start -ver "); print(version_id); print(" -no_verify\n");
        return;
    }
    print("=== Final Command ===\n");
    print("  Command length: "); print_int((int)strlen(cmd)); print("\n");
    print("  Command: "); print(cmd); print("\n\n");
    print("=== Starting Game Process ===\n");
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};

    // 重定向子进程输出
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa) };
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hChildStdOutRead, hChildStdOutWrite;
        HANDLE hChildStdErrRead, hChildStdErrWrite;

        if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &sa, 0)) {
            print("Failed to create stdout pipe\n");
            return;
        }
        if (!CreatePipe(&hChildStdErrRead, &hChildStdErrWrite, &sa, 0)) {
            print("Failed to create stderr pipe\n");
            CloseHandle(hChildStdOutRead);
            CloseHandle(hChildStdOutWrite);
            return;
        }

        // 确保子进程继承写入端（不是读取端！）
        SetHandleInformation(hChildStdOutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hChildStdErrWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hChildStdOutWrite;
        si.hStdError = hChildStdErrWrite;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.wShowWindow = SW_SHOW;

        if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, mc_path, &si, &pi)) {
            // 关闭写入端（父进程不需要）
            CloseHandle(hChildStdOutWrite);
            CloseHandle(hChildStdErrWrite);

            print("Minecraft started successfully! PID: ");
            char pid[16];
            int_to_str(pi.dwProcessId, pid, sizeof(pid));
            print(pid);
            print("\n");
            print("=== Game Output ===\n");

            game_pid = pi.dwProcessId;
            game_process_handle = pi.hProcess;
            CloseHandle(pi.hThread);

            // 等待初始进程退出（Forge/Fabric launcher 会快速退出）
            // 但对于原版，这就是游戏进程本身，不能关闭句柄
            DWORD waitResult = WaitForSingleObject(pi.hProcess, 3000);
            int initialProcessExited = (waitResult == WAIT_OBJECT_0);

            // 查找子进程（实际游戏进程，仅 Forge/Fabric 会有）
            Sleep(2000);
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            DWORD childPid = 0;
            HANDLE hChildProcess = NULL;
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32 pe = { sizeof(pe) };
                if (Process32First(hSnapshot, &pe)) {
                    do {
                        if (pe.th32ParentProcessID == game_pid) {
                            childPid = pe.th32ProcessID;
                            hChildProcess = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (hChildProcess) {
                                break;
                            }
                        }
                    } while (Process32Next(hSnapshot, &pe));
                }
                CloseHandle(hSnapshot);
            }

            // 如果有子进程，关闭初始进程句柄，使用子进程句柄
            // 如果没有子进程（原版），继续使用初始进程句柄
            HANDLE hMonitor = NULL;
            if (hChildProcess) {
                // Forge/Fabric：关闭初始进程，使用子进程
                CloseHandle(pi.hProcess);
                game_pid = childPid;
                game_process_handle = hChildProcess;
                hMonitor = hChildProcess;
            } else if (initialProcessExited) {
                // 原版但进程已退出：无法监控
                print("[WARN] Game process exited immediately!\n");
                CloseHandle(pi.hProcess);
                game_process_handle = NULL;
                print("\n=== End Game Output ===\n");
                print("Game exited with code: 1\n");
                game_pid = 0;
                return;
            } else {
                // 原版且进程仍在运行：继续使用初始进程
                hMonitor = pi.hProcess;
                game_process_handle = pi.hProcess;
            }

            // 读取子进程输出
            char readBuf[4096];
            DWORD bytesRead = 0;

            while (1) {
                // 读取标准输出
                while (PeekNamedPipe(hChildStdOutRead, NULL, 0, NULL, &bytesRead, NULL) && bytesRead > 0) {
                    if (ReadFile(hChildStdOutRead, readBuf, min(sizeof(readBuf) - 1, bytesRead), &bytesRead, NULL)) {
                        readBuf[bytesRead] = '\0';
                        print(readBuf);
                    } else {
                        break;
                    }
                }

                // 读取标准错误
                while (PeekNamedPipe(hChildStdErrRead, NULL, 0, NULL, &bytesRead, NULL) && bytesRead > 0) {
                    if (ReadFile(hChildStdErrRead, readBuf, min(sizeof(readBuf) - 1, bytesRead), &bytesRead, NULL)) {
                        readBuf[bytesRead] = '\0';
                        print("[ERR] "); print(readBuf);
                    } else {
                        break;
                    }
                }

                // 检查进程是否退出
                if (WaitForSingleObject(hMonitor, 1000) == WAIT_OBJECT_0) {
                    // 最后读取剩余输出
                    while (ReadFile(hChildStdOutRead, readBuf, sizeof(readBuf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                        readBuf[bytesRead] = '\0';
                        print(readBuf);
                    }
                    while (ReadFile(hChildStdErrRead, readBuf, sizeof(readBuf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                        readBuf[bytesRead] = '\0';
                        print("[ERR] "); print(readBuf);
                    }
                    break;
                }
            }

            CloseHandle(hChildStdOutRead);
            CloseHandle(hChildStdErrRead);
            if (hChildProcess) CloseHandle(hChildProcess);

            DWORD exitCode;
            GetExitCodeProcess(hMonitor, &exitCode);
            print("\n=== End Game Output ===\n");
            print("Game exited with code: ");
            char code[16]; int_to_str(exitCode, code, sizeof(code));
            print(code); print("\n");

            game_process_handle = NULL;
            game_pid = 0;
        } else {
            CloseHandle(hChildStdOutRead);
            CloseHandle(hChildStdOutWrite);
            CloseHandle(hChildStdErrRead);
            CloseHandle(hChildStdErrWrite);
            print("Failed to start Minecraft! Error: ");
            char err[16];
            int_to_str(GetLastError(), err, sizeof(err));
            print(err);
            print("\n");
        }
    }
}
#else
/* ---- POSIX: 启动游戏 ---- */
void start_game(const char* version_id, const AccountInfo* acc, int skip_verify) {
    print("=== Launch Sequence ===\n");
    print("Version: "); print(version_id); print("\n");
    print("Account: "); print(acc->username); print(" ("); print(acc->type); print(")\n");
    print("MC Path: "); print(mc_path); print("\n");

    /* 自定义窗口标题（终端标题，尽力而为） */
    if (str_len(window_title) > 0) {
        printf("\033]0;%s\007", window_title);
        print("Window title set: "); print(window_title); print("\n");
    }

    if (skip_verify) {
        print("Skipping file verification (no_verify flag set)\n");
    } else {
        if (!verify_and_download_files(version_id)) {
            print("Failed to verify/download game files, cannot launch\n");
            return;
        }
    }

    print("Starting game...\n");

    char cmd[32768];
    if (!build_command(version_id, acc, cmd, sizeof(cmd))) {
        print("Failed to build launch command\n");
        return;
    }
    print("=== Final Command ===\n");
    print("  Command length: "); print_int((int)strlen(cmd)); print("\n");
    print("  Command: "); print(cmd); print("\n\n");
    print("=== Starting Game Process ===\n");

    int outpipe[2], errpipe[2];
    if (pipe(outpipe) != 0 || pipe(errpipe) != 0) {
        print("Failed to create pipes\n");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) { print("Failed to fork\n"); return; }
    if (pid == 0) {
        /* 子进程：重定向输出并启动游戏 */
        dup2(outpipe[1], 1);
        dup2(errpipe[1], 2);
        close(outpipe[0]); close(outpipe[1]);
        close(errpipe[0]); close(errpipe[1]);
        if (mc_path[0]) chdir(mc_path);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }
    close(outpipe[1]); close(errpipe[1]);

    game_pid = (int)pid;
    game_process_handle = (int)pid;
    print("Minecraft started successfully! PID: ");
    char pidstr[16]; int_to_str((int)pid, pidstr, sizeof(pidstr));
    print(pidstr); print("\n");
    print("=== Game Output ===\n");

    char readBuf[4096];
    int alive = 1;
    while (alive) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || (r < 0)) alive = 0;
        /* 读取输出 */
        ssize_t n;
        while ((n = read(outpipe[0], readBuf, sizeof(readBuf)-1)) > 0) {
            readBuf[n] = '\0'; print(readBuf);
        }
        while ((n = read(errpipe[0], readBuf, sizeof(readBuf)-1)) > 0) {
            readBuf[n] = '\0'; print("[ERR] "); print(readBuf);
        }
        if (alive) usleep(50000);
    }
    close(outpipe[0]); close(errpipe[0]);
    print("\n=== End Game Output ===\n");
    print("Game exited.\n");
    game_process_handle = -1;
    game_pid = 0;
}
#endif

// ==================== 账号管理 ====================
void save_account(AccountInfo* acc) {
    int idx = -1;
    for (int i = 0; i < account_count; i++) {
        if (str_cmp(accounts[i].username, acc->username) == 0 && str_cmp(accounts[i].type, acc->type) == 0) {
            idx = i; break;
        }
    }
    if (idx == -1) {
        if (account_count >= MAX_ACCOUNTS) { print("Account list full!\n"); return; }
        idx = account_count++;
    }
    safe_str_cpy(accounts[idx].username, sizeof(accounts[idx].username), acc->username);
    safe_str_cpy(accounts[idx].email, sizeof(accounts[idx].email), acc->email);
    safe_str_cpy(accounts[idx].type, sizeof(accounts[idx].type), acc->type);
    safe_str_cpy(accounts[idx].server, sizeof(accounts[idx].server), acc->server);
    safe_str_cpy(accounts[idx].password, sizeof(accounts[idx].password), acc->password);
    safe_str_cpy(accounts[idx].accessToken, sizeof(accounts[idx].accessToken), acc->accessToken);
    safe_str_cpy(accounts[idx].uuid, sizeof(accounts[idx].uuid), acc->uuid);
    accounts[idx].is_default = acc->is_default;
    accounts[idx].player_id_count = acc->player_id_count;
    for (int i = 0; i < acc->player_id_count && i < MAX_PLAYER_IDS; i++)
        safe_str_cpy(accounts[idx].player_ids[i], 64, acc->player_ids[i]);
    safe_str_cpy(accounts[idx].custom_params, sizeof(accounts[idx].custom_params), acc->custom_params);

    char key[32];
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_USER"); write_config(key, accounts[idx].username);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_EMAIL"); write_config(key, accounts[idx].email);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_TYPE"); write_config(key, accounts[idx].type);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_SERVER"); write_config(key, accounts[idx].server);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PASSWORD"); write_config(key, accounts[idx].password);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_TOKEN"); write_config(key, accounts[idx].accessToken);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_UUID"); write_config(key, accounts[idx].uuid);
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_DEFAULT"); write_config(key, acc->is_default ? "1" : "0");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_COUNT");
    char pc[4]; int_to_str(acc->player_id_count, pc, sizeof(pc)); write_config(key, pc);
    for (int i = 0; i < acc->player_id_count && i < MAX_PLAYER_IDS; i++) {
        make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_");
        char pid[4]; int_to_str(i, pid, sizeof(pid)); str_cat(key, pid);
        write_config(key, accounts[idx].player_ids[i]);
    }
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PARAMS"); write_config(key, accounts[idx].custom_params);
    print("Account saved!\n");
}

int select_account_interactive(const char* type_hint) {
    if (account_count == 0) { print("No accounts found! Please login first.\n"); return -1; }
    int match_count = 0, match_idx[MAX_ACCOUNTS];
    for (int i = 0; i < account_count; i++) {
        if (str_len(type_hint) == 0 || str_cmp(accounts[i].type, type_hint) == 0)
            match_idx[match_count++] = i;
    }
    if (match_count == 0) { print("No "); print(type_hint); print(" accounts found!\n"); return -1; }
    if (match_count == 1) return match_idx[0];
    print("=== Select Account ===\n");
    for (int i = 0; i < match_count; i++) {
        print("  ["); print_int(i + 1); print("] ");
        print(accounts[match_idx[i]].username); print(" | "); print(accounts[match_idx[i]].type);
        if (accounts[match_idx[i]].is_default) print(" [DEFAULT]");
        print("\n");
    }
    print("Select (1-"); print_int(match_count); print("): ");
    char input[4] = {0};
    console_read_line(input, sizeof(input));
    int sel = str_to_int(input);
    if (sel > 0 && sel <= match_count) return match_idx[sel - 1];
    print("Invalid selection, using default.\n");
    for (int i = 0; i < match_count; i++) if (accounts[match_idx[i]].is_default) return match_idx[i];
    return match_idx[0];
}

void list_accounts() {
    print("=== Accounts ===\n");
    if (account_count == 0) { print("No accounts found!\n"); print("Use 'mc -u -l' to login first.\n"); return; }
    for (int i = 0; i < account_count; i++) {
        print("  ["); print_int(i + 1); print("] ");
        print(accounts[i].username); print(" | "); print(accounts[i].type); print(" | "); print(accounts[i].email);
        if (accounts[i].is_default) print(" [DEFAULT]");
        print("\n");
        if (accounts[i].player_id_count > 0) {
            print("     Player IDs: ");
            for (int j = 0; j < accounts[i].player_id_count; j++) {
                print(accounts[i].player_ids[j]);
                if (j < accounts[i].player_id_count - 1) print(", ");
            }
            print("\n");
        }
    }
}

void delete_account(int account_index) {
    if (account_index < 1 || account_index > account_count) {
        print("Error: Invalid account index! Must be between 1 and ");
        print_int(account_count);
        print("\n");
        return;
    }
    int idx = account_index - 1;
    print("Deleting account: ");
    print(accounts[idx].username);
    print(" (");
    print(accounts[idx].type);
    print(")\n");

    char key[32];
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_USER"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_EMAIL"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_TYPE"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_SERVER"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PASSWORD"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_TOKEN"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_UUID"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_DEFAULT"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_COUNT"); write_config(key, "");
    make_key(key, "ACCOUNT_", idx); str_cat(key, "_PARAMS"); write_config(key, "");
    for (int i = 0; i < MAX_PLAYER_IDS; i++) {
        make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_");
        char pid[4]; int_to_str(i, pid, sizeof(pid)); str_cat(key, pid);
        write_config(key, "");
    }

    for (int i = idx; i < account_count - 1; i++) {
        accounts[i] = accounts[i + 1];
    }
    account_count--;

    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    WritePrivateProfileStringA("config", "DEFAULT_ACCOUNT", "", path);

    print("Account deleted!\n");
}

void relogin_account(int account_index) {
    if (account_index < 1 || account_index > account_count) {
        print("Error: Invalid account index! Must be between 1 and ");
        print_int(account_count);
        print("\n");
        return;
    }
    int idx = account_index - 1;
    AccountInfo* acc = &accounts[idx];

    if (str_cmp(acc->type, "offline") == 0) {
        print("Offline account does not need relogin.\n");
        return;
    } else if (str_cmp(acc->type, "external") == 0) {
        if (str_len(acc->server) == 0 || str_len(acc->email) == 0 || str_len(acc->password) == 0) {
            print("Error: Missing account credentials for relogin!\n");
            return;
        }
        print("Relogin external account: ");
        print(acc->email);
        print(" at ");
        print(acc->server);
        print("\n");

        char username[64] = {0}, uuid[64] = {0}, token[128] = {0};
        if (yggdrasil_authenticate(acc->server, acc->email, acc->password, username, uuid, token)) {
            safe_str_cpy(acc->username, sizeof(acc->username), username);
            safe_str_cpy(acc->uuid, sizeof(acc->uuid), uuid);
            safe_str_cpy(acc->accessToken, sizeof(acc->accessToken), token);
            acc->player_id_count = 1;
            safe_str_cpy(acc->player_ids[0], 64, username);

            char key[32];
            make_key(key, "ACCOUNT_", idx); str_cat(key, "_USER"); write_config(key, acc->username);
            make_key(key, "ACCOUNT_", idx); str_cat(key, "_TOKEN"); write_config(key, acc->accessToken);
            make_key(key, "ACCOUNT_", idx); str_cat(key, "_UUID"); write_config(key, acc->uuid);
            make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_COUNT");
            char pc[4]; int_to_str(1, pc, sizeof(pc)); write_config(key, pc);
            make_key(key, "ACCOUNT_", idx); str_cat(key, "_PID_0"); write_config(key, username);

            print("Relogin successful! Username: ");
            print(acc->username);
            print("\n");
        } else {
            print("Relogin failed.\n");
        }
    } else if (str_cmp(acc->type, "official") == 0) {
        print("Official login relogin not implemented yet.\n");
    } else {
        print("Unknown account type.\n");
    }
}

// ==================== Microsoft OAuth2 Login ====================
// ╔═════════════════════════════════════════════════
// ╚══════════════════════════════════════════════════════════════╝
#define MSA_CLIENT_ID  "114514"
#define MSA_DEVICE_URL "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"
#define MSA_TOKEN_URL  "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
#define XBL_AUTH_URL   "https://user.auth.xboxlive.com/user/authenticate"
#define XSTS_AUTH_URL  "https://xsts.auth.xboxlive.com/xsts/authorize"
#define MC_LOGIN_URL   "https://api.minecraftservices.com/authentication/login_with_xbox"
#define MC_PROFILE_URL "https://api.minecraftservices.com/minecraft/profile"

/* minimal URL percent-encode (writes into out, returns out length) */
static int url_encode(const char* src, char* out, size_t out_size) {
    const char hex[] = "0123456789ABCDEF";
    size_t len = 0;
    for (const char* s = src; *s && len + 3 < out_size; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
             c == '-' || c == '_' || c == '.' || c == '~') {
            out[len++] = c;
        } else {
            out[len++] = '%';
            out[len++] = hex[c >> 4];
            out[len++] = hex[c & 15];
        }
    }
    out[len] = '\0';
    return (int)len;
}

/* HTTP POST with application/x-www-form-urlencoded body */
#ifdef PLATFORM_WINAPI
static char* http_post_urlencoded(const char* url, const char* body) {
    UrlInfo info;
    if (!parse_url(url, &info)) return NULL;
    HINTERNET hSession = WinHttpOpen(L"TinyMC/v260809-1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;
    HINTERNET hConnect = WinHttpConnect(hSession, info.host, info.port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", info.path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            info.use_tls ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    if (info.use_tls) set_tls_options(hRequest);
    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    DWORD body_len = (DWORD)strlen(body);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body, body_len, body_len, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }
    char* resp = read_response(hRequest);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return resp;
}
#else
static char* http_post_urlencoded(const char* url, const char* body) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    struct { char* data; size_t size; } chunk = {0};
    chunk.data = malloc(1);
    if (!chunk.data) { curl_easy_cleanup(curl); return NULL; }
    chunk.data[0] = '\0';
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(chunk.data); return NULL; }
    return chunk.data;
}
#endif

/* HTTP GET with Authorization: Bearer header */
#ifdef PLATFORM_WINAPI
static char* http_get_auth(const char* url, const char* token) {
    UrlInfo info;
    if (!parse_url(url, &info)) return NULL;
    HINTERNET hSession = WinHttpOpen(L"TinyMC/v260809-1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;
    HINTERNET hConnect = WinHttpConnect(hSession, info.host, info.port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", info.path, NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            info.use_tls ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    if (info.use_tls) set_tls_options(hRequest);
    wchar_t auth_hdr[256];
    swprintf(auth_hdr, 256, L"Authorization: Bearer %hs\r\n", token);
    WinHttpAddRequestHeaders(hRequest, auth_hdr, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }
    char* resp = read_response(hRequest);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return resp;
}
#else
static char* http_get_auth(const char* url, const char* token) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    struct { char* data; size_t size; } chunk = {0};
    chunk.data = malloc(1);
    if (!chunk.data) { curl_easy_cleanup(curl); return NULL; }
    chunk.data[0] = '\0';
    char auth_hdr[1024];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", token);
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, auth_hdr);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(chunk.data); return NULL; }
    return chunk.data;
}
#endif

/* helper: get JSON string field, empty if not found */
static void json_str(cJSON* obj, const char* key, char* out, size_t sz) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    safe_str_cpy(out, sz, (item && item->valuestring) ? item->valuestring : "");
}

/* Step 1: Get device code */
static int msa_get_device_code(char* out_user_code, size_t uc_size, char* out_device_code, size_t dc_size, char* out_message, size_t msg_size) {
    char body[256];
    snprintf(body, sizeof(body), "client_id=%s&scope=XboxLive.signin%%20offline_access", MSA_CLIENT_ID);
    char* resp = http_post_urlencoded(MSA_DEVICE_URL, body);
    if (!resp) { safe_str_cpy(out_message, msg_size, "Network error."); return 0; }
    cJSON* root = cJSON_Parse(resp);
    if (!root) { free(resp); safe_str_cpy(out_message, msg_size, "Invalid response."); return 0; }
    json_str(root, "user_code", out_user_code, uc_size);
    json_str(root, "device_code", out_device_code, dc_size);
    json_str(root, "message", out_message, msg_size);
    cJSON_Delete(root); free(resp);
    if (out_user_code[0] == '\0' || out_device_code[0] == '\0') {
        safe_str_cpy(out_message, msg_size, "Failed to get device code.");
        return 0;
    }
    return 1;
}

/* Step 2: Poll for token (5s interval, ~2min timeout) */
static int msa_poll_token(const char* device_code, char* out_access_token, size_t at_size, char* out_refresh_token, size_t rt_size) {
    char body[512], encoded[512];
    url_encode(device_code, encoded, sizeof(encoded));
    snprintf(body, sizeof(body), "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&client_id=%s&device_code=%s",
             MSA_CLIENT_ID, encoded);
    for (int i = 0; i < 24; i++) { /* ~2 min with 5s intervals */
        Sleep(5000);
        char* resp = http_post_urlencoded(MSA_TOKEN_URL, body);
        if (!resp) continue;
        cJSON* root = cJSON_Parse(resp);
        if (!root) { free(resp); continue; }
        cJSON* err = cJSON_GetObjectItem(root, "error");
        if (err && err->valuestring) {
            /* authorization_pending = user hasn't finished; slow_down = poll too fast */
            if (strcmp(err->valuestring, "authorization_pending") == 0 ||
                strcmp(err->valuestring, "slow_down") == 0) {
                cJSON_Delete(root); free(resp);
                printf("."); /* progress dot */
                continue;
            }
            printf("[MSA] Error: %s\n", err->valuestring);
            cJSON_Delete(root); free(resp);
            return 0;
        }
        json_str(root, "access_token", out_access_token, at_size);
        json_str(root, "refresh_token", out_refresh_token, rt_size);
        cJSON_Delete(root); free(resp);
        if (out_access_token[0]) { printf(" OK\n"); return 1; }
    }
    printf("[MSA] Login timed out.\n");
    return 0;
}

/* Step 3: Xbox Live authentication */
static int msa_xbox_auth(const char* msa_token, char* out_xbl_token, size_t xbl_size, char* out_uhs, size_t uhs_size) {
    char json[3072];
    snprintf(json, sizeof(json),
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RpsTicket\":\"d=%s\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}", msa_token);
    char* resp = http_post(XBL_AUTH_URL, json);
    if (!resp) return 0;
    cJSON* root = cJSON_Parse(resp);
    if (!root) { free(resp); return 0; }
    json_str(root, "Token", out_xbl_token, xbl_size);
    cJSON* disp = cJSON_GetObjectItem(root, "DisplayClaims");
    if (disp) {
        cJSON* xui = cJSON_GetObjectItem(disp, "xui");
        if (xui && xui->child) json_str(xui->child, "uhs", out_uhs, uhs_size);
    }
    cJSON_Delete(root); free(resp);
    return (out_xbl_token[0] && out_uhs[0]) ? 1 : 0;
}

/* Step 4: XSTS authentication */
static int msa_xsts_auth(const char* xbl_token, char* out_xsts_token, size_t xsts_size, char* out_uhs, size_t uhs_size) {
    char json[3072];
    snprintf(json, sizeof(json),
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
        "\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}", xbl_token);
    char* resp = http_post(XSTS_AUTH_URL, json);
    if (!resp) return 0;
    cJSON* root = cJSON_Parse(resp);
    if (!root) { free(resp); return 0; }
    cJSON* err = cJSON_GetObjectItem(root, "XErr");
    if (err) {
        /* 2148916233 = no Xbox Live Gold / no Minecraft ownership */
        printf("[MSA] Xbox error 0x%x\n", err->valueint);
        if (err->valueint == 2148916233)
            printf("[MSA] Does this account own Minecraft?\n");
        cJSON_Delete(root); free(resp);
        return 0;
    }
    json_str(root, "Token", out_xsts_token, xsts_size);
    cJSON* disp = cJSON_GetObjectItem(root, "DisplayClaims");
    if (disp) {
        cJSON* xui = cJSON_GetObjectItem(disp, "xui");
        if (xui && xui->child) json_str(xui->child, "uhs", out_uhs, uhs_size);
    }
    cJSON_Delete(root); free(resp);
    return (out_xsts_token[0] && out_uhs[0]) ? 1 : 0;
}

/* Step 5: Minecraft authentication */
static int msa_mc_login(const char* uhs, const char* xsts_token, char* out_mc_token, size_t mc_size) {
    char json[3072];
    snprintf(json, sizeof(json),
        "{\"identityToken\":\"XBL3.0 x=%s;%s\"}", uhs, xsts_token);
    char* resp = http_post(MC_LOGIN_URL, json);
    if (!resp) return 0;
    cJSON* root = cJSON_Parse(resp);
    if (!root) { free(resp); return 0; }
    json_str(root, "access_token", out_mc_token, mc_size);
    cJSON_Delete(root); free(resp);
    return out_mc_token[0] ? 1 : 0;
}

/* Step 6: Get Minecraft profile */
static int msa_get_profile(const char* mc_token, char* out_uuid, size_t uuid_size, char* out_name, size_t name_size) {
    char* resp = http_get_auth(MC_PROFILE_URL, mc_token);
    if (!resp) return 0;
    cJSON* root = cJSON_Parse(resp);
    if (!root) { free(resp); return 0; }
    json_str(root, "id", out_uuid, uuid_size);
    json_str(root, "name", out_name, name_size);
    cJSON_Delete(root); free(resp);
    return (out_uuid[0] && out_name[0]) ? 1 : 0;
}

/* Main Microsoft OAuth2 device code flow - returns 1 on success */
static int msa_authenticate(char* out_username, char* out_uuid, char* out_accessToken) {
    printf("[MSA] Starting Microsoft OAuth2 device code flow...\n");
    char user_code[16], device_code[256], msg[256];
    if (!msa_get_device_code(user_code, sizeof(user_code), device_code, sizeof(device_code), msg, sizeof(msg))) {
        printf("[MSA] Failed to get device code: %s\n", msg); return 0;
    }
    printf("[MSA] ========================================\n");
    printf("[MSA] 1. Open: https://www.microsoft.com/link\n");
    printf("[MSA] 2. Enter code: %s\n", user_code);
    printf("[MSA] ========================================\n");
    printf("[MSA] Waiting for authorization");
    char msa_token[2048], refresh_token[1024];
    if (!msa_poll_token(device_code, msa_token, sizeof(msa_token), refresh_token, sizeof(refresh_token))) {
        printf("[MSA] Failed to get MSA token.\n"); return 0;
    }
    printf("[MSA] Authenticating with Xbox Live...\n");
    char xbl_token[2048], uhs[16];
    if (!msa_xbox_auth(msa_token, xbl_token, sizeof(xbl_token), uhs, sizeof(uhs))) {
        printf("[MSA] Xbox Live auth failed.\n"); return 0;
    }
    char xsts_token[2048];
    if (!msa_xsts_auth(xbl_token, xsts_token, sizeof(xsts_token), uhs, sizeof(uhs))) {
        printf("[MSA] XSTS auth failed.\n"); return 0;
    }
    printf("[MSA] Logging into Minecraft...\n");
    char mc_token[2048];
    if (!msa_mc_login(uhs, xsts_token, mc_token, sizeof(mc_token))) {
        printf("[MSA] Minecraft login failed.\n"); return 0;
    }
    printf("[MSA] Fetching profile...\n");
    if (!msa_get_profile(mc_token, out_uuid, 64, out_username, 64)) {
        printf("[MSA] Failed to get Minecraft profile.\n"); return 0;
    }
    safe_str_cpy(out_accessToken, 2048, mc_token);
    printf("[MSA] Login successful! Welcome, %s!\n", out_username);
    return 1;
}

// ==================== 外置登录（完整版，支持多角色选择和刷新令牌） ====================
int yggdrasil_authenticate(const char* api_root, const char* email, const char* password,
                           char* out_username, char* out_uuid, char* out_accessToken) {
    print("[DEBUG] API root: ");
    print(api_root);
    print("\n");

    // 1. 构建认证 URL（自动补全 /authserver/authenticate）
    char auth_url[512];
    safe_str_cpy(auth_url, sizeof(auth_url), api_root);
    size_t len = strlen(auth_url);
    if (len > 0 && auth_url[len-1] == '/') auth_url[len-1] = '\0';
    if (strstr(auth_url, "/authserver/authenticate") == NULL) {
        safe_str_cat(auth_url, sizeof(auth_url), "/authserver/authenticate");
    }
    print("[DEBUG] Auth URL: ");
    print(auth_url);
    print("\n");

    // 2. 生成 clientToken
    char client_token[CLIENT_TOKEN_LEN];
    srand((unsigned int)(time(NULL) ^ (unsigned long)getpid()));
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < CLIENT_TOKEN_LEN - 1; i++) client_token[i] = hex[rand() % 16];
    client_token[CLIENT_TOKEN_LEN - 1] = '\0';

    // 3. 构建认证 JSON
    cJSON* auth_json = cJSON_CreateObject();
    cJSON* agent = cJSON_CreateObject();
    cJSON_AddStringToObject(agent, "name", "Minecraft");
    cJSON_AddNumberToObject(agent, "version", 1);
    cJSON_AddItemToObject(auth_json, "agent", agent);
    cJSON_AddStringToObject(auth_json, "username", email);
    cJSON_AddStringToObject(auth_json, "password", password);
    cJSON_AddStringToObject(auth_json, "clientToken", client_token);
    cJSON_AddBoolToObject(auth_json, "requestUser", 1);

    char* auth_data = cJSON_PrintUnformatted(auth_json);
    cJSON_Delete(auth_json);
    if (!auth_data) {
        print("Failed to create JSON request.\n");
        return 0;
    }

    // 4. 发送认证请求
    char* response = http_post(auth_url, auth_data);
    free(auth_data);
    if (!response) {
        print("HTTP request failed. Check network or server address.\n");
        return 0;
    }
    
    print("[DEBUG] Response: ");
    print(response);
    print("\n");

    // 5. 解析响应
    cJSON* root = cJSON_Parse(response);
    if (!root) {
        const char* error_ptr = cJSON_GetErrorPtr();
        print("Failed to parse JSON response.\n");
        if (error_ptr) {
            print("Error at: ");
            print(error_ptr);
            print("\n");
        }
        free(response);
        return 0;
    }
    free(response);

    // 检查错误
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error && cJSON_IsString(error)) {
        cJSON* error_msg = cJSON_GetObjectItem(root, "errorMessage");
        print("Authentication failed: ");
        print(error->valuestring);
        if (error_msg && cJSON_IsString(error_msg)) { print(" - "); print(error_msg->valuestring); }
        print("\n");
        cJSON_Delete(root);
        return 0;
    }

    // 提取 accessToken
    cJSON* accessTokenItem = cJSON_GetObjectItem(root, "accessToken");
    if (!accessTokenItem || !cJSON_IsString(accessTokenItem)) {
        print("Missing accessToken in response.\n");
        cJSON_Delete(root);
        return 0;
    }
    safe_str_cpy(out_accessToken, 128, accessTokenItem->valuestring);

    // 6. 获取角色列表
    cJSON* available_profiles = cJSON_GetObjectItem(root, "availableProfiles");
    cJSON* selected_profile = cJSON_GetObjectItem(root, "selectedProfile");

    if (!available_profiles || cJSON_GetArraySize(available_profiles) == 0) {
        print("No profiles found for this account.\n");
        cJSON_Delete(root);
        return 0;
    }

    int profile_count = cJSON_GetArraySize(available_profiles);
    cJSON* chosen_profile = NULL;

    if (selected_profile && cJSON_IsObject(selected_profile)) {
        // 有默认角色，直接使用
        chosen_profile = selected_profile;
        print("Auto-selected default profile.\n");
    } else {
        // 多角色，交互选择
        print("Multiple profiles found, please select:\n");
        for (int i = 0; i < profile_count; i++) {
            cJSON* prof = cJSON_GetArrayItem(available_profiles, i);
            cJSON* name = cJSON_GetObjectItem(prof, "name");
            cJSON* id = cJSON_GetObjectItem(prof, "id");
            if (name && cJSON_IsString(name) && id && cJSON_IsString(id)) {
                print("  ["); print_int(i+1); print("] ");
                print(name->valuestring);
                print(" ("); print(id->valuestring); print(")\n");
            }
        }
        print("Select (1-"); print_int(profile_count); print("): ");
        char input[4] = {0};
        console_read_line(input, sizeof(input));
        int sel = str_to_int(input);
        if (sel < 1 || sel > profile_count) sel = 1;
        chosen_profile = cJSON_GetArrayItem(available_profiles, sel - 1);
    }

    // 提取角色信息
    cJSON* chosen_id = cJSON_GetObjectItem(chosen_profile, "id");
    cJSON* chosen_name = cJSON_GetObjectItem(chosen_profile, "name");
    if (!chosen_id || !cJSON_IsString(chosen_id) || !chosen_name || !cJSON_IsString(chosen_name)) {
        print("Invalid profile data.\n");
        cJSON_Delete(root);
        return 0;
    }
    safe_str_cpy(out_uuid, 64, chosen_id->valuestring);
    safe_str_cpy(out_username, 64, chosen_name->valuestring);

    // 7. 如果没有默认角色（即用户手动选择了非默认角色），需要刷新令牌
    if (!selected_profile) {
        print("Refreshing token for selected profile...\n");
        char refresh_url[512];
        safe_str_cpy(refresh_url, sizeof(refresh_url), api_root);
        len = strlen(refresh_url);
        if (len > 0 && refresh_url[len-1] == '/') refresh_url[len-1] = '\0';
        safe_str_cat(refresh_url, sizeof(refresh_url), "/authserver/refresh");

        cJSON* refresh_json = cJSON_CreateObject();
        cJSON_AddStringToObject(refresh_json, "accessToken", out_accessToken);
        cJSON_AddStringToObject(refresh_json, "clientToken", client_token);
        cJSON* prof_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(prof_obj, "id", out_uuid);
        cJSON_AddStringToObject(prof_obj, "name", out_username);
        cJSON_AddItemToObject(refresh_json, "selectedProfile", prof_obj);
        cJSON_AddBoolToObject(refresh_json, "requestUser", 1);

        char* refresh_data = cJSON_PrintUnformatted(refresh_json);
        cJSON_Delete(refresh_json);
        if (refresh_data) {
            char* refresh_resp = http_post(refresh_url, refresh_data);
            free(refresh_data);
            if (refresh_resp) {
                cJSON* refresh_root = cJSON_Parse(refresh_resp);
                free(refresh_resp);
                if (refresh_root) {
                    cJSON* new_token = cJSON_GetObjectItem(refresh_root, "accessToken");
                    if (new_token && cJSON_IsString(new_token)) {
                        safe_str_cpy(out_accessToken, 128, new_token->valuestring);
                    }
                    cJSON_Delete(refresh_root);
                }
            }
        }
    }

    cJSON_Delete(root);
    return 1;
}
//===============================================================
//=============其他登录============================================

void login_account(int argc, char** argv) {
    if (argc < 4) {
        print("=== Account Login Help ===\n");
        print("Offline:  mc -u -l offline <username> [-uuid <uuid>]\n");
        print("External: mc -u -l external <api_root> <email> <password>\n");
        print("  Example: mc -u -l external https://littleskin.cn/api/yggdrasil user@example.com pass\n");
        print("Official: mc -u -l official <email> <password> (reserved)\n");
        print("Delete:   mc -u -del -usertype <index>\n");
        print("Relogin:  mc -u -relogin -usertype <index>\n");
        print("==========================\n");
        return;
    }

    if (str_cmp(argv[2], "-del") == 0) {
        int usertype = 0;
        for (int i = 3; i < argc; i++) {
            if (str_cmp(argv[i], "-usertype") == 0 && i+1 < argc) {
                usertype = str_to_int(argv[++i]);
                break;
            }
        }
        if (usertype < 1) {
            print("Error: Please specify valid usertype index (>= 1)\n");
            return;
        }
        delete_account(usertype);
        return;
    }

    if (str_cmp(argv[2], "-relogin") == 0) {
        int usertype = 0;
        for (int i = 3; i < argc; i++) {
            if (str_cmp(argv[i], "-usertype") == 0 && i+1 < argc) {
                usertype = str_to_int(argv[++i]);
                break;
            }
        }
        if (usertype < 1) {
            print("Error: Please specify valid usertype index (>= 1)\n");
            return;
        }
        relogin_account(usertype);
        return;
    }

    AccountInfo acc = {0};
    safe_str_cpy(acc.type, sizeof(acc.type), argv[3]);
    str_trim_quotes(acc.type);
    acc.is_default = 1;

    char custom_uuid[64] = {0};
    for (int i = 4; i < argc; i++) {
        if (str_cmp(argv[i], "-uuid") == 0 && i+1 < argc) {
            safe_str_cpy(custom_uuid, sizeof(custom_uuid), argv[++i]);
            str_trim_quotes(custom_uuid);
        }
    }

    if (str_cmp(acc.type, "offline") == 0) {
        if (argc < 5) { print("Error: Please input username!\n"); return; }
        safe_str_cpy(acc.username, sizeof(acc.username), argv[4]);
        str_trim_quotes(acc.username);
        safe_str_cpy(acc.email, sizeof(acc.email), acc.username);
        safe_str_cpy(acc.accessToken, sizeof(acc.accessToken), "offline_token");
        
        if (str_len(custom_uuid) > 0) {
            safe_str_cpy(acc.uuid, sizeof(acc.uuid), custom_uuid);
        } else {
            unsigned int hash = 0;
            const char* name = acc.username;
            for (int i = 0; name[i]; i++) hash = name[i] + (hash << 6) + (hash << 16) - hash;
            snprintf(acc.uuid, sizeof(acc.uuid), "%08x-0000-0000-0000-000000000000", hash);
        }
        print("Offline login: "); print(acc.username); print("\n");
        save_account(&acc);
        print("Login success! Account set as default.\n");
        return;
    } else if (str_cmp(acc.type, "external") == 0) {
        if (argc < 7) { print("Error: Missing parameters!\n"); return; }
        safe_str_cpy(acc.server, sizeof(acc.server), argv[4]);
        str_trim_quotes(acc.server);
        safe_str_cpy(acc.email, sizeof(acc.email), argv[5]);
        str_trim_quotes(acc.email);
        safe_str_cpy(acc.password, sizeof(acc.password), argv[6]);
        str_trim_quotes(acc.password);
        print("Authenticating with: "); print(acc.server); print("\n");
        char username[64] = {0}, uuid[64] = {0}, token[128] = {0};
        if (yggdrasil_authenticate(acc.server, acc.email, acc.password, username, uuid, token)) {
            safe_str_cpy(acc.username, sizeof(acc.username), username);
            safe_str_cpy(acc.uuid, sizeof(acc.uuid), uuid);
            safe_str_cpy(acc.accessToken, sizeof(acc.accessToken), token);
            acc.player_id_count = 1;
            safe_str_cpy(acc.player_ids[0], 64, username);
            print("Login successful! Username: "); print(acc.username); print("\n");
            save_account(&acc);
            print("Account saved and set as default.\n");
        } else { print("External login failed.\n"); }
        return;
    } else if (str_cmp(acc.type, "official") == 0) {
        /* device code 登录不需要邮箱密码，但允许填入作为标识 */
        if (argc >= 6) {
            safe_str_cpy(acc.email, sizeof(acc.email), argv[4]);
            str_trim_quotes(acc.email);
            safe_str_cpy(acc.username, sizeof(acc.username), acc.email);
        } else {
            safe_str_cpy(acc.username, sizeof(acc.username), "Player");
        }
        printf("Official login (Microsoft device code)...\n");
        char msa_user[64], msa_uuid[64], msa_token[2048];
        if (!msa_authenticate(msa_user, msa_uuid, msa_token)) {
            printf("Microsoft login failed. Please try again.\n");
            return;
        }
        safe_str_cpy(acc.username, sizeof(acc.username), msa_user);
        safe_str_cpy(acc.uuid, sizeof(acc.uuid), msa_uuid);
        safe_str_cpy(acc.accessToken, sizeof(acc.accessToken), msa_token);
        printf("Microsoft login success! Welcome: %s\n", acc.username);
        save_account(&acc);
        printf("Account saved.\n");
        return;
    } else {
        print("Invalid account type! Use offline/external/official.\n");
    }
}
//===============================================================

// ==================== 版本管理等辅助 ====================
int version_exists(const char* ver) {
    char path[MAX_PATH_LEN];
    build_mc_path(path, sizeof(path), ver, "versions", ".json");
    return file_exists(path);
}

const char* get_modloader(const char* ver) {
    char json_path[MAX_PATH_LEN];
    build_mc_path(json_path, sizeof(json_path), ver, "versions", ".json");
    char* buf = read_file(json_path);
    if (!buf) return "Unknown";
    
    // 检查具体模组加载器（基于 Maven 坐标前缀匹配，与库名 "group:artifact:version" 兼容）
    // 注意顺序：Quilt/Fabric 可能共用 net.fabricmc 系列库，NeoForge 含 net.minecraftforge 前缀，需先匹配更特定项
    const char* res = "Modded";
    if (strstr(buf, "org.quiltmc:quilt-loader")) res = "Quilt";
    else if (strstr(buf, "net.fabricmc:fabric-loader") || strstr(buf, "net.fabricmc.loader.impl.launch.knot.KnotClient")) res = "Fabric";
    else if (strstr(buf, "net.neoforged.fancymodloader") || strstr(buf, "net.neoforged:loader") || strstr(buf, "net.neoforged:neoforge")) res = "NeoForge";
    else if (strstr(buf, "net.minecraftforge:fmlloader") || strstr(buf, "net.minecraftforge:forge")) res = "Forge";
    else if (strstr(buf, "com.mumfrey:liteloader")) res = "LiteLoader";
    else if (strstr(buf, "com.chocohead:rift")) res = "Rift";
    else if (strstr(buf, "optifine") || strstr(buf, "OptiFine")) res = "OptiFine";
    else if (strstr(buf, "\"inheritsFrom\"") || strstr(buf, "\"clientVersion\"")) res = "Modded";
    else if (strstr(buf, "\"net.minecraft.client.main.Main\"")) res = "Vanilla";
    free(buf);
    return res;
}

// ==================== 下载命令相关功能 ====================
typedef struct {
    char version_type[32];  // release, snapshot, old_version, april_fools
    char version[64];
    char mod_loader[32];    // Forge, Fabric, Quilt, NeoForge, Liteloader
    char mod_loader_version[64];
    int list_versions;
    int list_mod_loaders;
} DownloadParams;

void parse_download_params(int argc, char** argv, DownloadParams* params) {
    memset(params, 0, sizeof(DownloadParams));
    for (int i = 2; i < argc; i++) {
        if (str_cmp(argv[i], "-ver") == 0 && i+1 < argc) {
            // 检查下一个参数是否是另一个选项
            if (i+2 < argc && argv[i+2][0] == '-') {
                // 只有一个参数，作为 version
                safe_str_cpy(params->version, sizeof(params->version), argv[++i]);
                str_trim_quotes(params->version);
            } else if (i+2 < argc) {
                // 两个参数：type 和 version
                safe_str_cpy(params->version_type, sizeof(params->version_type), argv[++i]);
                str_trim_quotes(params->version_type);
                safe_str_cpy(params->version, sizeof(params->version), argv[++i]);
                str_trim_quotes(params->version);
            } else {
                // 只有一个参数
                safe_str_cpy(params->version, sizeof(params->version), argv[++i]);
                str_trim_quotes(params->version);
            }
        }
        else if (str_cmp(argv[i], "-mod_loader") == 0 && i+1 < argc) {
            safe_str_cpy(params->mod_loader, sizeof(params->mod_loader), argv[++i]);
            str_trim_quotes(params->mod_loader);
            if (i+1 < argc && argv[i+1][0] != '-') {
                safe_str_cpy(params->mod_loader_version, sizeof(params->mod_loader_version), argv[++i]);
                str_trim_quotes(params->mod_loader_version);
            }
        }
        else if (str_cmp(argv[i], "-ver_list") == 0 && i+1 < argc) {
            safe_str_cpy(params->version_type, sizeof(params->version_type), argv[++i]);
            str_trim_quotes(params->version_type);
            params->list_versions = 1;
        }
        else if (str_cmp(argv[i], "-mod_loader_list") == 0 && i+1 < argc) {
            safe_str_cpy(params->mod_loader, sizeof(params->mod_loader), argv[++i]);
            str_trim_quotes(params->mod_loader);
            params->list_mod_loaders = 1;
        }
    }
}

// 从版本类型映射到 manifest 中的 type 字段
const char* version_type_to_manifest(const char* type) {
    if (str_cmp(type, "release") == 0 || str_cmp(type, "正式版") == 0) return "release";
    if (str_cmp(type, "snapshot") == 0 || str_cmp(type, "快照版") == 0) return "snapshot";
    if (str_cmp(type, "old_version") == 0 || str_cmp(type, "old") == 0 || str_cmp(type, "远古版") == 0) return "old_beta";
    if (str_cmp(type, "april_fools") == 0 || str_cmp(type, "愚人节版") == 0) return "snapshot"; // 愚人节版本通常标记为 snapshot
    return type;
}

// 列出可用版本
void list_available_versions(const char* type) {
    char url[512];
    safe_str_cpy(url, sizeof(url), MC_BASE_URL);
    safe_str_cat(url, sizeof(url), "/mc/game/version_manifest.json");

    print("Fetching version list...\n");
    char* manifest = http_get(url);
    if (!manifest) {
        print("Failed to fetch version manifest.\n");
        return;
    }

    cJSON* root = cJSON_Parse(manifest);
    free(manifest);
    if (!root) {
        print("Failed to parse version manifest.\n");
        return;
    }

    const char* manifest_type = version_type_to_manifest(type);
    cJSON* versions = cJSON_GetObjectItem(root, "versions");
    if (versions && cJSON_IsArray(versions)) {
        int size = cJSON_GetArraySize(versions);
        int count = 0;
        print("=== Available Versions ===\n");
        for (int i = 0; i < size; i++) {
            cJSON* ver = cJSON_GetArrayItem(versions, i);
            cJSON* id = cJSON_GetObjectItem(ver, "id");
            cJSON* vtype = cJSON_GetObjectItem(ver, "type");
            if (id && cJSON_IsString(id) && vtype && cJSON_IsString(vtype)) {
                // 过滤版本类型
                int match = 0;
                if (str_cmp(type, "release") == 0 || str_cmp(type, "正式版") == 0) {
                    match = (str_cmp(vtype->valuestring, "release") == 0);
                } else if (str_cmp(type, "snapshot") == 0 || str_cmp(type, "快照版") == 0) {
                    match = (str_cmp(vtype->valuestring, "snapshot") == 0);
                } else if (str_cmp(type, "old_version") == 0 || str_cmp(type, "old") == 0 || str_cmp(type, "远古版") == 0) {
                    match = (str_cmp(vtype->valuestring, "old_beta") == 0 || str_cmp(vtype->valuestring, "old_alpha") == 0);
                } else if (str_cmp(type, "april_fools") == 0 || str_cmp(type, "愚人节版") == 0) {
                    // 愚人节版本通常是 3 月份发布的快照版，ID 格式如 YYw14a 或包含特定关键词
                    // 检查是否是 w14a 或 w13a 或 w15a（愚人节前后）
                    const char* vid = id->valuestring;
                    match = (strstr(vid, "20w14infinite") || strstr(vid, "3D") ||
                             strstr(vid, "1.RV-Pre1") || strstr(vid, "15w14a") ||
                             strstr(vid, "2.0") || strstr(vid, "infinite") ||
                             // 匹配 YYw14a 格式（如 26w14a, 25w14a, 24w14a 等）
                             (str_cmp(vtype->valuestring, "snapshot") == 0 && 
                              (strstr(vid, "w14a") || strstr(vid, "w13a") || strstr(vid, "w15a"))));
                } else {
                    match = 1; // 显示所有版本
                }

                if (match) {
                    print("  "); print(id->valuestring); print(" ["); print(vtype->valuestring); print("]\n");
                    count++;
                }
            }
        }
        print("Total: "); print_int(count); print(" versions\n");
    }
    cJSON_Delete(root);
}

// 下载并安装指定版本
int download_and_install_version(const char* version_id) {
    print("Downloading version: "); print(version_id); print("\n");

    // 下载版本 JSON
    if (!download_version_json(version_id)) {
        print("Failed to download version JSON.\n");
        return 0;
    }

    // 验证并下载所需文件
    if (!verify_and_download_files(version_id)) {
        print("Failed to download some game files.\n");
        return 0;
    }

    print("Version "); print(version_id); print(" installed successfully!\n");
    return 1;
}

// 列出模组加载器版本
void list_mod_loader_versions(const char* loader) {
    if (str_cmp(loader, "Forge") == 0 || str_cmp(loader, "forge") == 0) {
        print("Fetching Forge versions...\n");
        // Forge 版本列表 - 使用 Maven metadata
        char* forge_list = http_get("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");
        if (forge_list) {
            // 简单解析 XML 中的版本号
            print("=== Forge Versions ===\n");
            char* p = forge_list;
            int count = 0;
            while ((p = strstr(p, "<version>")) != NULL && count < 50) {
                p += 9; // 跳过 "<version>"
                char* end = strstr(p, "</version>");
                if (end) {
                    char ver[64];
                    int len = (int)(end - p);
                    if (len > 0 && len < 63) {
                        memcpy(ver, p, len);
                        ver[len] = '\0';
                        print("  "); print(ver); print("\n");
                        count++;
                    }
                    p = end;
                } else {
                    break;
                }
            }
            print("Showing latest 50 versions\n");
            free(forge_list);
        } else {
            print("Failed to fetch Forge versions.\n");
        }
    }
    else if (str_cmp(loader, "Fabric") == 0 || str_cmp(loader, "fabric") == 0) {
        print("Fetching Fabric versions...\n");
        char* fabric_list = http_get("https://meta.fabricmc.net/v2/versions/loader");
        if (fabric_list) {
            cJSON* root = cJSON_Parse(fabric_list);
            free(fabric_list);
            if (root && cJSON_IsArray(root)) {
                print("=== Fabric Loader Versions ===\n");
                int size = cJSON_GetArraySize(root);
                int count = 0;
                for (int i = 0; i < size && count < 50; i++) {
                    cJSON* item = cJSON_GetArrayItem(root, i);
                    if (item) {
                        cJSON* version = cJSON_GetObjectItem(item, "version");
                        if (version && cJSON_IsString(version)) {
                            print("  "); print(version->valuestring); print("\n");
                            count++;
                        }
                    }
                }
                print("Showing latest 50 versions\n");
                cJSON_Delete(root);
            } else {
                print("Failed to parse Fabric version list.\n");
            }
        } else {
            print("Failed to fetch Fabric versions.\n");
        }
    }
    else if (str_cmp(loader, "Quilt") == 0 || str_cmp(loader, "quilt") == 0) {
        print("Fetching Quilt versions...\n");
        char* quilt_list = http_get("https://meta.quiltmc.org/v3/versions/loader");
        if (quilt_list) {
            cJSON* root = cJSON_Parse(quilt_list);
            free(quilt_list);
            if (root && cJSON_IsArray(root)) {
                print("=== Quilt Loader Versions ===\n");
                int size = cJSON_GetArraySize(root);
                int count = 0;
                for (int i = 0; i < size && count < 50; i++) {
                    cJSON* item = cJSON_GetArrayItem(root, i);
                    if (item) {
                        cJSON* version = cJSON_GetObjectItem(item, "version");
                        if (version && cJSON_IsString(version)) {
                            print("  "); print(version->valuestring); print("\n");
                            count++;
                        }
                    }
                }
                print("Showing latest 50 versions\n");
                cJSON_Delete(root);
            } else {
                print("Failed to parse Quilt version list.\n");
            }
        } else {
            print("Failed to fetch Quilt versions.\n");
        }
    }
    else if (str_cmp(loader, "NeoForge") == 0 || str_cmp(loader, "neoforge") == 0) {
        print("Fetching NeoForge versions...\n");
        char* neoforge_list = http_get("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml");
        if (neoforge_list) {
            print("=== NeoForge Versions ===\n");
            char* p = neoforge_list;
            int count = 0;
            while ((p = strstr(p, "<version>")) != NULL && count < 50) {
                p += 9;
                char* end = strstr(p, "</version>");
                if (end) {
                    char ver[64];
                    int len = (int)(end - p);
                    if (len > 0 && len < 63) {
                        memcpy(ver, p, len);
                        ver[len] = '\0';
                        print("  "); print(ver); print("\n");
                        count++;
                    }
                    p = end;
                } else {
                    break;
                }
            }
            print("Showing latest 50 versions\n");
            free(neoforge_list);
        } else {
            print("Failed to fetch NeoForge versions.\n");
        }
    }
    else if (str_cmp(loader, "Liteloader") == 0 || str_cmp(loader, "liteloader") == 0) {
        print("Fetching Liteloader versions...\n");
        char* lite_list = http_get("https://repo.mumfrey.com/content/repositories/snapshots/com/mumfrey/liteloader/maven-metadata.xml");
        if (lite_list) {
            print("=== Liteloader Versions ===\n");
            char* p = lite_list;
            int count = 0;
            while ((p = strstr(p, "<version>")) != NULL && count < 50) {
                p += 9;
                char* end = strstr(p, "</version>");
                if (end) {
                    char ver[64];
                    int len = (int)(end - p);
                    if (len > 0 && len < 63) {
                        memcpy(ver, p, len);
                        ver[len] = '\0';
                        print("  "); print(ver); print("\n");
                        count++;
                    }
                    p = end;
                } else {
                    break;
                }
            }
            print("Showing latest 50 versions\n");
            free(lite_list);
        } else {
            print("Failed to fetch Liteloader versions.\n");
        }
    }
    else {
        print("Unknown mod loader: "); print(loader); print("\n");
        print("Supported: Forge, Fabric, Quilt, NeoForge, Liteloader\n");
    }
}

// 下载并安装模组加载器
int download_and_install_mod_loader(const char* loader, const char* loader_version, const char* mc_version) {
    print("Installing "); print(loader);
    if (str_len(loader_version) > 0) {
        print(" v"); print(loader_version);
    }
    if (str_len(mc_version) > 0) {
        print(" for Minecraft "); print(mc_version);
    }
    print("\n");

    // 创建临时目录
    char temp_dir[MAX_PATH_LEN];
    safe_str_cpy(temp_dir, sizeof(temp_dir), mc_path);
    safe_str_cat(temp_dir, sizeof(temp_dir), PATH_SEP_STR "temp_modloader");
    print("Temp dir: "); print(temp_dir); print("\n");
    CreateDirectoryA(temp_dir, NULL);

    if (str_cmp(loader, "Fabric") == 0 || str_cmp(loader, "fabric") == 0) {
        // Fabric 安装
        // Fabric 安装器版本与 Loader 版本不同，使用最新版安装器
        char installer_url[512];
        safe_str_cpy(installer_url, sizeof(installer_url), "https://maven.fabricmc.net/net/fabricmc/fabric-installer/1.1.1/fabric-installer-1.1.1.jar");

        char installer_path[MAX_PATH_LEN];
        safe_str_cpy(installer_path, sizeof(installer_path), temp_dir);
        safe_str_cat(installer_path, sizeof(installer_path), PATH_SEP_STR "fabric-installer.jar");

        // 确保临时目录存在
        CreateDirectoryA(temp_dir, NULL);

        print("Downloading Fabric installer...\n");
        print("URL: "); print(installer_url); print("\n");
        print("Path: "); print(installer_path); print("\n");
        if (!http_get_file(installer_url, installer_path)) {
            print("Failed to download Fabric installer.\n");
            return 0;
        }

        // 运行安装器
        char cmd[2048];
        char java_path[MAX_PATH_LEN];
        if (str_len(default_java_path) > 0) {
            safe_str_cpy(java_path, sizeof(java_path), default_java_path);
        } else {
            // 尝试使用系统 Java
            safe_str_cpy(java_path, sizeof(java_path), "java");
        }

        safe_str_cpy(cmd, sizeof(cmd), "\"");
        safe_str_cat(cmd, sizeof(cmd), java_path);
        safe_str_cat(cmd, sizeof(cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE "\" -jar \"");
        safe_str_cat(cmd, sizeof(cmd), installer_path);
        safe_str_cat(cmd, sizeof(cmd), "\" client -dir \"");
        safe_str_cat(cmd, sizeof(cmd), mc_path);
        safe_str_cat(cmd, sizeof(cmd), "\" -mcversion ");
        safe_str_cat(cmd, sizeof(cmd), mc_version);
        if (str_len(loader_version) > 0) {
            safe_str_cat(cmd, sizeof(cmd), " -loader ");
            safe_str_cat(cmd, sizeof(cmd), loader_version);
        }
        safe_str_cat(cmd, sizeof(cmd), " -downloadMinecraft");

        print("Running Fabric installer...\n");
        print("Command: "); print(cmd); print("\n");

        int exit_code = run_cmd_blocking(cmd, temp_dir);
        if (exit_code == 0) {
            print("Fabric installed successfully!\n");
        } else if (exit_code == -1) {
            print("Failed to run Fabric installer. Make sure Java is available.\n");
            return 0;
        } else {
            print("Fabric installation failed with exit code: ");
            char code[16]; int_to_str(exit_code, code, sizeof(code));
            print(code); print("\n");
            return 0;
        }
    }
    else if (str_cmp(loader, "Forge") == 0 || str_cmp(loader, "forge") == 0) {
        // Forge 安装
        char installer_url[512];
        if (str_len(loader_version) > 0) {
            safe_str_cpy(installer_url, sizeof(installer_url), "https://maven.minecraftforge.net/net/minecraftforge/forge/");
            safe_str_cat(installer_url, sizeof(installer_url), mc_version);
            safe_str_cat(installer_url, sizeof(installer_url), "-");
            safe_str_cat(installer_url, sizeof(installer_url), loader_version);
            safe_str_cat(installer_url, sizeof(installer_url), "/forge-");
            safe_str_cat(installer_url, sizeof(installer_url), mc_version);
            safe_str_cat(installer_url, sizeof(installer_url), "-");
            safe_str_cat(installer_url, sizeof(installer_url), loader_version);
            safe_str_cat(installer_url, sizeof(installer_url), "-installer.jar");
        } else {
            print("Error: Forge version must be specified.\n");
            return 0;
        }

        char installer_path[MAX_PATH_LEN];
        safe_str_cpy(installer_path, sizeof(installer_path), temp_dir);
        safe_str_cat(installer_path, sizeof(installer_path), PATH_SEP_STR "forge-installer.jar");

        print("Downloading Forge installer...\n");
        if (!http_get_file(installer_url, installer_path)) {
            print("Failed to download Forge installer.\n");
            return 0;
        }

        // 运行安装器
        char cmd[2048];
        char java_path[MAX_PATH_LEN];
        if (str_len(default_java_path) > 0) {
            safe_str_cpy(java_path, sizeof(java_path), default_java_path);
        } else {
            safe_str_cpy(java_path, sizeof(java_path), "java");
        }

        safe_str_cpy(cmd, sizeof(cmd), "\"");
        safe_str_cat(cmd, sizeof(cmd), java_path);
        safe_str_cat(cmd, sizeof(cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE "\" -jar \"");
        safe_str_cat(cmd, sizeof(cmd), installer_path);
        safe_str_cat(cmd, sizeof(cmd), "\" --installClient \"");
        safe_str_cat(cmd, sizeof(cmd), mc_path);
        safe_str_cat(cmd, sizeof(cmd), "\"");

        print("Running Forge installer...\n");
        print("Command: "); print(cmd); print("\n");

        int exit_code = run_cmd_blocking(cmd, temp_dir);
        if (exit_code == 0) {
            print("Forge installed successfully!\n");
        } else if (exit_code == -1) {
            print("Failed to run Forge installer. Make sure Java is available.\n");
            return 0;
        } else {
            print("Forge installation failed with exit code: ");
            char code[16]; int_to_str(exit_code, code, sizeof(code));
            print(code); print("\n");
            return 0;
        }
    }
    else if (str_cmp(loader, "Quilt") == 0 || str_cmp(loader, "quilt") == 0) {
        // Quilt 安装 - 使用最新版安装器
        char installer_url[512];
        safe_str_cpy(installer_url, sizeof(installer_url), "https://maven.quiltmc.org/repository/release/org/quiltmc/quilt-installer/0.10.0/quilt-installer-0.10.0.jar");

        char installer_path[MAX_PATH_LEN];
        safe_str_cpy(installer_path, sizeof(installer_path), temp_dir);
        safe_str_cat(installer_path, sizeof(installer_path), PATH_SEP_STR "quilt-installer.jar");

        print("Downloading Quilt installer...\n");
        if (!http_get_file(installer_url, installer_path)) {
            print("Failed to download Quilt installer.\n");
            return 0;
        }

        char cmd[2048];
        char java_path[MAX_PATH_LEN];
        if (str_len(default_java_path) > 0) {
            safe_str_cpy(java_path, sizeof(java_path), default_java_path);
        } else {
            safe_str_cpy(java_path, sizeof(java_path), "java");
        }

        safe_str_cpy(cmd, sizeof(cmd), "\"");
        safe_str_cat(cmd, sizeof(cmd), java_path);
        safe_str_cat(cmd, sizeof(cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE "\" -jar \"");
        safe_str_cat(cmd, sizeof(cmd), installer_path);
        safe_str_cat(cmd, sizeof(cmd), "\" install client ");
        safe_str_cat(cmd, sizeof(cmd), mc_version);
        safe_str_cat(cmd, sizeof(cmd), " --install-dir=\"");
        safe_str_cat(cmd, sizeof(cmd), mc_path);
        safe_str_cat(cmd, sizeof(cmd), "\"");

        print("Running Quilt installer...\n");
        print("Command: "); print(cmd); print("\n");

        int exit_code = run_cmd_blocking(cmd, temp_dir);
        if (exit_code == 0) {
            print("Quilt installed successfully!\n");
        } else if (exit_code == -1) {
            print("Failed to run Quilt installer. Make sure Java is available.\n");
            return 0;
        } else {
            print("Quilt installation failed with exit code: ");
            char code[16]; int_to_str(exit_code, code, sizeof(code));
            print(code); print("\n");
            return 0;
        }
    }
    else if (str_cmp(loader, "NeoForge") == 0 || str_cmp(loader, "neoforge") == 0) {
        // NeoForge 安装
        char installer_url[512];
        if (str_len(loader_version) > 0) {
            safe_str_cpy(installer_url, sizeof(installer_url), "https://maven.neoforged.net/releases/net/neoforged/neoforge/");
            safe_str_cat(installer_url, sizeof(installer_url), loader_version);
            safe_str_cat(installer_url, sizeof(installer_url), "/neoforge-");
            safe_str_cat(installer_url, sizeof(installer_url), loader_version);
            safe_str_cat(installer_url, sizeof(installer_url), "-installer.jar");
        } else {
            print("Error: NeoForge version must be specified.\n");
            return 0;
        }

        char installer_path[MAX_PATH_LEN];
        safe_str_cpy(installer_path, sizeof(installer_path), temp_dir);
        safe_str_cat(installer_path, sizeof(installer_path), PATH_SEP_STR "neoforge-installer.jar");

        print("Downloading NeoForge installer...\n");
        if (!http_get_file(installer_url, installer_path)) {
            print("Failed to download NeoForge installer.\n");
            return 0;
        }

        char cmd[2048];
        char java_path[MAX_PATH_LEN];
        if (str_len(default_java_path) > 0) {
            safe_str_cpy(java_path, sizeof(java_path), default_java_path);
        } else {
            safe_str_cpy(java_path, sizeof(java_path), "java");
        }

        safe_str_cpy(cmd, sizeof(cmd), "\"");
        safe_str_cat(cmd, sizeof(cmd), java_path);
        safe_str_cat(cmd, sizeof(cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE "\" -jar \"");
        safe_str_cat(cmd, sizeof(cmd), installer_path);
        safe_str_cat(cmd, sizeof(cmd), "\" --installClient \"");
        safe_str_cat(cmd, sizeof(cmd), mc_path);
        safe_str_cat(cmd, sizeof(cmd), "\"");

        print("Running NeoForge installer...\n");
        print("Command: "); print(cmd); print("\n");

        int exit_code = run_cmd_blocking(cmd, temp_dir);
        if (exit_code == 0) {
            print("NeoForge installed successfully!\n");
        } else if (exit_code == -1) {
            print("Failed to run NeoForge installer. Make sure Java is available.\n");
            return 0;
        } else {
            print("NeoForge installation failed with exit code: ");
            char code[16]; int_to_str(exit_code, code, sizeof(code));
            print(code); print("\n");
            return 0;
        }
    }
    else if (str_cmp(loader, "Liteloader") == 0 || str_cmp(loader, "liteloader") == 0 ||
             str_cmp(loader, "LiteLoader") == 0) {
        print("Installing LiteLoader for "); print(mc_version); print("...\n");
        /* LiteLoader 使用 snapshot 仓库，需要先解析版本号 */
        char meta_url[512];
        snprintf(meta_url, sizeof(meta_url),
            "http://repo.mumfrey.com/content/repositories/snapshots/com/mumfrey/liteloader/%s-SNAPSHOT/maven-metadata.xml",
            mc_version);
        char* meta_xml = http_get(meta_url);
        char lite_url[512] = "";
        if (meta_xml) {
            /* 解析 <timestamp> 和 <buildNumber> */
            char *ts = strstr(meta_xml, "<timestamp>");
            char *bn = strstr(meta_xml, "<buildNumber>");
            if (ts && bn) {
                ts += 11; bn += 13;
                char *ts_end = strstr(ts, "</timestamp>");
                char *bn_end = strstr(bn, "</buildNumber>");
                if (ts_end && bn_end) {
                    int ts_len = (int)(ts_end - ts);
                    int bn_len = (int)(bn_end - bn);
                    if (ts_len > 0 && ts_len < 32 && bn_len > 0 && bn_len < 8) {
                        char timestamp[32], build[8];
                        memcpy(timestamp, ts, ts_len); timestamp[ts_len] = '\0';
                        memcpy(build, bn, bn_len); build[bn_len] = '\0';
                        snprintf(lite_url, sizeof(lite_url),
                            "http://repo.mumfrey.com/content/repositories/snapshots/com/mumfrey/liteloader/%s-SNAPSHOT/liteloader-%s-%s-%s.jar",
                            mc_version, mc_version, timestamp, build);
                    }
                }
            }
            free(meta_xml);
        }
        if (lite_url[0] == '\0') {
            /* fallback: 尝试 releases 仓库 */
            snprintf(lite_url, sizeof(lite_url),
                "http://repo.mumfrey.com/content/repositories/releases/com/mumfrey/liteloader/%s/liteloader-%s.jar",
                mc_version, mc_version);
        }
        char lite_path[MAX_PATH_LEN];
        safe_str_cpy(lite_path, sizeof(lite_path), mc_path);
        safe_str_cat(lite_path, sizeof(lite_path), PATH_SEP_STR "libraries" PATH_SEP_STR "com" PATH_SEP_STR "mumfrey" PATH_SEP_STR "liteloader" PATH_SEP_STR);
        safe_str_cat(lite_path, sizeof(lite_path), mc_version);
        safe_str_cat(lite_path, sizeof(lite_path), PATH_SEP_STR "liteloader-");
        safe_str_cat(lite_path, sizeof(lite_path), mc_version);
        safe_str_cat(lite_path, sizeof(lite_path), ".jar");
        print("Downloading: "); print(lite_url); print("\n");
        if (!file_exists(lite_path)) {
            create_parent_dirs(lite_path);
            if (!http_get_file(lite_url, lite_path)) {
                print("Failed to download LiteLoader.\n"); return 0;
            }
            print("LiteLoader JAR downloaded.\n");
        } else print("LiteLoader JAR already exists.\n");
        /* launchwrapper */
        char lw_path[MAX_PATH_LEN];
        safe_str_cpy(lw_path, sizeof(lw_path), mc_path);
        safe_str_cat(lw_path, sizeof(lw_path), PATH_SEP_STR "libraries" PATH_SEP_STR "net" PATH_SEP_STR "minecraft" PATH_SEP_STR "launchwrapper" PATH_SEP_STR "1.12" PATH_SEP_STR "launchwrapper-1.12.jar");
        if (!file_exists(lw_path)) {
            char lw_url[256]; safe_str_cpy(lw_url, sizeof(lw_url), LIBRARIES_URL);
            safe_str_cat(lw_url, sizeof(lw_url), "/net/minecraft/launchwrapper/1.12/launchwrapper-1.12.jar");
            create_parent_dirs(lw_path);
            if (!http_get_file(lw_url, lw_path)) {
                print("Failed to download launchwrapper.\n"); return 0;
            }
        }
        char ver_id[128]; snprintf(ver_id, sizeof(ver_id), "%s-LiteLoader%s", mc_version, mc_version);
        char json_path[MAX_PATH_LEN];
        build_mc_path(json_path, sizeof(json_path), ver_id, "versions", ".json");
        create_parent_dirs(json_path);
        char json_buf[2048];
        snprintf(json_buf, sizeof(json_buf),
            "{\"id\":\"%s\",\"inheritsFrom\":\"%s\",\"type\":\"release\","
            "\"mainClass\":\"net.minecraft.launchwrapper.Launch\","
            "\"minecraftArguments\":\"--tweakClass com.mumfrey.liteloader.launch.LiteLoaderTweaker "
            "--version ${version_name} --gameDir ${game_directory} --assetsDir ${assets_root} "
            "--assetIndex ${assets_index_name} --accessToken ${auth_access_token} "
            "--userProperties ${user_properties} --username ${auth_player_name}\","
            "\"libraries\":["
            "{\"name\":\"com.mumfrey:liteloader:%s\"},"
            "{\"name\":\"net.minecraft:launchwrapper:1.12\"}]}",
            ver_id, mc_version, mc_version);
        FILE* f = fopen(json_path, "w");
        if (f) { fwrite(json_buf, 1, str_len(json_buf), f); fclose(f); print("LiteLoader installed: "); print(ver_id); print("\n"); }
        else { print("Failed to create version JSON.\n"); return 0; }
    }
    else {
        print("Unsupported mod loader: "); print(loader); print("\n");
        print("Supported: Forge, Fabric, Quilt, NeoForge, Liteloader\n");
        return 0;
    }

    // 清理临时文件
    delete_directory_recursive(temp_dir);

    return 1;
}

// 下载命令主函数
void download_mc(int argc, char** argv) {
    DownloadParams params;
    parse_download_params(argc, argv, &params);

    // 如果要求列出版本
    if (params.list_versions) {
        list_available_versions(params.version_type);
        return;
    }

    // 如果要求列出模组加载器版本
    if (params.list_mod_loaders) {
        list_mod_loader_versions(params.mod_loader);
        return;
    }

    // 下载游戏版本
    if (str_len(params.version) > 0) {
        if (!download_and_install_version(params.version)) {
            return;
        }
    }

    // 安装模组加载器
    if (str_len(params.mod_loader) > 0) {
        // 如果没有指定 Minecraft 版本，使用刚刚下载的版本或默认版本
        char mc_ver[64];
        if (str_len(params.version) > 0) {
            safe_str_cpy(mc_ver, sizeof(mc_ver), params.version);
        } else if (str_len(default_ver) > 0) {
            safe_str_cpy(mc_ver, sizeof(mc_ver), default_ver);
        } else {
            print("Error: No Minecraft version specified.\n");
            return;
        }
        download_and_install_mod_loader(params.mod_loader, params.mod_loader_version, mc_ver);
    }

    if (str_len(params.version) == 0 && str_len(params.mod_loader) == 0) {
        print("No version or mod loader specified. Use -help for usage.\n");
    }
}

void list_mc_versions() {
    char dir[MAX_PATH_LEN];
    safe_str_cpy(dir, sizeof(dir), mc_path);
    safe_str_cat(dir, sizeof(dir), PATH_SEP_STR "versions");
    print("=== MC Versions (Mod Loader) ===\n");
    char names[64][MAX_PATH_LEN];
    int count = list_subdirs(dir, names, 64);
    if (count == 0) { print("No versions found.\n"); return; }
    for (int i = 0; i < count; i++) {
        print("  "); print(names[i]); print(" [");
        const char* loader = get_modloader(names[i]);
        print(loader); print("]\n");
    }
}

int select_version_interactive() {
    char dir[MAX_PATH_LEN];
    safe_str_cpy(dir, sizeof(dir), mc_path);
    safe_str_cat(dir, sizeof(dir), PATH_SEP_STR "versions");
    char names[64][MAX_PATH_LEN];
    int count = list_subdirs(dir, names, 64);
    if (count == 0) { print("No versions found.\n"); return -1; }
    if (count == 1) { print("Using version: "); print(names[0]); print("\n"); return 0; }
    char versions[64][64];
    for (int i = 0; i < count; i++) safe_str_cpy(versions[i], 64, names[i]);
    print("=== Select Version ===\n");
    for (int i = 0; i < count; i++) {
        print("  ["); print_int(i + 1); print("] "); print(versions[i]); print("\n");
    }
    print("Select (1-"); print_int(count); print("): ");
    char input[4] = {0};
    console_read_line(input, sizeof(input));
    int sel = str_to_int(input);
    if (sel > 0 && sel <= count) return sel - 1;
    print("Invalid selection, using default.\n");
    return 0;
}

void set_mc_path(const char* path) {
    safe_str_cpy(mc_path, sizeof(mc_path), path);
    str_trim_quotes(mc_path);
    write_config("MC_PATH", mc_path);
    print("MC path set to: "); print(mc_path); print("\n");
}

void set_default_ver(const char* ver) {
    char trimmed[64];
    safe_str_cpy(trimmed, sizeof(trimmed), ver);
    str_trim_quotes(trimmed);
    if (!version_exists(trimmed)) {
        print("Error: Version '"); print(trimmed); print("' not found in '"); print(mc_path); print(PATH_SEP_STR "versions'.\n");
        return;
    }
    safe_str_cpy(default_ver, sizeof(default_ver), trimmed);
    write_config("DEFAULT_VERSION", default_ver);
    print("Default version set to: "); print(default_ver); print("\n");
}

// 修复后的 set_launch_params 函数
void set_launch_params(int argc, char** argv) {
    if (argc < 4) {
        print("=== Set Launch Parameters ===\n");
        print("Memory: mc -set memory <Xms/Xmx> (e.g. 512M/2G)\n");
        print("Auto Memory: mc -set memory auto\n");
        print("JVM Args: mc -set jvm <args> (e.g. \"-Xmx3G -XX:+UseG1GC\")\n");
        print("=============================\n");
        return;
    }

    const char* param = argv[2];
    // 跳过可能的前导短横线
    if (param[0] == '-') param++;

    if (str_cmp(param, "memory") == 0) {
        if (argc < 4) {
            print("Please specify memory (e.g. 512M/2G or auto)\n");
            return;
        }
        const char* value = argv[3];
        // 跳过可能的前导短横线
        if (value[0] == '-') value++;

        if (str_cmp(value, "auto") == 0) {
            long total_mb = get_total_physical_mb();
            long xms = total_mb / 8;
            long xmx = total_mb / 2;
            char xms_str[16], xmx_str[16];
            int_to_str((int)xms, xms_str, sizeof(xms_str));
            int_to_str((int)xmx, xmx_str, sizeof(xmx_str));
            safe_str_cpy(jvm_args, sizeof(jvm_args), "-Xms");
            safe_str_cat(jvm_args, sizeof(jvm_args), xms_str);
            safe_str_cat(jvm_args, sizeof(jvm_args), "M -Xmx");
            safe_str_cat(jvm_args, sizeof(jvm_args), xmx_str);
            safe_str_cat(jvm_args, sizeof(jvm_args), "M");
        } else {
            const char* sep_pos = strchr(value, '/');
            if (!sep_pos) {
                print("Invalid memory format! Use Xms/Xmx (e.g. 512M/2G)\n");
                return;
            }
            int sep = (int)(sep_pos - value);
            char xms[32] = {0}, xmx[32] = {0};
            for (int i = 0; i < sep; i++) xms[i] = value[i];
            for (int i = sep + 1; i < (int)str_len(value); i++) xmx[i - sep - 1] = value[i];
            safe_str_cpy(jvm_args, sizeof(jvm_args), "-Xms");
            safe_str_cat(jvm_args, sizeof(jvm_args), xms);
            safe_str_cat(jvm_args, sizeof(jvm_args), " -Xmx");
            safe_str_cat(jvm_args, sizeof(jvm_args), xmx);
        }
        // 保存配置（略）
        print("Memory set to: "); print(jvm_args); print("\n");
    } else if (str_cmp(param, "jvm") == 0) {
        if (argc < 4) {
            print("Please specify JVM arguments\n");
            return;
        }
        const char* args = argv[3];
        if (args[0] == '-') args++;
        safe_str_cpy(jvm_args, sizeof(jvm_args), args);
        print("JVM args set to: "); print(jvm_args); print("\n");
    } else {
        print("Unknown parameter! Use memory or jvm\n");
    }
}

// ==================== 增强的 -start 参数解析 ====================
typedef struct {
    char version[64];
    char account_type[16];
    char account_email[128];
    char account_pass[128];
    char account_server[256];
    char java_home[MAX_PATH_LEN];
    int memory_mb;  // 0 表示使用默认
    char extra_jvm_args[1024];
    char extra_game_args[1024];
    char pre_command[1024];
    char window_title[256];
    int user_type_index;  // -1 表示使用默认账号
    char authlib_injector[512];  // authlib-injector jar path
    int no_authlib;  // disable authlib-injector
    int skip_verify;  // skip file verification and download
    int use_java;  // use java.exe instead of javaw.exe
} StartParams;

void parse_start_params(int argc, char** argv, StartParams* params) {
    memset(params, 0, sizeof(StartParams));
    params->user_type_index = -1;
    for (int i = 2; i < argc; i++) {
        if (str_cmp(argv[i], "-ver") == 0 && i+1 < argc) {
            safe_str_cpy(params->version, sizeof(params->version), argv[++i]);
            str_trim_quotes(params->version);
        }
        else if (str_cmp(argv[i], "-account") == 0 && i+1 < argc) {
            char* p = argv[++i];
            if (*p == '[') p++;
            char* parts[4];
            int pc = 0;
            parts[pc++] = p;
            while (*p && pc < 4) {
                if (*p == ',') {
                    *p = '\0';
                    p++;
                    parts[pc++] = p;
                    continue;
                }
                p++;
            }
            char* end = strchr(parts[pc-1], ']');
            if (end) *end = '\0';
            if (pc >= 1) safe_str_cpy(params->account_type, sizeof(params->account_type), parts[0]);
            if (pc >= 2) safe_str_cpy(params->account_email, sizeof(params->account_email), parts[1]);
            if (pc >= 3) safe_str_cpy(params->account_pass, sizeof(params->account_pass), parts[2]);
            if (pc >= 4) safe_str_cpy(params->account_server, sizeof(params->account_server), parts[3]);
        }
        else if (str_cmp(argv[i], "-usertype") == 0 && i+1 < argc) {
            params->user_type_index = str_to_int(argv[++i]);
        }
        else if (str_cmp(argv[i], "-java_home") == 0 && i+1 < argc) {
            safe_str_cpy(params->java_home, sizeof(params->java_home), argv[++i]);
            str_trim_quotes(params->java_home);
        }
        else if (str_cmp(argv[i], "-no_authlib") == 0) {
            params->no_authlib = 1;
        }
        else if (str_cmp(argv[i], "-mem") == 0 && i+1 < argc) {
            char* mem_str = argv[++i];
            int len = strlen(mem_str);
            if (mem_str[len-1] == 'G' || mem_str[len-1] == 'g') {
                params->memory_mb = atoi(mem_str) * 1024;
            } else {
                params->memory_mb = atoi(mem_str);
                if (params->memory_mb == 0 && mem_str[len-1] == 'M') params->memory_mb = atoi(mem_str);
            }
        }
        else if (str_cmp(argv[i], "-jvm_args") == 0 && i+1 < argc) {
            safe_str_cpy(params->extra_jvm_args, sizeof(params->extra_jvm_args), argv[++i]);
            str_trim_quotes(params->extra_jvm_args);
        }
        else if (str_cmp(argv[i], "-game_args") == 0 && i+1 < argc) {
            safe_str_cpy(params->extra_game_args, sizeof(params->extra_game_args), argv[++i]);
            str_trim_quotes(params->extra_game_args);
        }
        else if (str_cmp(argv[i], "-pre_command") == 0 && i+1 < argc) {
            safe_str_cpy(params->pre_command, sizeof(params->pre_command), argv[++i]);
            str_trim_quotes(params->pre_command);
        }
        else if (str_cmp(argv[i], "-window_title") == 0 && i+1 < argc) {
            safe_str_cpy(params->window_title, sizeof(params->window_title), argv[++i]);
            str_trim_quotes(params->window_title);
        }
        else if (str_cmp(argv[i], "-no_verify") == 0) {
            params->skip_verify = 1;
        }
        else if (str_cmp(argv[i], "-java") == 0) {
            params->use_java = 1;
        }
    }
    if (params->memory_mb > 0) {
        char new_jvm[512];
        safe_str_cpy(new_jvm, sizeof(new_jvm), "-Xmx");
        char mem_str[16];
        int_to_str(params->memory_mb, mem_str, sizeof(mem_str));
        safe_str_cat(new_jvm, sizeof(new_jvm), mem_str);
        safe_str_cat(new_jvm, sizeof(new_jvm), "M -Xms");
        int xms = params->memory_mb / 2;
        if (xms < 256) xms = 256;
        int_to_str(xms, mem_str, sizeof(mem_str));
        safe_str_cat(new_jvm, sizeof(new_jvm), mem_str);
        safe_str_cat(new_jvm, sizeof(new_jvm), "M");
        safe_str_cpy(jvm_args, sizeof(jvm_args), new_jvm);
        write_config("JVM_ARGS", jvm_args);
    }
    if (str_len(params->extra_jvm_args) > 0) {
        safe_str_cat(jvm_args, sizeof(jvm_args), " ");
        safe_str_cat(jvm_args, sizeof(jvm_args), params->extra_jvm_args);
    }
}

void start_mc(int argc, char** argv) {
    StartParams params;
    parse_start_params(argc, argv, &params);

    print("=== Start Parameters ===\n");
    print("  Version: "); print(str_len(params.version) > 0 ? params.version : "(default)"); print("\n");
    print("  Java: "); print(str_len(params.java_home) > 0 ? params.java_home : "(default)"); print("\n");
    print("  Account type: "); print(str_len(params.account_type) > 0 ? params.account_type : "(default)"); print("\n");
    print("  Account email: "); print(str_len(params.account_email) > 0 ? params.account_email : "(default)"); print("\n");
    print("  Skip verify: "); print(params.skip_verify ? "yes" : "no"); print("\n");
    print("  Use java.exe: "); print(params.use_java ? "yes" : "no"); print("\n");
    print("  No authlib: "); print(params.no_authlib ? "yes" : "no"); print("\n");
    if (str_len(params.extra_jvm_args) > 0) {
        print("  Extra JVM: "); print(params.extra_jvm_args); print("\n");
    }
    if (str_len(params.extra_game_args) > 0) {
        print("  Extra game: "); print(params.extra_game_args); print("\n");
    }
    print("\n");

    // 执行启动前命令
    if (str_len(params.pre_command) > 0) {
        print("Executing pre-command: "); print(params.pre_command); print("\n");
        system(params.pre_command);
    }

    // 确定版本
    char version[64];
    if (str_len(params.version) > 0) {
        if (!version_exists(params.version)) {
            print("Version '"); print(params.version); print("' not found locally, downloading...\n");
            if (!download_version_json(params.version)) {
                print("Error: Failed to download version '"); print(params.version); print("'.\n");
                return;
            }
        }
        safe_str_cpy(version, sizeof(version), params.version);
    } else if (str_len(default_ver) > 0) {
        safe_str_cpy(version, sizeof(version), default_ver);
    } else {
        int v_idx = select_version_interactive();
        if (v_idx < 0) return;
        char dir[MAX_PATH_LEN];
        safe_str_cpy(dir, sizeof(dir), mc_path);
        safe_str_cat(dir, sizeof(dir), PATH_SEP_STR "versions");
        char vnames[64][MAX_PATH_LEN];
        int vcount = list_subdirs(dir, vnames, 64);
        if (v_idx >= 0 && v_idx < vcount) safe_str_cpy(version, sizeof(version), vnames[v_idx]);
        else safe_str_cpy(version, sizeof(version), "");
        if (str_len(version) == 0) return;
    }

    // 确定 Java 路径
    char java_path[MAX_PATH_LEN];
    if (str_len(params.java_home) > 0) {
        safe_str_cpy(java_path, sizeof(java_path), params.java_home);
        // Add custom java to java_list if not already present
        int found = 0;
        for (int i = 0; i < java_count; i++) {
            if (str_cmp(java_list[i].path, java_path) == 0) { found = 1; break; }
        }
        if (!found && java_count < MAX_JAVA) {
            safe_str_cpy(java_list[java_count].path, sizeof(java_list[java_count].path), java_path);
            // Detect java version
            char ver_cmd[MAX_PATH_LEN + 64];
            safe_str_cpy(ver_cmd, sizeof(ver_cmd), java_path);
            safe_str_cat(ver_cmd, sizeof(ver_cmd), PATH_SEP_STR "bin" PATH_SEP_STR JAVA_EXE " -version 2>&1");
            char ver_buf[256] = {0};
            exec_cmd(ver_cmd, ver_buf, sizeof(ver_buf));
            safe_str_cpy(java_list[java_count].version, sizeof(java_list[java_count].version), ver_buf);
            java_list[java_count].major = parse_java_major_version(ver_buf);
            java_list[java_count].valid = 1;
            java_count++;
        }
    } else if (str_len(default_java_path) > 0) {
        safe_str_cpy(java_path, sizeof(java_path), default_java_path);
    } else {
        int j_idx = select_java_interactive();
        if (j_idx < 0) return;
        safe_str_cpy(java_path, sizeof(java_path), java_list[j_idx].path);
    }
    safe_str_cpy(default_java_path, sizeof(default_java_path), java_path);
    write_config("DEFAULT_JAVA", default_java_path);

    // 确定账号
    int acc_idx = -1;
    // First check if user_type_index is specified
    if (params.user_type_index >= 0 && params.user_type_index < account_count) {
        acc_idx = params.user_type_index;
    } else if (str_len(params.account_type) > 0) {
        // 尝试登录新账号
        if (str_cmp(params.account_type, "offline") == 0 && str_len(params.account_email) > 0) {
            AccountInfo new_acc;
            memset(&new_acc, 0, sizeof(new_acc));
            safe_str_cpy(new_acc.type, sizeof(new_acc.type), "offline");
            safe_str_cpy(new_acc.username, sizeof(new_acc.username), params.account_email);
            safe_str_cpy(new_acc.email, sizeof(new_acc.email), params.account_email);
            safe_str_cpy(new_acc.accessToken, sizeof(new_acc.accessToken), "offline_token");
            unsigned int hash = 0;
            const char* name = new_acc.username;
            for (int i = 0; name[i]; i++) hash = name[i] + (hash << 6) + (hash << 16) - hash;
            snprintf(new_acc.uuid, sizeof(new_acc.uuid), "%08x-0000-0000-0000-000000000000", hash);
            new_acc.is_default = 1;
            save_account(&new_acc);
            for (int i = 0; i < account_count; i++) {
                if (str_cmp(accounts[i].username, new_acc.username) == 0 && str_cmp(accounts[i].type, "offline") == 0) {
                    acc_idx = i;
                    break;
                }
            }
        } else if (str_cmp(params.account_type, "external") == 0 && str_len(params.account_email) > 0 && str_len(params.account_pass) > 0 && str_len(params.account_server) > 0) {
            AccountInfo new_acc;
            memset(&new_acc, 0, sizeof(new_acc));
            safe_str_cpy(new_acc.type, sizeof(new_acc.type), "external");
            safe_str_cpy(new_acc.email, sizeof(new_acc.email), params.account_email);
            safe_str_cpy(new_acc.password, sizeof(new_acc.password), params.account_pass);
            safe_str_cpy(new_acc.server, sizeof(new_acc.server), params.account_server);
            print("Authenticating with external account...\n");
            char username[64], uuid[64], token[128];
            if (yggdrasil_authenticate(new_acc.server, new_acc.email, new_acc.password, username, uuid, token)) {
                safe_str_cpy(new_acc.username, sizeof(new_acc.username), username);
                safe_str_cpy(new_acc.uuid, sizeof(new_acc.uuid), uuid);
                safe_str_cpy(new_acc.accessToken, sizeof(new_acc.accessToken), token);
                new_acc.player_id_count = 1;
                safe_str_cpy(new_acc.player_ids[0], 64, username);
                new_acc.is_default = 1;
                save_account(&new_acc);
                for (int i = 0; i < account_count; i++) {
                    if (str_cmp(accounts[i].username, new_acc.username) == 0 && str_cmp(accounts[i].type, "external") == 0) {
                        acc_idx = i;
                        break;
                    }
                }
            } else {
                print("External login failed.\n");
                return;
            }
        } else {
            // 尝试从已有账号匹配
            for (int i = 0; i < account_count; i++) {
                if (str_cmp(accounts[i].type, params.account_type) == 0) {
                    if (str_len(params.account_email) == 0 || str_cmp(accounts[i].email, params.account_email) == 0) {
                        acc_idx = i;
                        break;
                    }
                }
            }
        }
    }
    if (acc_idx == -1) {
        acc_idx = select_account_interactive("");
        if (acc_idx == -1) return;
    }

    // 设置全局自定义路径
    if (str_len(params.java_home) > 0) {
        safe_str_cpy(custom_java_path, sizeof(custom_java_path), params.java_home);
    }
    use_java_exe = params.use_java;

    // authlib-injector: 默认启用（外置登录时），除非使用 -no_authlib
    if (!params.no_authlib) {
        char authlib_path[MAX_PATH_LEN];
        safe_str_cpy(authlib_path, sizeof(authlib_path), mc_path);
        safe_str_cat(authlib_path, sizeof(authlib_path), PATH_SEP_STR);
        safe_str_cat(authlib_path, sizeof(authlib_path), AUTHLIB_JAR_NAME);
        if (!file_exists(authlib_path)) {
            if (download_authlib_injector(authlib_path, sizeof(authlib_path))) {
                safe_str_cpy(custom_authlib_path, sizeof(custom_authlib_path), authlib_path);
            }
        } else {
            safe_str_cpy(custom_authlib_path, sizeof(custom_authlib_path), authlib_path);
        }
    }

    // 启动游戏
    safe_str_cpy(window_title, sizeof(window_title), params.window_title);
    start_game(version, &accounts[acc_idx], params.skip_verify);

    // 额外游戏参数等暂未实现，因为游戏参数通常已包含在版本 JSON 中
    if (str_len(params.extra_game_args) > 0) {
        print("Note: Extra game arguments are not automatically added, please use -game_args to modify launch command.\n");
    }
}

// ==================== 其他命令 ====================
void quick_start() {
    if (str_len(default_ver) == 0) {
        print("No default version set! Use 'mc -setver <version>' first.\n");
        return;
    }
    int acc_idx = -1;
    for (int i = 0; i < account_count; i++) if (accounts[i].is_default) { acc_idx = i; break; }
    if (acc_idx == -1) {
        print("No default account set! Please login first.\n");
        return;
    }
    start_game(default_ver, &accounts[acc_idx], 0);
}

void export_start_script(const char* ver) {
    if (!ver || str_len(ver) == 0) ver = default_ver;
    if (str_len(ver) == 0) { print("Please set default version first!\n"); return; }
    int acc_idx = select_account_interactive("");
    if (acc_idx == -1) return;
    char cmd[8192];
    if (!build_command(ver, &accounts[acc_idx], cmd, sizeof(cmd))) return;
#ifdef PLATFORM_WINAPI
    const char* script_name = "start_mc.bat";
#else
    const char* script_name = "start_mc.sh";
#endif
    FILE* hFile = fopen(script_name, "wb");
    if (!hFile) { print("Failed to create start script!\n"); return; }
    char content[8192];
    safe_str_cpy(content, sizeof(content), "@echo off\n");
    safe_str_cat(content, sizeof(content), "echo Starting Minecraft ");
    safe_str_cat(content, sizeof(content), ver);
    safe_str_cat(content, sizeof(content), "...\n");
    safe_str_cat(content, sizeof(content), cmd);
    safe_str_cat(content, sizeof(content), "\n");
    safe_str_cat(content, sizeof(content), "echo Minecraft started!\n");
    safe_str_cat(content, sizeof(content), "pause\n");
    fwrite(content, 1, strlen(content), hFile);
    fclose(hFile);
    print("Start script exported to "); print(script_name); print("!\n");
}

void show_help() {
    print(
        "Core Commands:\n"
        "  -ver                Show version\n"
        "  -help               Show this help\n"
        "\nMC Path Management:\n"
        "  -mcpath <path>      Set Minecraft directory\n"
        "  -lv                 List MC versions (with mod loader)\n"
        "  -setver <ver>       Set default version\n"
        "\nJava Management:\n"
        "  -j -au              Auto scan Java\n"
        "  -j -list            List all Java\n"
        "\nAccount Management:\n"
        "  -u -l               Show login help\n"
        "  -u -l offline <username> [-uuid <uuid>]\n"
        "  -u -l external <api_root> <email> <password>\n"
        "  -u -l official <email> <password> (reserved)\n"
        "  -u -list            List accounts\n"
        "  -u -del -usertype <index>  Delete account by index\n"
        "  -u -relogin -usertype <index>  Relogin external account\n"
        "\nLaunch Settings:\n"
        "  -set memory <Xms/Xmx>  Set memory\n"
        "  -set memory auto       Auto memory\n"
        "  -set jvm <args>        Custom JVM args\n"
        "\nLaunch Game:\n"
        "  -start               Interactive startup\n"
        "  -start [options]     Advanced startup with:\n"
        "    -ver <name>           Specify version\n"
        "    -account [type,email,pass,server]  Account info\n"
        "    -usertype <index>     Use account by index number\n"
        "    -java_home <path>     Java installation path\n"
        "    -no_authlib           Disable authlib-injector (enabled by default for external login)\n"
        "    -mem <size>           Memory in MB or G (e.g. 2048M, 2G)\n"
        "    -jvm_args <args>      Extra JVM arguments\n"
        "    -game_args <args>     Extra game arguments\n"
        "    -pre_command <cmd>    Command to run before launch\n"
        "    -window_title <title> Set console/game window title\n"
        "    -no_verify            Skip file verification and download\n"
        "    -java                 Use java.exe instead of javaw.exe (shows console output)\n"
        "  -s                   Quick start (default settings)\n"
        "  -printstart <ver>    Export start_mc.bat\n"
        "\nDownload:\n"
        "  -download            Download game versions and mod loaders\n"
        "    -ver <type> <ver>     Download version (release/snapshot/old_version/april_fools)\n"
        "    -mod_loader <loader> [ver]  Install mod loader (Forge/Fabric/Quilt/NeoForge/Liteloader)\n"
        "    -ver_list <type>      List available versions\n"
        "    -mod_loader_list <loader>  List mod loader versions\n"
    );
}

int parse_args(char* cmd, char** argv, int max) {
    int argc = 0, in_quote = 0, i = 0, len = str_len(cmd);
    while (i < len && argc < max - 1) {
        while (i < len && (cmd[i] == ' ' || cmd[i] == '\t') && !in_quote) i++;
        if (i >= len) break;
        argv[argc++] = &cmd[i];
        while (i < len) {
            if (cmd[i] == '"' || cmd[i] == '\'') {
                in_quote = !in_quote;
                int j; for (j = i; j < len; j++) cmd[j] = cmd[j + 1];
                len--;
            } else if ((cmd[i] == ' ' || cmd[i] == '\t') && !in_quote) {
                cmd[i] = '\0'; i++; break;
            } else i++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

int main(int argc, char** argv) {
    // 禁用 stdout 缓冲，确保管道捕获时实时输出
    setvbuf(stdout, NULL, _IONBF, 0);

    // 获取启动器所在目录
#ifdef PLATFORM_WINAPI
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    GetModuleFileNameA(NULL, launcher_dir, sizeof(launcher_dir));
    char* last_slash = strrchr(launcher_dir, PATH_SEP);
    if (last_slash) *last_slash = '\0';
#else
    if (argc > 0 && argv[0] && argv[0][0]) {
        safe_str_cpy(launcher_dir, sizeof(launcher_dir), argv[0]);
        char* ls = strrchr(launcher_dir, '/');
        if (ls) *ls = '\0';
        else safe_str_cpy(launcher_dir, sizeof(launcher_dir), ".");
    } else safe_str_cpy(launcher_dir, sizeof(launcher_dir), ".");
#endif

    // 加载配置
    if (!read_config("MC_PATH", mc_path, sizeof(mc_path))) {
#ifdef PLATFORM_WINAPI
        GetCurrentDirectoryA(MAX_PATH_LEN, mc_path);
#else
        if (getcwd(mc_path, MAX_PATH_LEN) == NULL) mc_path[0] = '\0';
#endif
        safe_str_cat(mc_path, sizeof(mc_path), PATH_SEP_STR ".minecraft");
        write_config("MC_PATH", mc_path);
    }
    read_config("DEFAULT_VERSION", default_ver, sizeof(default_ver));
    read_config("DEFAULT_JAVA", default_java_path, sizeof(default_java_path));
    read_config("JVM_ARGS", jvm_args, sizeof(jvm_args));

    // 加载账号
    int acc_idx = 0;
    char key[32], acc_buf[64];
    while (acc_idx < MAX_ACCOUNTS) {
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_USER");
        if (!read_config(key, accounts[acc_idx].username, sizeof(accounts[acc_idx].username))) break;
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_EMAIL");
        read_config(key, accounts[acc_idx].email, sizeof(accounts[acc_idx].email));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_TYPE");
        read_config(key, accounts[acc_idx].type, sizeof(accounts[acc_idx].type));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_SERVER");
        read_config(key, accounts[acc_idx].server, sizeof(accounts[acc_idx].server));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_PASSWORD");
        read_config(key, accounts[acc_idx].password, sizeof(accounts[acc_idx].password));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_TOKEN");
        read_config(key, accounts[acc_idx].accessToken, sizeof(accounts[acc_idx].accessToken));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_UUID");
        read_config(key, accounts[acc_idx].uuid, sizeof(accounts[acc_idx].uuid));
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_DEFAULT");
        accounts[acc_idx].is_default = (read_config(key, acc_buf, sizeof(acc_buf)) && str_cmp(acc_buf, "1") == 0);
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_PID_COUNT");
        accounts[acc_idx].player_id_count = read_config(key, acc_buf, sizeof(acc_buf)) ? str_to_int(acc_buf) : 0;
        for (int i = 0; i < accounts[acc_idx].player_id_count && i < MAX_PLAYER_IDS; i++) {
            make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_PID_");
            char pid[4]; int_to_str(i, pid, sizeof(pid)); str_cat(key, pid);
            read_config(key, accounts[acc_idx].player_ids[i], 64);
        }
        make_key(key, "ACCOUNT_", acc_idx); str_cat(key, "_PARAMS");
        read_config(key, accounts[acc_idx].custom_params, sizeof(accounts[acc_idx].custom_params));
        acc_idx++;
    }
    account_count = acc_idx;

    char default_account_buf[8];
    int default_account = read_config("DEFAULT_ACCOUNT", default_account_buf, sizeof(default_account_buf)) ? str_to_int(default_account_buf) : 1;
    if (default_account < 1) default_account = 1;

    // 加载 Java 列表
    int java_idx = 0;
    char temp_path[MAX_PATH_LEN];
    while (java_idx < MAX_JAVA) {
        make_key(key, "JAVA_", java_idx); str_cat(key, "_PATH");
        if (!read_config(key, temp_path, sizeof(temp_path))) break;
        int dup = 0;
        for (int i = 0; i < java_idx; i++) if (str_cmp(java_list[i].path, temp_path) == 0) { dup = 1; break; }
        if (!dup) {
            safe_str_cpy(java_list[java_idx].path, sizeof(java_list[java_idx].path), temp_path);
            make_key(key, "JAVA_", java_idx); str_cat(key, "_VER");
            read_config(key, java_list[java_idx].version, sizeof(java_list[java_idx].version));
            make_key(key, "JAVA_", java_idx); str_cat(key, "_MAJOR");
            int major = parse_java_major_version(java_list[java_idx].version);
            java_list[java_idx].major = major;
            char major_str[8]; int_to_str(major, major_str, sizeof(major_str));
            write_config(key, major_str);
            java_list[java_idx].valid = 1;
            java_idx++;
        }
    }
    java_count = java_idx;

    if (argc < 2) { show_help(); return 0; }

    if (str_cmp(argv[1], "-ver") == 0) { print("Tiny MC Launcher v260809-1 (Full cJSON)\n"); return 0; }
    if (str_cmp(argv[1], "-help") == 0) { show_help(); return 0; }
    if (str_cmp(argv[1], "-?") == 0) { show_help(); return 0; }
    if (str_cmp(argv[1], "-mcpath") == 0 && argc >= 3) { set_mc_path(argv[2]); return 0; }
    if (str_cmp(argv[1], "-lv") == 0) { list_mc_versions(); return 0; }
    if (str_cmp(argv[1], "-setver") == 0 && argc >= 3) { set_default_ver(argv[2]); return 0; }
    if (str_cmp(argv[1], "-j") == 0 && argc >= 3) {
        if (str_cmp(argv[2], "-au") == 0) auto_scan_java();
        else if (str_cmp(argv[2], "-list") == 0) {
            auto_scan_java();
            print("=== Java List ===\n");
            for (int i = 0; i < java_count; i++) {
                print("  ["); print_int(i+1); print("] ");
                print(java_list[i].path); print(" | "); print(java_list[i].version); print(" | major "); print_int(java_list[i].major); print("\n");
            }
        }
        return 0;
    }
    if (str_cmp(argv[1], "-u") == 0 || str_cmp(argv[1], "-user") == 0) {
        if (argc >= 3 && str_cmp(argv[2], "-l") == 0) login_account(argc, argv);
        else if (argc >= 3 && str_cmp(argv[2], "-list") == 0) list_accounts();
        else login_account(argc, argv);
        return 0;
    }
    if (str_cmp(argv[1], "-set") == 0) { set_launch_params(argc, argv); return 0; }
    if (str_cmp(argv[1], "-start") == 0) { start_mc(argc, argv); return 0; }
    if (str_cmp(argv[1], "-s") == 0) { quick_start(); return 0; }
    if (str_cmp(argv[1], "-printstart") == 0) { export_start_script(argc >= 3 ? argv[2] : ""); return 0; }
    if (str_cmp(argv[1], "-download") == 0) { download_mc(argc, argv); return 0; }
    print("Unknown command! Use -help for help.\n");
    return 0;
}