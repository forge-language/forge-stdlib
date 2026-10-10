#define _POSIX_C_SOURCE 200809L
#include "forge/lsprpc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
int main(void) {
    int pipefd[2];assert(pipe(pipefd)==0);
    pid_t pid=fork();assert(pid>=0);
    if(!pid){
        close(pipefd[1]);assert(dup2(pipefd[0],0)==0);close(pipefd[0]);
        assert(fr_lsp_poll(20)==0);
        while(fr_lsp_poll(50)==0){}
        assert(!strcmp(fr_lsp_message(),"hello"));
        while(fr_lsp_poll(50)==0){}
        assert(!strcmp(fr_lsp_message(),"world"));
        assert(fr_lsp_poll(50)==-1);_exit(0);
    }
    close(pipefd[0]);
    /* partial header, then coalesced body and next frame */
    assert(write(pipefd[1],"Content-Len",11)==11);
    struct timespec delay={0,50000000}; nanosleep(&delay,NULL);
    const char *rest="gth: 5\r\n\r\nhelloContent-Length: 5\r\n\r\nworld";
    assert(write(pipefd[1],rest,strlen(rest))==(ssize_t)strlen(rest));close(pipefd[1]);
    int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    #define BAD_FRAME(text) {text, sizeof(text)-1}
    const struct { const char *data; size_t size; } bad[] = {
        BAD_FRAME("Content-Length: -1\r\n\r\n"),
        BAD_FRAME("Content-Length: 1048577\r\n\r\n"),
        BAD_FRAME("Content-Length: 2x\r\n\r\n"),
        BAD_FRAME("Content-Length: 5\r\n\r\nab"),
        BAD_FRAME("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}"),
        BAD_FRAME("Content-Length: 2\r\n\0ignored\r\n\r\n{}"),
        BAD_FRAME("Content-Length: 2\r\n\r\na\0"),
        BAD_FRAME("Content-Length: 0\r\n\r\n"),
    };
    #undef BAD_FRAME
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        assert(pipe(pipefd)==0);pid=fork();assert(pid>=0);
        if(!pid){close(pipefd[1]);dup2(pipefd[0],0);close(pipefd[0]);int r=0;while(!r)r=(int)fr_lsp_poll(50);assert(r==-2);_exit(0);}
        close(pipefd[0]);assert(write(pipefd[1],bad[i].data,bad[i].size)==(ssize_t)bad[i].size);close(pipefd[1]);
        assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    puts("fragmented/coalesced framing, strict bounds and truncated EOF passed");return 0;
}
#else
int main(void){assert(fr_lsp_poll(0)==-3);return 0;}
#endif
