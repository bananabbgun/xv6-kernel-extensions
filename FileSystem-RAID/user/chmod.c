#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int chmod_recursive(char *path, int mode) {
    struct stat st;
    int is_symlink_to_dir = 0;
    
    int fd = open(path, O_NOACCESS);
    if (fd < 0) {
        return -1;
    }
    
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    if (st.type == T_SYMLINK) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            // 無法開啟符號連結的目標，可能是權限問題
            return -1;  // 直接返回錯誤
        }
        
        struct stat target_st;
        if (fstat(fd, &target_st) < 0) {
            close(fd);
            return -1;
        }
        
        if (target_st.type == T_DIR) {
            is_symlink_to_dir = 1;
        }
        close(fd);
        
        // 如果是符號連結指向目錄，但無法訪問，也應該失敗
        if (is_symlink_to_dir) {
            // 再次檢查是否真的能夠列出目錄內容
            fd = open(path, O_RDONLY);
            if (fd < 0) {
                return -1;
            }
            close(fd);
        }
    }

    if (st.type == T_DIR || is_symlink_to_dir) {
        int will_have_read = (mode & M_READ) != 0;

        if (!will_have_read) {
            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                if (chmod(path, mode) < 0)
                    return -1;
                return 0;
            }

            struct dirent de;
            char buf[512], *p;
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
                close(fd);
                return -1;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';

            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0) 
                    continue;
                if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                    continue;

                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = '\0';
                if (chmod_recursive(buf, mode) < 0) {
                    close(fd);
                    return -1;
                }
            }
            close(fd);
            
            if (chmod(path, mode) < 0)
                return -1;
            return 0;
        } else {
            if (chmod(path, mode) < 0)
                return -1;

            int fd = open(path, O_RDONLY);
            if (fd < 0)
                return -1;

            struct dirent de;
            char buf[512], *p;
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
                close(fd);
                return -1;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';

            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0) 
                    continue;
                if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                    continue;

                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = '\0';
                if (chmod_recursive(buf, mode) < 0) {
                    close(fd);
                    return -1;
                }
            }
            close(fd);
            return 0;
        }
    } else {
        if (chmod(path, mode) < 0)
            return -1;
        return 0;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: chmod [-R] (+|-)(r|w|rw|wr) file_name|dir_name\n");
        exit(1);
    }
    
    int recursive = 0;
    int arg_idx = 1;
    
    if (argc >= 4 && strcmp(argv[1], "-R") == 0) {
        recursive = 1;
        arg_idx = 2;
    }
    
    char *mode_str = argv[arg_idx];
    char *path = argv[arg_idx + 1];
    
    if (strlen(mode_str) < 2) {
        printf("Usage: chmod [-R] (+|-)(r|w|rw|wr) file_name|dir_name\n");
        exit(1);
    }
    
    char operation = mode_str[0];
    char *perms = &mode_str[1];
    
    if (operation != '+' && operation != '-') {
        printf("Usage: chmod [-R] (+|-)(r|w|rw|wr) file_name|dir_name\n");
        exit(1);
    }
    
    int read_perm = 0, write_perm = 0;
    if (strcmp(perms, "r") == 0) {
        read_perm = 1;
    } else if (strcmp(perms, "w") == 0) {
        write_perm = 1;
    } else if (strcmp(perms, "rw") == 0 || strcmp(perms, "wr") == 0) {
        read_perm = 1;
        write_perm = 1;
    } else {
        printf("Usage: chmod [-R] (+|-)(r|w|rw|wr) file_name|dir_name\n");
        exit(1);
    }
    
    int temp_fd = open(path, O_NOACCESS);
    if (temp_fd < 0) {
        printf("chmod: cannot chmod %s\n", path);
        exit(1);
    }
    struct stat st;
    if (fstat(temp_fd, &st) < 0) {
        close(temp_fd);
        printf("chmod: cannot chmod %s\n", path);
        exit(1);
    }
    close(temp_fd);
        
    int new_mode = st.mode;
    
    if (operation == '+') {
        if (read_perm) new_mode |= M_READ;
        if (write_perm) new_mode |= M_WRITE;
    } else {
        if (read_perm) new_mode &= ~M_READ;
        if (write_perm) new_mode &= ~M_WRITE;
    }
    
    if (recursive) {
        if (chmod_recursive(path, new_mode) < 0) {
            printf("chmod: cannot chmod %s\n", path);
            exit(1);
        }
    } else {
        if (chmod(path, new_mode) < 0) {
            printf("chmod: cannot chmod %s\n", path);
            exit(1);
        }
    }
    
    exit(0);
}