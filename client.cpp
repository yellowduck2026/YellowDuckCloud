#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <stdint.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT 9999
#define BUFFER_SIZE 4096

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

// 发送文件
void send_file(SOCKET sock, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("错误: 无法打开文件 %s\n", filename);
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    uint64_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    printf("文件大小: %llu 字节\n", (unsigned long long)file_size);
    
    char buffer[BUFFER_SIZE];
    uint64_t sent = 0;
    while (sent < file_size) {
        uint64_t remaining = file_size - sent;
        int to_send = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
        int n = fread(buffer, 1, to_send, fp);
        if (n <= 0) break;
        int actual = send(sock, buffer, n, 0);
        if (actual <= 0) break;
        sent += actual;
    }
    fclose(fp);
    
    if (sent == file_size) {
        printf("文件发送完成。\n");
    } else {
        printf("错误: 文件发送不完整。\n");
    }
}

// 接收文件
void recv_file(SOCKET sock, const char *filename) {
    // 接收文件大小
    uint64_t file_size_net;
    int bytes_received = recv(sock, (char *)&file_size_net, 8, 0);
    if (bytes_received != 8) {
        printf("错误: 接收文件大小失败\n");
        return;
    }
    uint64_t file_size = ntohll(file_size_net);
    printf("正在接收文件: %s, 大小: %llu 字节\n", filename, (unsigned long long)file_size);
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("错误: 无法创建文件 %s\n", filename);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    uint64_t received = 0;
    while (received < file_size) {
        uint64_t remaining = file_size - received;
        int to_recv = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
        int n = recv(sock, buffer, to_recv, 0);
        if (n <= 0) break;
        fwrite(buffer, 1, n, fp);
        received += n;
    }
    fclose(fp);
    
    if (received == file_size) {
        printf("文件接收成功。\n");
    } else {
        printf("错误: 文件接收不完整。\n");
        remove(filename);
    }
}

// 接收文本数据
int recv_text_data(SOCKET sock, char *buffer, int max_len) {
    char response[256];
    
    // 先接收状态码
    int len = recv(sock, response, 255, 0);
    if (len <= 0) {
        return -1;
    }
    response[len] = '\0';
    
    // 检查是否是错误响应
    if (strncmp(response, "ERROR", 5) == 0) {
        strncpy(buffer, response, max_len - 1);
        buffer[max_len - 1] = '\0';
        return len;
    }
    
    // 如果是OK，接收长度和数据
    if (strncmp(response, "OK", 2) == 0) {
        // 接收数据长度
        uint64_t data_len_net;
        if (recv(sock, (char*)&data_len_net, 8, 0) != 8) {
            return -1;
        }
        uint64_t data_len = ntohll(data_len_net);
        
        // 确保不超过缓冲区
        if (data_len >= (uint64_t)max_len) {
            data_len = max_len - 1;
        }
        
        // 接收数据
        uint64_t received = 0;
        while (received < data_len) {
            int n = recv(sock, buffer + received, data_len - received, 0);
            if (n <= 0) break;
            received += n;
        }
        buffer[received] = '\0';
        
        return (int)received;
    }
    
    // 其他响应
    strncpy(buffer, response, max_len - 1);
    buffer[max_len - 1] = '\0';
    return len;
}

