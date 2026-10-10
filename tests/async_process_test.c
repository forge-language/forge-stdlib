#define _POSIX_C_SOURCE 200809L
#include "forge/process.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
static int descendant_dead(pid_t pid) {
    if (kill(pid, 0) < 0 && errno == ESRCH) return 1;
#if defined(__linux__)
    char path[64], buffer[512];
    snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);
    FILE *f=fopen(path,"r");
    if (!f) return 1;
    char *read=fgets(buffer,sizeof(buffer),f);fclose(f);
    char *end=read?strrchr(buffer,')'):NULL;
    return end && end[1]==' ' && end[2]=='Z';
#else
    return 0;
#endif
}
static void finish(int64_t h) {
    for (int i=0;i<3000;i++) {
        if (fr_proc_poll(h)==1) return;
        struct timespec ts={0,1000000}; nanosleep(&ts,NULL);
    }
    assert(!"child did not finish");
}
int main(void) {
    char file[]="/tmp/forge-proc-test-XXXXXX";
    int fd=mkstemp(file);assert(fd>=0);
    const char *script="printf stdout; printf stderr >&2; exit 7\n";
    assert(write(fd,script,strlen(script))==(ssize_t)strlen(script));close(fd);
    int64_t h=fr_proc_start_forge("/bin/sh",file,"--check","","","");assert(h>0);
    assert(fr_proc_release(h)==0);finish(h);
    assert(fr_proc_status(h)==7);assert(fr_proc_error_kind(h)==0);
    assert(!strcmp(fr_proc_stdout(h),"stdout"));assert(!strcmp(fr_proc_stderr(h),"stderr"));
    assert(fr_proc_release(h)==1);assert(fr_proc_cancel(h)==0);
    assert(fr_proc_start_forge("/missing/compiler",file,"--check","","","")==-3);
    pid_t child=fork();assert(child>=0);
    if (!child) {
        close(0);close(1);close(2);
        int64_t x=fr_proc_start_forge("/bin/sh",file,"--check","","","");
        if (x<=0) _exit(1);
        finish(x);
        if (fr_proc_status(x)!=7 || strcmp(fr_proc_stdout(x),"stdout") || strcmp(fr_proc_stderr(x),"stderr")) _exit(2);
        fr_proc_release(x);_exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    FILE *f=fopen(file,"w");assert(f);fputs("exec sleep 30\n",f);fclose(f);
    int64_t jobs[4];for(int i=0;i<4;i++){jobs[i]=fr_proc_start_forge("/bin/sh",file,"--check","","","");assert(jobs[i]>0);}
    assert(fr_proc_start_forge("/bin/sh",file,"--check","","","")==-2);
    for(int i=0;i<4;i++){assert(fr_proc_cancel(jobs[i])==1);finish(jobs[i]);assert(fr_proc_error_kind(jobs[i])==3);assert(fr_proc_release(jobs[i])==1);}
    for(int natural=0;natural<2;natural++) {
        f=fopen(file,"w");assert(f);
        fputs(natural ? "sleep 30 & echo $!; exit 0\n" : "sleep 30 & echo $!; wait\n",f);fclose(f);
        h=fr_proc_start_forge("/bin/sh",file,"--check","","","");assert(h>0);
        for(int i=0;i<1000&&!strlen(fr_proc_stdout(h));i++){
            fr_proc_poll(h);struct timespec ts={0,1000000};nanosleep(&ts,NULL);
        }
        pid_t descendant=(pid_t)strtol(fr_proc_stdout(h),NULL,10);assert(descendant>0);
        if(!natural)assert(fr_proc_cancel(h)==1);
        finish(h);
        for(int i=0;i<1000&&!descendant_dead(descendant);i++){
            struct timespec ts={0,1000000};nanosleep(&ts,NULL);
        }
        assert(descendant_dead(descendant));assert(fr_proc_release(h)==1);
    }
    f=fopen(file,"w");assert(f);fputs("head -c 1100000 /dev/zero\n",f);fclose(f);
    h=fr_proc_start_forge("/bin/sh",file,"--check","","","");assert(h>0);finish(h);
    assert(fr_proc_error_kind(h)==2);assert(fr_proc_release(h)==1);
    unlink(file);puts("async process bounds, lifecycle, separate output and closed std fds passed");return 0;
}
#else
int main(void){assert(fr_proc_start_forge("x","x","x","","","")==-1);return 0;}
#endif
