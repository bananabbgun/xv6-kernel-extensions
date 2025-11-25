struct file
{
    enum
    {
        FD_NONE,
        FD_PIPE,
        FD_INODE,
        FD_DEVICE
    } type;
    int ref; // reference count
    char readable;
    char writable;
    struct pipe *pipe; // FD_PIPE
    struct inode *ip;  // FD_INODE and FD_DEVICE
    uint off;          // FD_INODE
    short major;       // FD_DEVICE
};

#define major(dev) ((dev) >> 16 & 0xFFFF)
#define minor(dev) ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// in-memory copy of an inode
struct inode
{
    uint dev;              // Device number
    uint inum;             // Inode number
    int ref;               // Reference count
    struct sleeplock lock; // protects everything below here
    int valid;             // inode has been read from disk?
    short type;            // copy of disk inode
    short major;
    short minor;           // For devices: minor number; For files: file mode
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1];
};

// Helper macros to access mode from minor field
#define inode_get_mode(ip) ((ip)->type == T_DEVICE ? 0 : (ip)->minor)
#define inode_set_mode(ip, mode) do { if ((ip)->type != T_DEVICE) (ip)->minor = (mode); } while(0)

// map major device number to device functions.
struct devsw
{
    int (*read)(int, uint64, int);
    int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1