// 连接服务器
SOCKET connect_to_server(const char *server_ip, int port) {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server;
    
    printf("初始化Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup失败. 错误: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("创建socket失败. 错误: %d\n", WSAGetLastError());
        WSACleanup();
        return INVALID_SOCKET;
    }
    
    server.sin_addr.s_addr = inet_addr(server_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    
    printf("正在连接服务器 %s:%d...\n", server_ip, port);
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("连接失败. 错误: %d\n", WSAGetLastError());
        printf("可能的原因:\n");
        printf("1. 服务器没有运行\n");
        printf("2. IP地址错误\n");
        printf("3. 防火墙阻止连接\n");
        printf("4. 服务器使用了不同的端口\n");
        closesocket(sock);
        WSACleanup();
        return INVALID_SOCKET;
    }
    
    printf("成功连接到服务器！\n");
    return sock;
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

// 注册用户
int register_user(SOCKET sock, const char *username, const char *email, const char *password) {
    char command[512];
    sprintf(command, "register %s %s %s", username, email, password);
    
    send(sock, command, strlen(command), 0);
    
    char response[256];
    int len = recv(sock, response, sizeof(response) - 1, 0);
    if (len <= 0) {
        return 0;
    }
    response[len] = '\0';
    
    if (strcmp(response, "REGISTER_SUCCESS") == 0) {
        printf("注册成功！\n");
        return 1;
    } else {
        printf("注册失败: %s\n", response);
        return 0;
    }
}

// 用户登录
int login_user(SOCKET sock, const char *username, const char *password) {
    char command[512];
    sprintf(command, "login %s %s", username, password);
    
    send(sock, command, strlen(command), 0);
    
    char response[256];
    int len = recv(sock, response, sizeof(response) - 1, 0);
    if (len <= 0) {
        return 0;
    }
    response[len] = '\0';
    
    if (strcmp(response, "LOGIN_SUCCESS") == 0) {
        printf("登录成功！\n");
        return 1;
    } else {
        printf("登录失败: %s\n", response);
        return 0;
    }
}

int main(int argc, char *argv[]) {
    SOCKET sock = INVALID_SOCKET;
    char server_ip[64] = "127.0.0.1";
    int port = DEFAULT_PORT;
    char username[100] = "";
    char command[256];
    char full_command[512];
    char response[BUFFER_SIZE];
    char cmd[10], param1[100], param2[100];
    int logged_in = 0;
    
    printf("=== 文件传输客户端 (支持用户注册登录) ===\n");
    printf("==========================================\n\n");
    
    // 处理命令行参数
    if (argc >= 2) {
        strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }
    
    // 如果没有命令行参数，提示用户输入
    if (argc == 1) {
        printf("请输入服务器IP地址 (默认 127.0.0.1): ");
        fgets(server_ip, sizeof(server_ip), stdin);
        server_ip[strcspn(server_ip, "\n")] = 0;
        if (strlen(server_ip) == 0) {
            strcpy(server_ip, "127.0.0.1");
        }
        
        printf("请输入服务器端口 (默认 %d): ", DEFAULT_PORT);
        char port_str[16];
        fgets(port_str, sizeof(port_str), stdin);
        if (strlen(port_str) > 0 && port_str[0] != '\n') {
            port = atoi(port_str);
        }
    }
    
    printf("\n服务器设置: %s:%d\n", server_ip, port);
    
    // 尝试连接服务器
    while (sock == INVALID_SOCKET) {
        sock = connect_to_server(server_ip, port);
        if (sock == INVALID_SOCKET) {
            printf("\n连接失败。是否重试？(y/n): ");
            char choice = getchar();
            while (getchar() != '\n');
            
            if (choice != 'y' && choice != 'Y') {
                printf("客户端退出。\n");
                return 1;
            }
            
            printf("\n重新输入服务器信息 (留空使用之前的设置):\n");
            printf("服务器IP (当前: %s): ", server_ip);
            char new_ip[64];
            fgets(new_ip, sizeof(new_ip), stdin);
            new_ip[strcspn(new_ip, "\n")] = 0;
            if (strlen(new_ip) > 0) {
                strcpy(server_ip, new_ip);
            }
            
            printf("端口 (当前: %d): ", port);
            char port_str[16];
            fgets(port_str, sizeof(port_str), stdin);
            if (strlen(port_str) > 0 && port_str[0] != '\n') {
                port = atoi(port_str);
            }
        }
    }
    
    printf("\n成功连接到服务器！\n");
    
    // 主菜单循环
    while (!logged_in) {
        printf("\n=== 欢迎使用文件传输系统 ===\n");
        printf("1. 注册\n");
        printf("2. 登录\n");
        printf("3. 退出\n");
        printf("============================\n");
        printf("请选择 (1-3): ");
        
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;
        
        if (strcmp(command, "1") == 0) {
            // 注册
            char reg_username[100], reg_email[100], reg_password[100];
            
            printf("\n=== 用户注册 ===\n");
            
            while (1) {
                printf("用户名: ");
                fgets(reg_username, sizeof(reg_username), stdin);
                reg_username[strcspn(reg_username, "\n")] = 0;
                
                if (!is_valid_username(reg_username)) {
                    printf("用户名包含非法字符！允许: 字母、数字、点、下划线、连字符\n");
                    printf("不允许: / \\ : * ? \" < > | 和空格\n");
                    continue;
                }
                break;
            }
            
            while (1) {
                printf("邮箱: ");
                fgets(reg_email, sizeof(reg_email), stdin);
                reg_email[strcspn(reg_email, "\n")] = 0;
                
                if (!is_valid_email(reg_email)) {
                    printf("邮箱格式不正确！示例: user@example.com\n");
                    continue;
                }
                break;
            }
            
            while (1) {
                printf("密码(至少6位，需包含大小写字母和数字): ");
                fgets(reg_password, sizeof(reg_password), stdin);
                reg_password[strcspn(reg_password, "\n")] = 0;
                
                if (!is_valid_password(reg_password)) {
                    printf("密码不符合要求！至少6位，需包含大小写字母和数字\n");
                    continue;
                }
                
                printf("确认密码: ");
                char confirm_password[100];
                fgets(confirm_password, sizeof(confirm_password), stdin);
                confirm_password[strcspn(confirm_password, "\n")] = 0;
                
                if (strcmp(reg_password, confirm_password) != 0) {
                    printf("两次输入的密码不一致！\n");
                    continue;
                }
                break;
            }
            
            if (register_user(sock, reg_username, reg_email, reg_password)) {
                printf("注册成功！请使用新账户登录。\n");
            }
            
        } else if (strcmp(command, "2") == 0) {
            // 登录
            char login_username[100], login_password[100];
            
            printf("\n=== 用户登录 ===\n");
            printf("用户名: ");
            fgets(login_username, sizeof(login_username), stdin);
            login_username[strcspn(login_username, "\n")] = 0;
            
            printf("密码: ");
            fgets(login_password, sizeof(login_password), stdin);
            login_password[strcspn(login_password, "\n")] = 0;
            
            if (login_user(sock, login_username, login_password)) {
                strcpy(username, login_username);
                logged_in = 1;
            }
            
        } else if (strcmp(command, "3") == 0) {
            send(sock, "exit", 4, 0);
            closesocket(sock);
            WSACleanup();
            printf("客户端退出。\n");
            return 0;
        } else {
            printf("无效的选择！\n");
        }
    }
    
    // 登录成功后显示功能菜单
    printf("\n=== 文件传输系统 ===\n");
    printf("当前用户: %s\n", username);
    printf("支持命令:\n");
    printf("  save filename          - 上传文件 (用户空间限制: 1GB)\n");
    printf("  load filename          - 下载文件\n");
    printf("  list                  - 查看我的文件列表和空间使用情况\n");
    printf("  space                 - 查看我的空间使用情况\n");
    printf("  whoami                - 显示当前登录用户\n");
    printf("  logout                - 注销登录\n");
    printf("  exit/quit             - 退出程序\n");
    printf("  help                  - 显示帮助信息\n");
    printf("==========================\n");
    
    // 主命令循环
    while (1) {
        printf("\n[%s] > ", username);
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        int parsed = sscanf(command, "%9s %99s %99s", cmd, param1, param2);
        
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            send(sock, "exit", 4, 0);
            recv_text_data(sock, response, sizeof(response));
            printf("Server: %s\n", response);
            printf("正在断开连接...\n");
            break;
        }
        
        if (strcmp(cmd, "logout") == 0) {
            send(sock, "logout", 6, 0);
            recv_text_data(sock, response, sizeof(response));
            printf("Server: %s\n", response);
            logged_in = 0;
            username[0] = '\0';
            printf("已注销登录。\n");
            break;
        }
        
        if (strcmp(cmd, "help") == 0) {
            send(sock, "help", 4, 0);
            int len = recv_text_data(sock, response, sizeof(response));
            if (len > 0) {
                printf("服务器帮助:\n%s\n", response);
            } else {
                printf("无法获取帮助信息。\n");
            }
            continue;
        }
        
        if (strcmp(cmd, "whoami") == 0) {
            send(sock, "whoami", 6, 0);
            memset(response, 0, sizeof(response));
            int len = recv(sock, response, sizeof(response) - 1, 0);
            if (len > 0) {
                response[len] = '\0';
                printf("%s\n", response);
            }
            continue;
        }
        
        // 构建完整的命令字符串（自动添加用户名）
        if (strcmp(cmd, "save") == 0) {
            if (parsed < 2) {
                printf("错误: 命令格式错误。使用: save filename\n");
                continue;
            }
            
            // 检查本地文件是否存在
            FILE *fp = fopen(param1, "rb");
            if (!fp) {
                printf("错误: 本地文件 '%s' 不存在\n", param1);
                continue;
            }
            
            fseek(fp, 0, SEEK_END);
            uint64_t file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            fclose(fp);
            
            // 发送命令: save username filename
            sprintf(full_command, "save %s %s", username, param1);
            
            // 发送命令
            send(sock, full_command, strlen(full_command), 0);
            
            // 发送文件大小
            uint64_t file_size_net = htonll(file_size);
            send(sock, (char*)&file_size_net, 8, 0);
            
            printf("准备上传文件: %s, 大小: %llu 字节\n", param1, (unsigned long long)file_size);
            
            // 等待服务器响应
            memset(response, 0, sizeof(response));
            int len = recv(sock, response, 8, 0);
            if (len > 0) {
                response[len] = '\0';
                
                if (strncmp(response, "OK ready", 8) == 0) {
                    // 服务器准备好接收文件
                    printf("开始上传文件...\n");
                    
                    // 重新打开文件并发送
                    fp = fopen(param1, "rb");
                    if (fp) {
                        char buffer[BUFFER_SIZE];
                        uint64_t sent = 0;
                        while (sent < file_size) {
                            uint64_t remaining = file_size - sent;
                            int to_send = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
                            int n = fread(buffer, 1, to_send, fp);
                            if (n <= 0) break;
                            int actual = send(sock, buffer, n, 0);
                            if (actual <= 0) break;
                            sent += actual;
                        }
                        fclose(fp);
                        
                        printf("文件发送完成。\n");
                        
                        // 接收服务器确认
                        memset(response, 0, sizeof(response));
                        len = recv_text_data(sock, response, sizeof(response));
                        if (len > 0) {
                            printf("Server: %s\n", response);
                        }
                    }
                } else {
                    printf("服务器响应: %s\n", response);
                }
            } else {
                printf("服务器未响应。\n");
            }
            
        } 
        else if (strcmp(cmd, "load") == 0) {
            if (parsed < 2) {
                printf("错误: 命令格式错误。使用: load filename\n");
                continue;
            }
            
            // 发送命令: load username filename
            sprintf(full_command, "load %s %s", username, param1);
            send(sock, full_command, strlen(full_command), 0);
            
            // 接收服务器响应
            memset(response, 0, sizeof(response));
            int len = recv(sock, response, 8, 0);
            if (len > 0) {
                response[len] = '\0';
                printf("Server: %s\n", response);
                
                if (strncmp(response, "OK ready", 8) == 0) {
                    recv_file(sock, param1);
                } else {
                    printf("服务器拒绝了 load 命令。\n");
                }
            }
            
        } else if (strcmp(cmd, "list") == 0) {
            // 发送命令: list username
            sprintf(full_command, "list %s", username);
            send(sock, full_command, strlen(full_command), 0);
            
            int len = recv_text_data(sock, response, sizeof(response));
            if (len > 0) {
                if (strncmp(response, "ERROR", 5) == 0) {
                    printf("错误: %s\n", response + 6);
                } else {
                    printf("%s\n", response);
                }
            } else {
                printf("无法接收响应。\n");
            }
            
        } else if (strcmp(cmd, "space") == 0) {
            // 发送命令: space username
            sprintf(full_command, "space %s", username);
            send(sock, full_command, strlen(full_command), 0);
            
            int len = recv_text_data(sock, response, sizeof(response));
            if (len > 0) {
                if (strncmp(response, "ERROR", 5) == 0) {
                    printf("错误: %s\n", response + 6);
                } else {
                    printf("%s\n", response);
                }
            } else {
                printf("无法接收响应。\n");
            }
            
        } else {
            printf("未知命令。输入 'help' 查看可用命令。\n");
        }
    }
    
    closesocket(sock);
    WSACleanup();
    printf("客户端已关闭。\n");
    printf("按Enter键退出...");
    getchar();
    
    return 0;
}
