#include "kernel/types.h"
#include "user/user.h"
#include "user/list.h"
#include "user/threads.h"
#include "user/threads_sched.h"
#include <limits.h>
#define NULL 0

/* default scheduling algorithm */
#ifdef THREAD_SCHEDULER_DEFAULT
struct threads_sched_result schedule_default(struct threads_sched_args args)
{
    struct thread *thread_with_smallest_id = NULL;
    struct thread *th = NULL;
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (thread_with_smallest_id == NULL || th->ID < thread_with_smallest_id->ID)
            thread_with_smallest_id = th;
    }

    struct threads_sched_result r;
    if (thread_with_smallest_id != NULL) {
        r.scheduled_thread_list_member = &thread_with_smallest_id->thread_list;
        r.allocated_time = thread_with_smallest_id->remaining_time;
    } else {
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
    }

    return r;
}
#endif

/* MP3 Part 1 - Non-Real-Time Scheduling */

// HRRN
#ifdef THREAD_SCHEDULER_HRRN
struct threads_sched_result schedule_hrrn(struct threads_sched_args args)
{
    struct threads_sched_result r;
    struct thread *selected_thread = NULL;
    int highest_ratio_numerator = 0;
    int highest_ratio_denominator = 1;  // 避免除以零
    int current_time = args.current_time;
    
    struct thread *th = NULL;
    list_for_each_entry(th, args.run_queue, thread_list) {
        // 計算等待時間 (current_time - arrival_time)
        int waiting_time = current_time - th->arrival_time;
        
        // 計算回應比率的分子和分母
        // 分子：waiting_time + burst_time
        // 分母：burst_time
        int ratio_numerator = waiting_time + th->processing_time;
        int ratio_denominator = th->processing_time;
        
        // 比較回應比率：(a/b > c/d) 等價於 (a*d > b*c)
        if (selected_thread == NULL || 
            ratio_numerator * highest_ratio_denominator > highest_ratio_numerator * ratio_denominator ||
            (ratio_numerator * highest_ratio_denominator == highest_ratio_numerator * ratio_denominator && 
             th->ID < selected_thread->ID)) {
            selected_thread = th;
            highest_ratio_numerator = ratio_numerator;
            highest_ratio_denominator = ratio_denominator;
        }
    }
    
    // 如果找到要排程的執行緒
    if (selected_thread != NULL) {
        r.scheduled_thread_list_member = &selected_thread->thread_list;
        r.allocated_time = selected_thread->remaining_time;
    } else {
        // 如果執行佇列為空，返回佇列頭並分配 1 個時間單位
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
    }
    
    return r;
}
#endif

#ifdef THREAD_SCHEDULER_PRIORITY_RR
// priority Round-Robin(RR)
struct threads_sched_result schedule_priority_rr(struct threads_sched_args args)
{
    struct threads_sched_result r;

    /* 1. run queue 為空 → idle */
    if (list_empty(args.run_queue)) {
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
        return r;
    }

    /* 2. 找最小 priority */
    int best_pri = INT_MAX;
    struct thread *th;
    list_for_each_entry(th, args.run_queue, thread_list)
        if (th->priority < best_pri)
            best_pri = th->priority;

    /* 3. 找到“該群組的第一條 thread” */
    struct thread *chosen = NULL;
    list_for_each_entry(th, args.run_queue, thread_list)
        if (th->priority == best_pri) {
            chosen = th;
            break;
        }

    /* 4. 檢查同 group 的數量 */
    int group_cnt = 0;
    list_for_each_entry(th, args.run_queue, thread_list)
        if (th->priority == best_pri)
            ++group_cnt;

    /* 5. Round-Robin：只有在 group_cnt > 1 時才 move_tail */
    if (group_cnt > 1)
        list_move_tail(&chosen->thread_list, args.run_queue);

    /* 6. 決定 allocated_time */
    int quantum = 2;
    if (group_cnt == 1)          /* 唯一一條 → 跑到底 */
        r.allocated_time = chosen->remaining_time;
    else                         /* 否則用 RR quantum */
        r.allocated_time = chosen->remaining_time < quantum ?
                           chosen->remaining_time : quantum;

    r.scheduled_thread_list_member = &chosen->thread_list;
    return r;
}
#endif


/* MP3 Part 2 - Real-Time Scheduling*/

