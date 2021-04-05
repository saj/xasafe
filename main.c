#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <unistd.h>

#define PROG_NAME              "xasafe"
#define STATUS_XARGS_TERMINATE 255

static void usage(void);
static void shift(const char *argv[], size_t sz, size_t width);
static void die(const char *fmt, ...);
static void die_errnum(int errnum, const char *fmt, ...);

static void handle_signal_dummy(int signo);
static void sigset_add(sigset_t *set, int sigs[], size_t sz);
static void sigprocmask_block_by_sigset(sigset_t *set);
static void sigprocmask_overwrite_by_sigset(sigset_t *set);
static void sigprocmask_block(int sigs[], size_t sz);
static void sigprocmask_overwrite(int sigs[], size_t sz);
static void sigprocmask_zero(void);

void
usage(void) {
  fprintf(stderr, "usage: " PROG_NAME " prog [arg ...]\n");
  exit(STATUS_XARGS_TERMINATE);
}

void
shift(const char *argv[], size_t sz, size_t width) {
  if (width >= sz) {
    argv[0] = NULL;
    return;
  }
  size_t i = 0;
  size_t j = width;
  while (j <= sz) argv[i++] = argv[j++];
}

void
die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, PROG_NAME ": ");
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    fputc(' ', stderr);
    perror(NULL);
  } else {
    fputc('\n', stderr);
  }
  exit(STATUS_XARGS_TERMINATE);
}

void
die_errnum(int errnum, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, PROG_NAME ": ");
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    const char *errstr = strerror(errnum);
    if ((int)errstr == EINVAL) {
      fprintf(stderr, " Unknown error: %d\n", errnum);
    } else {
      fprintf(stderr, " %s\n", errstr);
    }
  } else {
    fputc('\n', stderr);
  }
  exit(STATUS_XARGS_TERMINATE);
}

// Dummy handler used to change our disposition toward SIG_IGN'd signals.
void
handle_signal_dummy(int signo) {}

static void
sigset_add(sigset_t *set, int sigs[], size_t sz) {
  for (size_t i = 0; i < sz; i++) sigaddset(set, sigs[i]);
}

static void
sigprocmask_block_by_sigset(sigset_t *set) {
  errno = 0;
  if (sigprocmask(SIG_BLOCK, set, NULL)) die("sigprocmask:");
}

static void
sigprocmask_overwrite_by_sigset(sigset_t *set) {
  errno = 0;
  if (sigprocmask(SIG_SETMASK, set, NULL)) die("sigprocmask:");
}

static void
sigprocmask_block(int sigs[], size_t sz) {
  sigset_t mask;
  sigemptyset(&mask);
  sigset_add(&mask, sigs, sz);
  sigprocmask_block_by_sigset(&mask);
}

static void
sigprocmask_overwrite(int sigs[], size_t sz) {
  sigset_t mask;
  sigemptyset(&mask);
  sigset_add(&mask, sigs, sz);
  sigprocmask_overwrite_by_sigset(&mask);
}

static void
sigprocmask_zero(void) {
  sigprocmask_overwrite(NULL, 0);
}

int
main(int argc, char *argv[]) {
  if (argc < 2) usage();

  shift((const char **)argv, argc, 1);
  sigprocmask_block((int[]){SIGINT, SIGTERM}, 2);

  int   rc = 0;
  pid_t pid;
  errno = 0;
  pid   = fork();

  if (pid == -1) die("fork:");
  if (pid == 0) {
    sigprocmask_zero();
    errno = 0;
    execvp(argv[0], argv);
    die("exec: %s:", argv[0]);
  } else {
    sigset_t mask;
    sigemptyset(&mask);
    sigset_add(&mask, (int[]){SIGCHLD, SIGINT, SIGTERM}, 3);
    sigprocmask_block_by_sigset(&mask);

    errno = 0;
    if (sigaction(SIGCHLD, // SIG_IGN'd by default
                  &(const struct sigaction){
                      .sa_handler = handle_signal_dummy,
                      .sa_flags   = SA_NOCLDSTOP,
                  },
                  NULL))
      die("sigaction:");

    for (;;) {
      int errnum, sig;
      if ((errnum = sigwait(&mask, &sig))) die_errnum(errnum, "sigwait:");

      int status;
      switch (sig) {
      case SIGCHLD:
        if (waitpid(pid, &status, 0) == -1) die("wait:");
        if (WIFEXITED(status)) rc = WEXITSTATUS(status);
        if (WIFSIGNALED(status)) rc = 128 + WTERMSIG(status);
        goto done;
      default:
        if (kill(pid, SIGTERM)) die("kill:");
      }
    }
  }
done:
  if (rc) die("%s: exit status %d", argv[0], rc);
  exit(rc);
}
