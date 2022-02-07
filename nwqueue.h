/* nwqueue.h 14-Apr-98 */
#ifndef _NWQUEUE_H_
#define _NWQUEUE_H_
#include "queuedef.h"

extern int nw_get_q_dirname(uint32 q_id, uint8 *buff);

extern int nw_creat_queue_job(int connection, int task, uint32 object_id,
                       uint32 q_id, uint8 *q_job, uint8 *responsedata,
                       int old_call);

extern int nw_close_queue_job(uint32 q_id, int job_id, 
		      uint8 *responsedata);

extern int nw_get_queue_status(uint32 q_id,  int *status, int *entries, 
                 int *servers, int server_ids[], int server_conns[]);

extern int nw_get_q_job_entry(uint32 q_id, int job_id,  uint32 fhandle,
                       uint8 *responsedata, int old_call);
extern int nw_get_queue_job_list_old(uint32 q_id, uint8 *responsedata);
extern int nw_get_queue_job_file_size(uint32 q_id, int job_id);
extern int nw_change_queue_job_entry(uint32 q_id, uint8 *qjstruct);

extern int nw_remove_job_from_queue(uint32 user_id, uint32 q_id, int job_id);

extern void nw_close_connection_jobs(int connection, int task);


/* ------------------ for queue servers ------------------- */
extern int nw_attach_server_to_queue(uint32 user_id, 
                              int connection, 
                              uint32 q_id);

extern int nw_detach_server_from_queue(uint32 user_id, 
                                int connection, 
                                uint32 q_id);

extern int nw_service_queue_job(uint32 user_id, int connection, int task,
    			uint32 q_id, int job_typ, 
    			uint8 *responsedata, int old_call);	 

extern int nw_finish_abort_queue_job(int mode, uint32 user_id, int connection,
                       uint32 q_id, int job_id);

extern int nw_creat_queue(int q_typ, uint8 *q_name, int q_name_len, 
                   uint8 *path, int path_len, uint32 *q_id);

extern int nw_destroy_queue(uint32 q_id);

extern void exit_queues(void);
extern void init_queues(int entry18_flags_p);
#endif