#if defined(THREAD_SCHEDULER_EDF_CBS) || defined(THREAD_SCHEDULER_DM)
static struct thread *__check_deadline_miss(struct list_head *run_queue, int current_time)
{
    struct thread *th = NULL;
    struct thread *thread_missing_deadline = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->current_deadline <= current_time) {
            if (thread_missing_deadline == NULL)
                thread_missing_deadline = th;
            else if (th->ID < thread_missing_deadline->ID)
                thread_missing_deadline = th;
        }
    }
    return thread_missing_deadline;
}
#endif

#ifdef THREAD_SCHEDULER_DM
/* 定義比較函數 */
static int __dm_thread_cmp(struct thread *a, struct thread *b)
{
    // 比較截止日期，返回 1 表示 a 優先於 b，-1 表示 b 優先於 a
    if (a->deadline < b->deadline)
        return 1;  // a 優先
    else if (a->deadline > b->deadline)
        return -1; // b 優先
    else {
        // 截止日期相同，比較 ID
        if (a->ID < b->ID)
            return 1;  // a 優先
        else
            return -1; // b 優先
    }
}

/* 主要排程函數 */
struct threads_sched_result schedule_dm(struct threads_sched_args args)
{
    struct threads_sched_result r;
    int current_time = args.current_time;

    // 1. 檢查是否有執行緒已經錯過截止日期
    struct thread *missed_deadline_thread = __check_deadline_miss(args.run_queue, current_time);
    if (missed_deadline_thread) {
        r.scheduled_thread_list_member = &missed_deadline_thread->thread_list;
        r.allocated_time = 0;  // 已經錯過截止日期，分配 0 時間
        return r;
    }

    // 2. 處理空佇列情況
    if (list_empty(args.run_queue)) {
        r.scheduled_thread_list_member = args.run_queue;

        struct release_queue_entry *entry;
        int earliest_arrival = INT_MAX;

        list_for_each_entry(entry, args.release_queue, thread_list) {
            if (entry->release_time < earliest_arrival) {
                earliest_arrival = entry->release_time;
            }
        }
        
        if (earliest_arrival != INT_MAX) {
            // 計算需要睡眠的時間 = 最早到達時間 - 當前時間
            int sleep_time = earliest_arrival - current_time;
            r.allocated_time = sleep_time > 0 ? sleep_time : 1;
        } else {
            // 如果 release_queue 也是空的，睡眠 1 tick
            r.allocated_time = 1;
        }
        
        return r;
    }

    // 3. 找出當前 run_queue 中截止日期最短的執行緒（最高優先權）
    struct thread *best_thread = NULL;
    struct thread *th = NULL;
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (best_thread == NULL || __dm_thread_cmp(th, best_thread) > 0) {
            best_thread = th;
        }
    }

    // 4. 檢查 release_queue 中是否有更高優先權的執行緒即將到達
    struct release_queue_entry *entry;
    struct release_queue_entry *next_arrival_entry = NULL;
    int next_arrival_time = INT_MAX;

    list_for_each_entry(entry, args.release_queue, thread_list) {
        // 找到最早到達的執行緒
        if (entry->release_time < next_arrival_time) {
            next_arrival_time = entry->release_time;
            next_arrival_entry = entry;
        }
    }

    // 5. 決定下一個要運行的執行緒和時間
    if (next_arrival_entry != NULL && next_arrival_time <= current_time) {
        // 有執行緒此刻應該被釋放，但還沒被釋放（這部分通常由 __release 函數處理）
        // 我們可以讓 scheduler 返回 1 tick 的時間，讓 __release 在下一個 tick 處理它
        r.scheduled_thread_list_member = &best_thread->thread_list;
        r.allocated_time = 1;
    } else if (next_arrival_entry != NULL && 
              next_arrival_time < current_time + best_thread->remaining_time) {
        // 有執行緒將在當前執行緒完成前到達
        struct thread *next_thread = next_arrival_entry->thrd;
        
        // 檢查到達的執行緒是否有更高優先權
        if (next_thread->deadline < best_thread->deadline || 
            (next_thread->deadline == best_thread->deadline && 
             next_thread->ID < best_thread->ID)) {
            
            // 將會被搶佔，只分配到下一個高優先權執行緒到達前的時間
            int time_until_preemption = next_arrival_time - current_time;
            r.scheduled_thread_list_member = &best_thread->thread_list;
            r.allocated_time = time_until_preemption > 0 ? time_until_preemption : 1;
        } else {
            // 不會被搶佔，分配所有剩餘時間
            r.scheduled_thread_list_member = &best_thread->thread_list;
            r.allocated_time = best_thread->remaining_time;
        }
    } else {
        // 沒有搶佔，分配所有剩餘時間
        r.scheduled_thread_list_member = &best_thread->thread_list;
        r.allocated_time = best_thread->remaining_time;
    }
    
    return r;
}
#endif



