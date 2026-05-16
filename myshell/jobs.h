#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

/* ------------------------------------------------------------------ *
 * Job state
 * ------------------------------------------------------------------ */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

/* ------------------------------------------------------------------ *
 * Job descriptor
 *
 *   id      – user-visible job number (1-based).
 *   pgid    – process group id of the pipeline.
 *   cmdstr  – heap-allocated human-readable command string.
 *   state   – current state of the job.
 * ------------------------------------------------------------------ */
typedef struct Job {
    int        id;
    pid_t      pgid;
    char      *cmdstr;
    JobState   state;
    struct Job *next;   /* intrusive singly-linked list */
} Job;

/* ------------------------------------------------------------------ *
 * Public API
 * ------------------------------------------------------------------ */

/*
 * add_job - create a new job and insert it into the job list.
 *
 * pgid    process group id of the new pipeline.
 * cmdstr  command string (will be duplicated internally).
 * state   initial state (usually JOB_RUNNING or JOB_STOPPED).
 *
 * Returns the new Job pointer, or NULL on allocation failure.
 */
Job *add_job(pid_t pgid, const char *cmdstr, JobState state);

/*
 * find_job_by_id   - look up a job by its user-visible job number.
 * find_job_by_pgid - look up a job by process group id.
 *
 * Both return NULL if not found.
 */
Job *find_job_by_id(int id);
Job *find_job_by_pgid(pid_t pgid);

/*
 * remove_job - remove and free the job with the given id.
 * Does nothing if no such job exists.
 */
void remove_job(int id);

/*
 * print_jobs - print all non-DONE jobs to stdout in the format:
 *   [id] state   cmdstr
 */
void print_jobs(void);

/*
 * update_jobs - reap finished/stopped children with WNOHANG and update
 * job states accordingly.  Should be called from the SIGCHLD handler and
 * before printing the job list.
 */
void update_jobs(void);

/*
 * job_state_str - return a printable string for a JobState value.
 */
const char *job_state_str(JobState s);

#endif /* JOBS_H */
