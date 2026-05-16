#include "jobs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ *
 * The job list is the only intentional global in the shell.
 * All access is through the functions below; no other file touches it
 * directly.
 * ------------------------------------------------------------------ */
static Job *job_list = NULL;  /* head of singly-linked list */
static int  next_id  = 1;     /* monotonically increasing job id counter */

/* ------------------------------------------------------------------ *
 * job_state_str
 * ------------------------------------------------------------------ */
const char *job_state_str(JobState s)
{
    switch (s) {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE:    return "Done";
    }
    return "Unknown";
}

/* ------------------------------------------------------------------ *
 * add_job
 * ------------------------------------------------------------------ */
Job *add_job(pid_t pgid, const char *cmdstr, JobState state)
{
    Job *j = malloc(sizeof(Job));
    if (!j) return NULL;

    j->cmdstr = malloc(strlen(cmdstr) + 1);
    if (!j->cmdstr) {
        free(j);
        return NULL;
    }
    strcpy(j->cmdstr, cmdstr);

    j->id    = next_id++;
    j->pgid  = pgid;
    j->state = state;
    j->next  = NULL;

    /* Append to tail so jobs print in creation order */
    if (job_list == NULL) {
        job_list = j;
    } else {
        Job *cur = job_list;
        while (cur->next) cur = cur->next;
        cur->next = j;
    }

    return j;
}

/* ------------------------------------------------------------------ *
 * find_job_by_id
 * ------------------------------------------------------------------ */
Job *find_job_by_id(int id)
{
    for (Job *j = job_list; j; j = j->next)
        if (j->id == id) return j;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * find_job_by_pgid
 * ------------------------------------------------------------------ */
Job *find_job_by_pgid(pid_t pgid)
{
    for (Job *j = job_list; j; j = j->next)
        if (j->pgid == pgid) return j;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * remove_job
 * ------------------------------------------------------------------ */
void remove_job(int id)
{
    Job *prev = NULL;
    Job *cur  = job_list;

    while (cur) {
        if (cur->id == id) {
            if (prev)
                prev->next = cur->next;
            else
                job_list = cur->next;

            free(cur->cmdstr);
            free(cur);
            return;
        }
        prev = cur;
        cur  = cur->next;
    }
}

/* ------------------------------------------------------------------ *
 * print_jobs
 *
 * Prints all jobs that are not DONE.  Format:
 *   [1] Running    sleep 10
 *   [2] Stopped    vim file.c
 * ------------------------------------------------------------------ */
void print_jobs(void)
{
    for (Job *j = job_list; j; j = j->next) {
        if (j->state != JOB_DONE)
            printf("[%d] %-10s %s\n", j->id, job_state_str(j->state), j->cmdstr);
    }
}

/* ------------------------------------------------------------------ *
 * update_jobs
 *
 * Polls all tracked process groups with waitpid(WNOHANG | WUNTRACED).
 * Updates state to JOB_STOPPED or JOB_DONE as appropriate.
 *
 * We iterate by pgid rather than individual pids because a pipeline may
 * have multiple processes; we consider the whole group done/stopped once
 * waitpid on the group leader returns a terminal event.  A negative pgid
 * passed to waitpid waits for any member of that process group.
 *
 * Completed jobs are marked DONE here but NOT removed automatically –
 * the shell prints a notification and removes them at the next prompt,
 * mirroring standard bash/dash behaviour.
 * ------------------------------------------------------------------ */
void update_jobs(void)
{
    int   status;
    pid_t pid;

    /*
     * Loop over every tracked job.  We use waitpid(-pgid, ...) to wait
     * for any process in the group.  WNOHANG means we never block.
     * WUNTRACED lets us catch SIGTSTP-stopped children.
     */
    for (Job *j = job_list; j; j = j->next) {
        if (j->state == JOB_DONE) continue; /* already reaped */

        pid = waitpid(-(j->pgid), &status, WNOHANG | WUNTRACED);

        if (pid <= 0) continue; /* nothing to report for this group yet */

        if (WIFSTOPPED(status)) {
            j->state = JOB_STOPPED;
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            j->state = JOB_DONE;
        }
        /* WIFCONTINUED: the job was resumed via SIGCONT; mark running */
        else if (WIFCONTINUED(status)) {
            j->state = JOB_RUNNING;
        }
    }
}