#ifdef THREAD_SCHEDULER_EDF_CBS
// EDF with CBS comparison
static int __edf_thread_cmp(struct thread *a, struct thread *b)
{
    // 比較截止日期，返回 1 表示 a 優先於 b，-1 表示 b 優先於 a
    if (a->current_deadline < b->current_deadline)
        return 1;  // a 優先
    else if (a->current_deadline > b->current_deadline)
        return -1; // b 優先
    else {
        // 截止日期相同，直接比較 ID (規格保證 Hard RT 的 ID 比 Soft 小)
        return a->ID < b->ID ? 1 : -1;
    }
}

// EDF_CBS scheduler
struct threads_sched_result schedule_edf_cbs(struct threads_sched_args args)
{
    struct threads_sched_result r;
    int current_time = args.current_time;

    // 1. 處理被節流的任務
    struct thread *th = NULL;
    list_for_each_entry(th, args.run_queue, thread_list) {
        // 如果任務是軟實時且被節流，檢查是否到達解除節流時間
        if (!th->cbs.is_hard_rt && th->cbs.is_throttled && current_time >= th->current_deadline) {
            // 重置預算並更新截止日期
            th->cbs.remaining_budget = th->cbs.budget;
            th->current_deadline = th->current_deadline + th->period;
            th->cbs.is_throttled = 0;
        }
    }

    // 2. 檢查是否有執行緒已經錯過截止日期
    struct thread *missed_deadline_thread = __check_deadline_miss(args.run_queue, current_time);
    if (missed_deadline_thread) {
        r.scheduled_thread_list_member = &missed_deadline_thread->thread_list;
        r.allocated_time = 0;  // 已經錯過截止日期，分配 0 時間
        return r;
    }

    // 3. 處理空佇列情況
    if (list_empty(args.run_queue)) {
        r.scheduled_thread_list_member = args.run_queue;

        struct release_queue_entry *entry;
        int earliest_arrival = INT_MAX;

        list_for_each_entry(entry, args.release_queue, thread_list) {
            if (entry->release_time < earliest_arrival) {
                earliest_arrival = entry->release_time;
            }
        }
        
        if (earliest_arrival != INT_MAX) {
            // 計算需要睡眠的時間 = 最早到達時間 - 當前時間
            int sleep_time = earliest_arrival - current_time;
            r.allocated_time = sleep_time > 0 ? sleep_time : 1;
        } else {
            // 如果 release_queue 也是空的，睡眠 1 tick
            r.allocated_time = 1;
        }
        
        return r;
    }

    list_for_each_entry(th, args.run_queue, thread_list) {
        if (!th->cbs.is_hard_rt && !th->cbs.is_throttled && th->cbs.remaining_budget <= 0) {
            th->cbs.is_throttled = 1;
            th->cbs.throttled_arrived_time = current_time;
        }
    }
    // 4. 找出截止日期最早的執行緒（不包括被節流的）
    struct thread *best_thread = NULL;

    /*list_for_each_entry(th, args.run_queue, thread_list) {
        // 跳過被節流的軟實時任務
        if (!th->cbs.is_hard_rt && th->cbs.is_throttled)
            continue;
            
        if (best_thread == NULL || __edf_thread_cmp(th, best_thread) > 0) {
            best_thread = th;
        }
    }*/

    // 5. 如果選中的是軟實時任務，檢查是否需要延長截止日期
    // 5. 檢查並處理所有軟實時任務的截止日期延長
    int changes_made;
    do {
        changes_made = 0; // 0 表示 false
        
        // 找出截止日期最早的執行緒（不包括被節流的）
        best_thread = NULL;
        list_for_each_entry(th, args.run_queue, thread_list) {
            //printf("111 ID: %d current deadline: %d\n", th->ID, th->current_deadline);
            // 跳過被節流的軟實時任務
            if (!th->cbs.is_hard_rt && th->cbs.is_throttled)
                continue;
            //printf("111 ID: %d current deadline: %d\n", th->ID, th->current_deadline);
            if (best_thread == NULL || __edf_thread_cmp(th, best_thread) > 0) {
                best_thread = th;
            }
        }
        
        // 如果所有任務都被節流，跳出循環
        if (best_thread == NULL) {
            break;
        }
        
        // 如果選中的是軟實時任務，檢查是否需要延長截止日期
        if (!best_thread->cbs.is_hard_rt && best_thread->cbs.remaining_budget > 0) {
            //printf("222 ID: %d current deadline: %d\n", best_thread->ID, best_thread->current_deadline);
            int time_until_deadline = best_thread->current_deadline - current_time;
            
            // 檢查是否違反帶寬約束
            if (time_until_deadline <= 0 || 
                best_thread->cbs.remaining_budget * best_thread->period > 
                best_thread->cbs.budget * time_until_deadline) {
                //printf("333 ID: %d\n", best_thread->ID);
                
                // 延長截止日期並重置預算
                best_thread->current_deadline = current_time + best_thread->period;
                best_thread->cbs.remaining_budget = best_thread->cbs.budget;
                
                // 標記已有變更，需要重新迭代
                changes_made = 1; // 1 表示 true
            }
        }
        //printf("changes_made = %d\n", changes_made);
    } while (changes_made);

