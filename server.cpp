#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#include <stdint.h>
#include <io.h>
#include <process.h>
#include <time.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9999
#define BUFFER_SIZE 4096
#define SERVER_FILES_DIR "server_files"
#define MAX_CLIENTS 10
#define MAX_USER_SPACE (1024ULL * 1024 * 1024) // 1GB
#define USER_DB_FILE "user_database.txt"
#define MAX_LINE_LENGTH 1024

// 64 位网络字节序转换
uint64_t htonll(uint64_t val) {
    uint64_t result = 0;
    unsigned char *data = (unsigned char *)&result;
    data[0] = (val >> 56) & 0xFF;
    data[1] = (val >> 48) & 0xFF;
    data[2] = (val >> 40) & 0xFF;
    data[3] = (val >> 32) & 0xFF;
    data[4] = (val >> 24) & 0xFF;
    data[5] = (val >> 16) & 0xFF;
    data[6] = (val >> 8) & 0xFF;
    data[7] = val & 0xFF;
    return result;
}

uint64_t ntohll(uint64_t val) {
    unsigned char *data = (unsigned char *)&val;
    return ((uint64_t)data[0] << 56) |
           ((uint64_t)data[1] << 48) |
           ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) |
           ((uint64_t)data[4] << 24) |
           ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) |
           (uint64_t)data[7];
}

// 检查用户名是否合法
int is_valid_username(const char *name) {
    if (strlen(name) == 0 || strlen(name) > 50) return 0;
    const char *invalid_chars = "/\\:*?\"<>| ";
    for (int i = 0; name[i]; i++) {
        if (strchr(invalid_chars, name[i])) return 0;
    }
    return 1;
}

// 检查邮箱是否合法
int is_valid_email(const char *email) {
    if (strlen(email) == 0 || strlen(email) > 100) return 0;
    
    int at_count = 0;
    int dot_after_at = 0;
    
    for (int i = 0; email[i]; i++) {
        if (email[i] == '@') at_count++;
        if (at_count > 0 && email[i] == '.') {
            if (i > 0 && email[i-1] != '@') dot_after_at = 1;
        }
    }
    
    return (at_count == 1 && dot_after_at);
}

// 检查密码是否合法
int is_valid_password(const char *password) {
    if (strlen(password) < 6 || strlen(password) > 50) return 0;
    
    int has_upper = 0, has_lower = 0, has_digit = 0;
    for (int i = 0; password[i]; i++) {
        if (isupper(password[i])) has_upper = 1;
        if (islower(password[i])) has_lower = 1;
        if (isdigit(password[i])) has_digit = 1;
    }
    
    return (has_upper && has_lower && has_digit);
}

// 辅助函数：获取文件大小字符串
const char* get_file_size_string(uint64_t size) {
    static char buf[32];
    if (size > 1024 * 1024 * 1024) {
        sprintf(buf, "%.2f GB", (double)size / (1024 * 1024 * 1024));
    } else if (size > 1024 * 1024) {
        sprintf(buf, "%.2f MB", (double)size / (1024 * 1024));
    } else if (size > 1024) {
        sprintf(buf, "%.2f KB", (double)size / 1024);
    } else {
        sprintf(buf, "%llu B", (unsigned long long)size);
    }
    return buf;
}

