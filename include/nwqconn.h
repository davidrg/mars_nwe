/* nwqconn.h 14-Apr-98 */
#ifndef _NWQCONN_H_
#define _NWQCONN_H_
#include "queuedef.h"
extern int creat_queue_job(int task,
                           uint32 q_id,
                           uint8 *queue_job, 
                           uint8 *responsedata,
                    	   uint8 old_call);

extern int close_queue_job(uint32 q_id, int job_id);
extern int close_queue_job2(uint32 q_id, int job_id,
                            uint8 *client_area,
                            uint8 *prc, int prc_len);

extern int service_queue_job(int task,
                    uint32 q_id,
                    uint8 *queue_job, 
                    uint8 *responsedata,
                    uint8 old_call);

extern int finish_abort_queue_job(uint32 q_id, int job_id);
extern uint32 get_queue_job_fhandle(uint32 q_id, int job_id);

extern void free_queue_jobs(void);

extern void free_connection_task_jobs(int task);
#endif