    // 如果所有任務都被節流，返回 idle
    if (best_thread == NULL) {
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
        return r;
    }
    

    // 6. 設定回傳值
    r.scheduled_thread_list_member = &best_thread->thread_list;
    
    // 7. 決定分配的時間
    int allocated_time;
    
    // 首先檢查預算限制（僅針對軟實時任務）
    if (!best_thread->cbs.is_hard_rt) {
        /*if (best_thread->cbs.remaining_budget <= 0) {
            // 預算耗盡，應該被節流
            best_thread->cbs.is_throttled = 1;
            best_thread->cbs.throttled_arrived_time = current_time;
            r.scheduled_thread_list_member = args.run_queue;
            r.allocated_time = 1;
            return r;
        }*/
        
        // 分配時間不能超過剩餘預算
        allocated_time = best_thread->remaining_time < best_thread->cbs.remaining_budget ? 
                         best_thread->remaining_time : best_thread->cbs.remaining_budget;
    } else {
        allocated_time = best_thread->remaining_time;
    }
    
    // 8. 檢查所有可能的搶佔情況
    int min_preemption_time = allocated_time;

    // 8.1 檢查新到達的執行緒
    struct release_queue_entry *entry;
    list_for_each_entry(entry, args.release_queue, thread_list) {
        // 只考慮在當前分配時間內會到達的執行緒
        if (entry->release_time >= current_time && 
            entry->release_time < current_time + min_preemption_time) {
            
            struct thread *arrival_thread = entry->thrd;
            
            // 設置臨時截止日期用於比較
            int original_deadline = arrival_thread->current_deadline;
            arrival_thread->current_deadline = entry->release_time + arrival_thread->period;
            
            // 使用一致的比較函數
            if (__edf_thread_cmp(arrival_thread, best_thread) > 0) {
                // 更新搶佔時間點
                int time_until_arrival = entry->release_time - current_time;
                if (time_until_arrival < min_preemption_time) {
                    min_preemption_time = time_until_arrival > 0 ? time_until_arrival : 1;
                }
            }
            
            // 恢復原始截止日期
            arrival_thread->current_deadline = original_deadline;
        }
    }

    // 8.2 檢查會被重新填充預算的節流執行緒
    list_for_each_entry(th, args.run_queue, thread_list) {
        // 檢查被節流的軟實時任務
        if (!th->cbs.is_hard_rt && th->cbs.is_throttled) {
            // 檢查是否在當前分配時間內達到截止日期
            if (th->current_deadline >= current_time && 
                th->current_deadline < current_time + min_preemption_time) {
                
                // 保存原始狀態
                int original_throttled = th->cbs.is_throttled;
                int original_budget = th->cbs.remaining_budget;
                int original_deadline = th->current_deadline;
                
                // 模擬重新填充預算和更新截止日期
                th->cbs.is_throttled = 0;
                th->cbs.remaining_budget = th->cbs.budget;
                int new_deadline = th->current_deadline + th->period;
                th->current_deadline = new_deadline;
                
                // 比較優先級
                if (__edf_thread_cmp(th, best_thread) > 0 && th != best_thread) {
                    // 如果會搶佔，更新最小搶佔時間
                    int time_until_replenish = original_deadline - current_time;
                    if (time_until_replenish < min_preemption_time) {
                        min_preemption_time = time_until_replenish > 0 ? time_until_replenish : 1;
                    }
                }
                
                // 恢復原始狀態
                th->cbs.is_throttled = original_throttled;
                th->cbs.remaining_budget = original_budget;
                th->current_deadline = original_deadline;
            }
        }
    }

    // 更新分配時間為最小搶佔時間
    allocated_time = min_preemption_time;

    // 確保分配的時間至少為 1
    r.allocated_time = allocated_time > 0 ? allocated_time : 1;

    return r;
}
#endif