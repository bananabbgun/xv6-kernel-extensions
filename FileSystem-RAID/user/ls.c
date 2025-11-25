#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char *fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

// Convert mode to string representation
void mode_to_string(int mode, char *str) {
    str[0] = (mode & M_READ) ? 'r' : '-';
    str[1] = (mode & M_WRITE) ? 'w' : '-';
    str[2] = '\0';
}

/* TODO: Access Control & Symbolic Link */
void ls(char *path)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // Step 1: 使用 O_NOACCESS 獲取 path 本身的 stat (不追蹤連結)
    int noaccess_fd = open(path, O_NOACCESS);
    if (noaccess_fd < 0) {
        printf("ls: cannot open %s\n", path);
        return;
    }
    
    if (fstat(noaccess_fd, &st) < 0) {
        printf("ls: cannot stat %s\n", path);
        close(noaccess_fd);
        return;
    }
    
    // 保存原始的 stat 資訊（用於符號連結的情況）
    struct stat orig_st = st;
    close(noaccess_fd);
    
    // Step 2: 判斷 st.type
    if (st.type == T_SYMLINK) {
        // Case 1: path 是符號連結
        fd = open(path, O_RDONLY);  // 這會追蹤連結
        if (fd < 0) {
            printf("ls: cannot open %s\n", path);
            return;
        }
        
        // 獲取目標的 stat
        struct stat target_st;
        if (fstat(fd, &target_st) < 0) {
            printf("ls: cannot stat %s\n", path);
            close(fd);
            return;
        }
        
        if (target_st.type == T_FILE) {
            // 符號連結指向檔案：顯示符號連結本身
            close(fd);
            char mode_str[3];
            mode_to_string(orig_st.mode, mode_str);
            printf("%s %d %d %d %s\n", fmtname(path), orig_st.type, orig_st.ino, (int)orig_st.size, mode_str);
            return;
        } else if (target_st.type == T_DIR) {
            // 符號連結指向目錄：列出目標目錄內容
            // 直接使用 path（符號連結名稱）來構建路徑
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n");
                close(fd);
                return;
            }
            
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0) continue;
                
                // 構建路徑：symlink_name/entry_name
                strcpy(buf, path);
                p = buf + strlen(buf);
                *p++ = '/';
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                
                // 讓 namei 處理符號連結的解析
                int temp_fd = open(buf, O_NOACCESS);
                if (temp_fd >= 0) {
                    struct stat child_stat;
                    if (fstat(temp_fd, &child_stat) >= 0) {
                        char mode_str[3];
                        mode_to_string(child_stat.mode, mode_str);
                        printf("%s %d %d %d %s\n", fmtname(de.name), child_stat.type, child_stat.ino, (int)child_stat.size, mode_str);
                    }
                    close(temp_fd);
                }
            }
        }
        close(fd);
        
    } else {
        // Case 2: path 不是符號連結（普通檔案或目錄）
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("ls: cannot open %s\n", path);
            return;
        }
        
        if (st.type == T_FILE) {
            // 顯示檔案資訊
            char mode_str[3];
            mode_to_string(st.mode, mode_str);
            printf("%s %d %d %d %s\n", fmtname(path), st.type, st.ino, (int)st.size, mode_str);
        } else if (st.type == T_DIR) {
            // 列出目錄內容
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n");
                close(fd);
                return;
            }
            
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0) continue;
                
                strcpy(buf, path);
                p = buf + strlen(buf);
                *p++ = '/';
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                
                int temp_fd = open(buf, O_NOACCESS);
                if (temp_fd >= 0) {
                    struct stat child_stat;
                    if (fstat(temp_fd, &child_stat) >= 0) {
                        char mode_str[3];
                        mode_to_string(child_stat.mode, mode_str);
                        printf("%s %d %d %d %s\n", fmtname(de.name), child_stat.type, child_stat.ino, (int)child_stat.size, mode_str);
                    }
                    close(temp_fd);
                }
            }
        }
        close(fd);
    }
}



int main(int argc, char *argv[])
{
    int i;

    if (argc < 2)
    {
        ls(".");
        exit(0);
    }
    for (i = 1; i < argc; i++)
        ls(argv[i]);
    exit(0);
}