// 检查用户是否存在
int user_exists(const char *username) {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        char db_username[256], db_email[256], db_password[256];
        sscanf(line, "%255[^|]|%255[^|]|%255[^\n]", 
               db_username, db_email, db_password);
        if (strcmp(db_username, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// 检查邮箱是否已注册
int email_exists(const char *email) {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        char db_username[256], db_email[256], db_password[256];
        sscanf(line, "%255[^|]|%255[^|]|%255[^\n]", 
               db_username, db_email, db_password);
        if (strcmp(db_email, email) == 0) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// 用户注册
int register_user(const char *username, const char *email, const char *password) {
    if (user_exists(username)) {
        return 0; // 用户已存在
    }
    
    if (email_exists(email)) {
        return 0; // 邮箱已注册
    }
    
    FILE *fp = fopen(USER_DB_FILE, "a");
    if (!fp) return 0;
    
    fprintf(fp, "%s|%s|%s\n", username, email, password);
    fclose(fp);
    
    // 创建用户目录
    char dir_path[512];
    sprintf(dir_path, "%s/%s", SERVER_FILES_DIR, username);
    _mkdir(dir_path);
    
    return 1;
}

// 用户登录验证
int verify_login(const char *username, const char *password) {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        char db_username[256], db_email[256], db_password[256];
        sscanf(line, "%255[^|]|%255[^|]|%255[^\n]", 
               db_username, db_email, db_password);
        
        if (strcmp(db_username, username) == 0 && 
            strcmp(db_password, password) == 0) {
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// 获取用户已用空间大小
uint64_t get_user_used_space(const char *username) {
    char path[512];
    sprintf(path, "%s/%s/*.*", SERVER_FILES_DIR, username);
    
    uint64_t total_size = 0;
    struct _finddata_t c_file;
    long hFile = _findfirst(path, &c_file);
    
    if (hFile != -1) {
        do {
            if (strcmp(c_file.name, ".") != 0 && strcmp(c_file.name, "..") != 0 && 
                !(c_file.attrib & _A_SUBDIR)) {
                total_size += c_file.size;
            }
        } while (_findnext(hFile, &c_file) == 0);
        _findclose(hFile);
    }
    
    return total_size;
}

// 获取用户空间使用情况字符串
void get_user_space_info(const char *username, char *info_buf, int buf_size) {
    uint64_t used_space = get_user_used_space(username);
    uint64_t free_space = (used_space > MAX_USER_SPACE) ? 0 : (MAX_USER_SPACE - used_space);
    double used_percent = (double)used_space * 100.0 / MAX_USER_SPACE;
    
    sprintf(info_buf, "用户: %s\n已用空间: %s / 1.00 GB\n可用空间: %s\n使用率: %.2f%%\n",
            username,
            get_file_size_string(used_space),
            get_file_size_string(free_space),
            used_percent);
}

// 检查用户是否有足够空间上传新文件
int check_user_space(const char *username, uint64_t new_file_size, char *error_msg, int msg_len) {
    uint64_t used_space = get_user_used_space(username);
    uint64_t total_needed = used_space + new_file_size;
    
    if (total_needed > MAX_USER_SPACE) {
        uint64_t free_space = MAX_USER_SPACE - used_space;
        sprintf(error_msg, "空间不足！需要: %s, 可用: %s", 
                get_file_size_string(new_file_size),
                get_file_size_string(free_space));
        return 0;
    }
    
    return 1;
}

// 线程参数结构体
typedef struct {
    SOCKET client_socket;
    struct sockaddr_in client_addr;
} ClientInfo;

// 处理客户端连接的线程函数
void client_thread(void *param) {
    ClientInfo *info = (ClientInfo *)param;
    SOCKET client_socket = info->client_socket;
    struct sockaddr_in client_addr = info->client_addr;
    
    char username[256] = {0};
    char buffer[BUFFER_SIZE];
    int bytes_received;
    char client_ip[16];
    
    strcpy(client_ip, inet_ntoa(client_addr.sin_addr));
    int client_port = ntohs(client_addr.sin_port);
    
    printf("[+] 客户端已连接: %s:%d\n", client_ip, client_port);
    
    // 释放参数内存
    free(info);
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            printf("[-] 客户端断开连接: %s\n", username[0] ? username : "unknown");
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // 检查命令类型
        if (strncmp(buffer, "register ", 9) == 0) {
            char reg_username[256], reg_email[256], reg_password[256];
            sscanf(buffer + 9, "%255s %255s %255s", reg_username, reg_email, reg_password);
            
            if (!is_valid_username(reg_username)) {
                send(client_socket, "ERROR: 用户名包含非法字符", 28, 0);
                continue;
            }
            
            if (!is_valid_email(reg_email)) {
                send(client_socket, "ERROR: 邮箱格式不正确", 24, 0);
                continue;
            }
            
            if (!is_valid_password(reg_password)) {
                send(client_socket, "ERROR: 密码至少6位，需包含大小写字母和数字", 50, 0);
                continue;
            }
            
            if (register_user(reg_username, reg_email, reg_password)) {
                send(client_socket, "REGISTER_SUCCESS", 16, 0);
            } else {
                send(client_socket, "ERROR: 用户名或邮箱已存在", 28, 0);
            }
            
        } else if (strncmp(buffer, "login ", 6) == 0) {
            char login_username[256], login_password[256];
            sscanf(buffer + 6, "%255s %255s", login_username, login_password);
            
            if (verify_login(login_username, login_password)) {
                strcpy(username, login_username);
                send(client_socket, "LOGIN_SUCCESS", 13, 0);
                printf("[%s] 用户登录成功\n", username);
            } else {
                send(client_socket, "ERROR: 用户名或密码错误", 26, 0);
            }
            
        } else if (strlen(username) == 0) {
            // 未登录状态，只能执行注册和登录
            send(client_socket, "ERROR: 请先登录", 18, 0);
            continue;
            
        } else if (strncmp(buffer, "list", 4) == 0) {
            char target_user[256] = {0};
            sscanf(buffer + 5, "%255s", target_user);
            
            if (strlen(target_user) == 0) {
                strcpy(target_user, username);
            }
            
            if (!is_valid_username(target_user)) {
                send(client_socket, "ERROR: 用户名无效", 20, 0);
                continue;
            }
            
            char user_path[512];
            sprintf(user_path, "%s/%s", SERVER_FILES_DIR, target_user);
            
            // 检查目录是否存在
            struct _stat st;
            if (_stat(user_path, &st) != 0) {
                char space_info[512];
                sprintf(space_info, "用户 %s 还没有任何文件\n空间限制: 1.00 GB\n可用空间: 1.00 GB (100.00%%)\n", target_user);
                
                send(client_socket, "OK", 2, 0);
                uint64_t len = strlen(space_info);
                uint64_t net_len = htonll(len);
                send(client_socket, (char*)&net_len, 8, 0);
                send(client_socket, space_info, len, 0);
                continue;
            }
            
            // 生成文件列表
            char file_list_text[BUFFER_SIZE * 10] = {0};
            struct _finddata_t c_file;
            long hFile;
            
            char space_info[256];
            get_user_space_info(target_user, space_info, sizeof(space_info));
            strcat(file_list_text, space_info);
            strcat(file_list_text, "==================================================\n");
            strcat(file_list_text, "文件名                    大小          修改时间\n");
            strcat(file_list_text, "==================================================\n");
            
            sprintf(user_path, "%s/%s/*.*", SERVER_FILES_DIR, target_user);
            hFile = _findfirst(user_path, &c_file);
            
            int file_count = 0;
            uint64_t total_size = 0;
            if (hFile != -1) {
                do {
                    if (strcmp(c_file.name, ".") != 0 && strcmp(c_file.name, "..") != 0 && 
                        !(c_file.attrib & _A_SUBDIR)) {
                        char line[512];
                        
                        struct tm *timeinfo;
                        time_t rawtime = c_file.time_write;
                        timeinfo = localtime(&rawtime);
                        char time_str[20];
                        if (timeinfo) {
                            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", timeinfo);
                        } else {
                            strcpy(time_str, "Unknown");
                        }
                        
                        sprintf(line, "%-20s  %10s  %s\n", 
                            c_file.name,
                            get_file_size_string(c_file.size),
                            time_str
                        );
                        strcat(file_list_text, line);
                        file_count++;
                        total_size += c_file.size;
                    }
                } while (_findnext(hFile, &c_file) == 0);
                _findclose(hFile);
            }
            
            if (file_count == 0) {
                strcat(file_list_text, "(无文件)\n");
            } else {
                char summary[256];
                sprintf(summary, "\n总计: %d 个文件, 总大小: %s\n", 
                        file_count, get_file_size_string(total_size));
                strcat(file_list_text, summary);
            }
            
            send(client_socket, "OK", 2, 0);
            uint64_t len = strlen(file_list_text);
            uint64_t net_len = htonll(len);
            send(client_socket, (char*)&net_len, 8, 0);
            send(client_socket, file_list_text, len, 0);
            
        } else if (strncmp(buffer, "save ", 5) == 0) {
            char user[256] = {0}, filename[256] = {0};
            sscanf(buffer + 5, "%255s %255s", user, filename);
            
            if (strlen(user) == 0 || strlen(filename) == 0) {
                send(client_socket, "ERROR: 命令格式错误", 22, 0);
                continue;
            }
            
            if (strcmp(user, username) != 0) {
                send(client_socket, "ERROR: 只能操作自己的文件", 28, 0);
                continue;
            }
            
            // 创建用户目录
            char path[512];
            sprintf(path, "%s/%s", SERVER_FILES_DIR, username);
            _mkdir(path);
            
            // 接收文件大小
            uint64_t file_size_net;
            int recv_result = recv(client_socket, (char*)&file_size_net, 8, 0);
            if (recv_result != 8) {
                send(client_socket, "ERROR: 接收文件大小失败", 28, 0);
                continue;
            }
            
            uint64_t file_size = ntohll(file_size_net);
            printf("[%s] 准备接收文件: %s, 大小: %s\n", 
                   username, filename, get_file_size_string(file_size));
            
            // 检查用户空间
            char space_error[256];
            if (!check_user_space(username, file_size, space_error, sizeof(space_error))) {
                send(client_socket, space_error, strlen(space_error), 0);
                continue;
            }
            
            // 发送确认，准备接收文件
            send(client_socket, "OK ready", 8, 0);
            
            char full_path[512];
            sprintf(full_path, "%s/%s/%s", SERVER_FILES_DIR, username, filename);
            
            FILE *fp = fopen(full_path, "wb");
            if (!fp) {
                send(client_socket, "ERROR: 无法创建文件", 22, 0);
                continue;
            }
            
            char file_buffer[BUFFER_SIZE];
            uint64_t received = 0;
            int success = 1;
            
            // 接收文件数据
            while (received < file_size) {
                uint64_t remaining = file_size - received;
                int to_recv = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
                int n = recv(client_socket, file_buffer, to_recv, 0);
                if (n <= 0) {
                    success = 0;
                    break;
                }
                fwrite(file_buffer, 1, n, fp);
                received += n;
            }
            
            fclose(fp);
            
            if (success && received == file_size) {
                struct _stat st;
                if (_stat(full_path, &st) == 0) {
                    uint64_t actual_size = st.st_size;
                    if (actual_size != file_size) {
                        send(client_socket, "ERROR: 文件大小不匹配", 24, 0);
                        remove(full_path);
                    } else {
                        printf("[%s] 文件保存成功: %s, 大小: %s\n", 
                               username, filename, get_file_size_string(file_size));
                        
                        char space_info[256];
                        get_user_space_info(username, space_info, sizeof(space_info));
                        char success_msg[512];
                        sprintf(success_msg, "SAVE_SUCCESS\n%s", space_info);
                        send(client_socket, success_msg, strlen(success_msg), 0);
                    }
                } else {
                    send(client_socket, "ERROR: 文件验证失败", 22, 0);
                }
            } else {
                send(client_socket, "ERROR: 文件传输不完整", 24, 0);
                remove(full_path);
            }
            
        } else if (strncmp(buffer, "load ", 5) == 0) {
            char user[256] = {0}, filename[256] = {0};
            sscanf(buffer + 5, "%255s %255s", user, filename);
            
            if (strlen(user) == 0 || strlen(filename) == 0) {
                send(client_socket, "ERROR: 命令格式错误", 22, 0);
                continue;
            }
            
            if (strcmp(user, username) != 0) {
                send(client_socket, "ERROR: 只能下载自己的文件", 28, 0);
                continue;
            }
            
            char full_path[512];
            sprintf(full_path, "%s/%s/%s", SERVER_FILES_DIR, user, filename);
            
            struct _stat st;
            if (_stat(full_path, &st) != 0) {
                send(client_socket, "ERROR: 文件不存在", 20, 0);
                continue;
            }
            
            uint64_t file_size = st.st_size;
            printf("[%s] 发送文件: %s, 大小: %s\n", 
                   user, filename, get_file_size_string(file_size));
            
            // 发送准备就绪
            send(client_socket, "OK ready", 8, 0);
            
            // 发送文件大小
            uint64_t file_size_net = htonll(file_size);
            send(client_socket, (char*)&file_size_net, 8, 0);
            
            // 发送文件数据
            FILE *fp = fopen(full_path, "rb");
            if (!fp) {
                send(client_socket, "ERROR: 无法打开文件", 22, 0);
                continue;
            }
            
            char file_buffer[BUFFER_SIZE];
            uint64_t sent = 0;
            
            while (sent < file_size) {
                uint64_t remaining = file_size - sent;
                int to_send = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
                int n = fread(file_buffer, 1, to_send, fp);
                if (n <= 0) break;
                int actual = send(client_socket, file_buffer, n, 0);
                if (actual <= 0) break;
                sent += actual;
            }
            
            fclose(fp);
            
            if (sent == file_size) {
                printf("[%s] 文件发送成功: %s\n", user, filename);
            } else {
                printf("[%s] 文件发送不完整: %s\n", user, filename);
            }
            
        } else if (strncmp(buffer, "space ", 6) == 0) {
            char user[256] = {0};
            sscanf(buffer + 6, "%255s", user);
            
            if (strlen(user) == 0) {
                strcpy(user, username);
            }
            
            if (!is_valid_username(user)) {
                send(client_socket, "ERROR: 用户名无效", 20, 0);
                continue;
            }
            
            char space_info[512];
            get_user_space_info(user, space_info, sizeof(space_info));
            
            send(client_socket, "OK", 2, 0);
            uint64_t len = strlen(space_info);
            uint64_t net_len = htonll(len);
            send(client_socket, (char*)&net_len, 8, 0);
            send(client_socket, space_info, len, 0);
            
        } else if (strncmp(buffer, "whoami", 6) == 0) {
            char response[256];
            sprintf(response, "当前用户: %s", username);
            send(client_socket, response, strlen(response), 0);
            
        } else if (strncmp(buffer, "logout", 6) == 0) {
            send(client_socket, "注销成功", 12, 0);
            break;
            
        } else if (strncmp(buffer, "exit", 4) == 0) {
            send(client_socket, "BYE", 3, 0);
            break;
            
        } else if (strncmp(buffer, "help", 4) == 0) {
            char help_msg[] = 
                "可用命令:\n"
                "  register username email password  - 注册用户\n"
                "  login username password          - 登录\n"
                "  save username filename           - 上传文件 (用户空间限制: 1GB)\n"
                "  load username filename           - 下载文件\n"
                "  list username                    - 查看文件列表和空间使用情况\n"
                "  space username                   - 查看用户空间使用情况\n"
                "  whoami                           - 显示当前登录用户\n"
                "  logout                           - 注销登录\n"
                "  exit                             - 退出\n"
                "  help                             - 显示帮助";
            
            send(client_socket, "OK", 2, 0);
            uint64_t len = strlen(help_msg);
            uint64_t net_len = htonll(len);
            send(client_socket, (char*)&net_len, 8, 0);
            send(client_socket, help_msg, len, 0);
            
        } else {
            send(client_socket, "ERROR: 未知命令", 18, 0);
        }
    }
    
    closesocket(client_socket);
    _endthread();
}

int main() {
    WSADATA wsaData;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);
    
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Winsock initialization failed. Error: %d\n", WSAGetLastError());
        return 1;
    }
    printf("Winsock initialized: version %d.%d\n", 
           LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("Socket creation failed. Error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    printf("Socket created successfully\n");
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    printf("Bind successful on port %d\n", PORT);
    
    listen(server_socket, 3);
    printf("=====================================\n");
    printf("多客户端文件传输服务器 1.0\n");
    printf("监听端口: %d\n", PORT);
    printf("文件存储路径: %s/\n", SERVER_FILES_DIR);
    printf("用户数据库: %s\n", USER_DB_FILE);
    printf("用户空间限制: 1.00 GB 每人\n");
    printf("支持多客户端同时连接\n");
    printf("等待客户端连接...\n");
    printf("=====================================\n");
    
    // 创建服务器目录
    _mkdir(SERVER_FILES_DIR);
    
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed. Error: %d\n", WSAGetLastError());
            continue;
        }
        
        ClientInfo *info = (ClientInfo *)malloc(sizeof(ClientInfo));
        if (info == NULL) {
            printf("Memory allocation failed\n");
            closesocket(client_socket);
            continue;
        }
        
        info->client_socket = client_socket;
        info->client_addr = client_addr;
        
        // 为每个客户端创建一个新线程
        HANDLE thread = (HANDLE)_beginthread(client_thread, 0, (void*)info);
        if (thread == (HANDLE)-1) {
            printf("Failed to create thread\n");
            free(info);
            closesocket(client_socket);
        } else {
            CloseHandle(thread);
        }
    }
    
    closesocket(server_socket);
    WSACleanup();
    return 0;
}
