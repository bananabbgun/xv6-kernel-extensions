// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// Global variables for RAID 1 simulation (declared in proc.c)
extern int force_read_error_pbn;
extern int force_disk_fail_id;

struct
{
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head;
} bcache;

void binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // Create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
struct buf *bget(uint dev, uint blockno)
{
    struct buf *b;

    acquire(&bcache.lock);

    // Is the block already cached?
    for (b = bcache.head.next; b != &bcache.head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
    {
        if (b->refcnt == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    panic("bget: no buffers");
}

// TODO: RAID 1 simulation
// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;
    
    b = bget(dev, blockno);
    
    if (!b->valid)
    {
        // Check if we should simulate read failure and redirect to mirror
        int should_use_mirror = 0;
        
        // Only apply RAID logic if blockno is within the logical disk range
        if (blockno < LOGICAL_DISK_SIZE) {
            uint pbn0 = blockno;  // PBN0 (Disk 0)
            
            // Check if we should read from mirror due to simulated failure
            if (force_disk_fail_id == 0 || 
                (force_read_error_pbn != -1 && force_read_error_pbn == pbn0)) {
                should_use_mirror = 1;
            }
        }
        
        // Store original block number
        uint original_blockno = b->blockno;
        
        if (should_use_mirror) {
            // Read from mirror disk (Disk 1)
            b->blockno = blockno + DISK1_START_BLOCK;
        } else {
            // Normal read from primary disk (Disk 0)
            b->blockno = blockno;
        }
        
        virtio_disk_rw(b, 0);
        
        // Restore original block number
        b->blockno = original_blockno;
        b->valid = 1;
    }
    return b;
}

// TODO: RAID 1 simulation
// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    
    // Only apply RAID logic if blockno is within the logical disk range
    if (b->blockno >= LOGICAL_DISK_SIZE) {
        // For blocks outside logical range, just write normally
        virtio_disk_rw(b, 1);
        return;
    }
    
    // Calculate physical block numbers for RAID 1
    uint pbn0 = b->blockno;  // PBN0 (Disk 0)
    uint pbn1 = pbn0 + DISK1_START_BLOCK;  // PBN1 (Disk 1)
    
    // Read simulation flags
    int fail_disk = force_disk_fail_id;
    int pbn0_fail = (force_read_error_pbn != -1 && force_read_error_pbn == pbn0) ? 1 : 0;
    
    // Print diagnostic message
    printf("BW_DIAG: PBN0=%d, PBN1=%d, sim_disk_fail=%d, sim_pbn0_block_fail=%d\n",
           pbn0, pbn1, fail_disk, pbn0_fail);
    
    // Store original block number
    uint original_blockno = b->blockno;
    
    // Decide whether to attempt write to PBN0 (Disk 0)
    if (fail_disk == 0) {
        // Disk 0 is simulated as failed
        printf("BW_ACTION: SKIP_PBN0 (PBN %d) due to simulated Disk 0 failure.\n", pbn0);
    } else if (pbn0_fail) {
        // PBN0 is simulated as a bad block
        printf("BW_ACTION: SKIP_PBN0 (PBN %d) due to simulated PBN0 block failure.\n", pbn0);
    } else {
        // PBN0 is clear for writing
        printf("BW_ACTION: ATTEMPT_PBN0 (PBN %d).\n", pbn0);
        b->blockno = pbn0;
        virtio_disk_rw(b, 1);
    }
    
    // Decide whether to attempt write to PBN1 (Disk 1)
    if (fail_disk == 1) {
        // Disk 1 is simulated as failed
        printf("BW_ACTION: SKIP_PBN1 (PBN %d) due to simulated Disk 1 failure.\n", pbn1);
    } else {
        // PBN1 is clear for writing
        printf("BW_ACTION: ATTEMPT_PBN1 (PBN %d).\n", pbn1);
        b->blockno = pbn1;
        virtio_disk_rw(b, 1);
    }
    
    // Restore original block number
    b->blockno = original_blockno;
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    release(&bcache.lock);
}

void bpin(struct buf *b)
{
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

void bunpin(struct buf *b)
{
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}