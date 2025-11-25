# Advanced xv6 Kernel Extensions

![Language](https://img.shields.io/badge/language-C-blue)
![Platform](https://img.shields.io/badge/platform-RISC--V-orange)
![System](https://img.shields.io/badge/system-xv6-green)

Advanced kernel extensions for the xv6 (RISC-V) teaching OS, including a **slab memory allocator**, **real-time & non-real-time schedulers**, and a **software RAID-1–style file system with security extensions**.

All code in this repository is implemented and integrated by me as part of an advanced OS project.

---

## 📁 Repository Layout

> ⚠️ Note  
> This repository contains **selected kernel source files** for each subsystem, not a full standalone xv6 tree.
> The code is meant to showcase the implementation details and project structure.

- `Kernel-Memory/`  
  Slab allocator–based kernel memory management, replacing the default page allocator for kernel objects.

- `Threading-Scheduler/`  
  User-level threading library with multiple non-real-time and real-time scheduling policies (HRRN, priority RR, DM, EDF+CBS).

- `FileSystem-RAID/`  
  Extended xv6 file system with software RAID-1–style mirroring, disk-failure handling, symbolic links, and permission control (`chmod`).

Each directory contains the **core kernel and user-space source files** relevant to that subsystem.

---

## 🔧 Kernel Memory Management – Slab Allocator

Replaces the original page-level allocator with a **slab allocator** to better handle frequent allocations of fixed-size kernel objects.

- **Cache abstraction**  
  - Introduced kernel caches with APIs similar to `kmem_cache_create`, `kmem_cache_alloc`, and `kmem_cache_free`.  
  - Each cache manages objects of a single type/size (e.g., `struct proc`, buffer heads).

- **Slab structure & free lists**  
  - Objects are stored in slabs carved from physical pages.  
  - Maintains **free**, **partial**, and **full** slab lists for O(1) allocation and free.  
  - Minimizes internal fragmentation compared to the original page allocator.

- **Concurrency & correctness**  
  - Protects cache metadata and free lists with spinlocks for multi-core safety.  
  - Includes sanity checks to avoid double free and invalid pointer reuse.

---

## 🧵 Threading & Scheduling – User-Level Schedulers

Implements a user-level threading runtime and multiple scheduling policies on top of xv6.

- **User-level threading runtime**
  - Context switching implemented with `setjmp` / `longjmp`.  
  - Threads share a single process address space; the library manages stacks and TCBs.  
  - Timer-based preemption using signals to switch threads without kernel changes.

- **Non-real-time schedulers**
  - **Highest Response Ratio Next (HRRN)**  
    - Computes priority as `(waiting time + service time) / service time` to prevent starvation.  
    - Periodically updates priorities to age long-waiting threads.
  - **Priority-based Round Robin**  
    - Multiple ready queues by priority level.  
    - Time slicing within each level, with preemption when higher-priority threads arrive.

- **Real-time schedulers**
  - **Deadline-Monotonic (DM)**  
    - Fixed-priority preemptive scheduling based on task relative deadlines.  
    - Ensures tasks with shorter deadlines have higher priority.
  - **Earliest Deadline First (EDF) with Constant Bandwidth Server (CBS)**  
    - Schedules jobs by earliest absolute deadline.  
    - CBS enforces CPU bandwidth for each real-time task by tracking budget and deadlines, isolating soft real-time tasks from best-effort ones.

---

## 📦 File System Extensions – RAID-1 & Security

Extends the xv6 file system to improve reliability and access control.

### Software RAID-1–Style Mirroring

- **Mirrored writes**
  - Modifies the block I/O layer so each logical block write is duplicated to a primary and mirror disk.  
  - Ensures that both copies are updated atomically to keep them consistent.

- **Read fallback & failure handling**
  - Normal reads go to the primary disk.  
  - On simulated primary disk failure, reads transparently fall back to the mirror device.  
  - Supports fault-injection to test resiliency (e.g., skipping or corrupting blocks on the primary).

### Symbolic Links & Permission Control

- **Symbolic links**
  - Adds a `symlink` system call to create soft links.  
  - Path resolution follows symlink chains with loop-detection to avoid infinite recursion.

- **`chmod` and access checks**
  - Implements `chmod` to change file permission bits.  
  - Enforces read/write/execute checks on file operations based on user and group mode bits.

---

## 🛠️ Technical Stack

- **Languages:** C, RISC-V assembly  
- **Environment:** xv6-riscv, QEMU, Docker (for toolchain containerization)  
- **Tools:** `make`, `gdb`, `objdump`, standard Unix toolchain

---

## 📚 Learning Outcomes

Through this project, I worked hands-on with:

- Low-level kernel memory management and custom allocators  
- User-level threading, context switching, and multiple scheduling algorithms  
- Real-time scheduling theory (DM, EDF, CBS) implemented in practice  
- File system design, block I/O, and software redundancy for fault tolerance  
- Concurrency control with spinlocks and debugging subtle race conditions
