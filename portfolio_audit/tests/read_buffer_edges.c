#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    int pipefd[2];
    char buf[4] = {'X', 'X', 'X', 'X'};
    if (pipe(pipefd) != 0 || write(pipefd[1], "Z", 1) != 1)
        return 1;
    if (close(pipefd[1]) != 0 || read(pipefd[0], buf, sizeof(buf)) != 1)
        return 2;
    if (memcmp(buf, "ZXXX", sizeof(buf)) != 0 || read(pipefd[0], buf, sizeof(buf)) != 0)
        return 3;
    if (read(-1, buf, sizeof(buf)) != -1 || errno != EBADF)
        return 4;

    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return 5;
    char *mapping = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED || mprotect(mapping + page, page, PROT_NONE) == -1)
        return 6;
    mapping[page - 1] = 'Q';
    int sink = open("/dev/null", O_WRONLY);
    /* /dev/null accepts both bytes. Only the first is readable by the tracer. */
    if (sink < 0 || write(sink, mapping + page - 1, 2) != 2)
        return 7;
    return 0;
}
